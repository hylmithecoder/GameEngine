//! Project files for a workspace, so an editor on a fresh device can join
//! without having the project folder.
//!
//! Content-addressed: every file is a blob named by its SHA-256, and each room
//! has one manifest (`path → hash, size`). Uploading the same texture twice, or
//! the same asset in two rooms, stores it once. The flow:
//!
//!   publisher: POST /files/missing {hashes}   → {missing}
//!              PUT  /files/blob/<hash>        (only the missing ones)
//!              PUT  /rooms/<room>/files       {files:[{path,hash,size}], by}
//!   joiner:    GET  /rooms/<room>/files       → manifest
//!              GET  /files/blob/<hash>        (only what it does not have)
//!
//! Linked manifests (LAN server on the publisher's own computer): the
//! publisher sends the manifest with `linked_dir` = its project folder and
//! uploads nothing; blobs are then read straight from that folder, so the
//! project is not copied a second time onto the same disk. Only accepted
//! from loopback on a server started with `RT_ALLOW_LINKED=1` (the Hub's
//! "Host on LAN"), and never shown to clients.
//!
//! Layout under `files_dir`: `blobs/<aa>/<hash>` and `manifests/<room>.json`.
//! Blobs no manifest refers to are removed by `gc` once they are an hour old
//! (younger ones may belong to a publish still in progress).

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, SystemTime};

pub const MAX_PATH_LEN: usize = 512;
const GC_GRACE: Duration = Duration::from_secs(3600);

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct FileEntry {
    pub path: String,
    pub hash: String,
    pub size: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct Manifest {
    pub files: Vec<FileEntry>,
    #[serde(default)]
    pub by: String,
    #[serde(default)]
    pub updated_unix: u64,
    #[serde(default)]
    pub total: u64,
    /// Project folder the files are served from (linked manifest); None =
    /// uploaded blobs. Server-side only: stripped before a client sees it.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub linked_dir: Option<String>,
}

pub fn valid_hash(h: &str) -> bool {
    h.len() == 64
        && h.bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}

/// A project-relative path with `/` separators that cannot leave the folder
/// it is extracted into (no `..`, no absolute paths, no drive letters).
pub fn valid_path(p: &str) -> bool {
    !p.is_empty()
        && p.len() <= MAX_PATH_LEN
        && !p.starts_with('/')
        && !p.contains('\\')
        && !p.contains(':')
        && !p.bytes().any(|b| b < 0x20)
        && p.split('/').all(|c| !c.is_empty() && c != "." && c != "..")
}

pub struct Store {
    dir: PathBuf,
    used: AtomicU64,
    tmp_counter: AtomicU64,
    /// hash -> (file in a linked project folder, size it had when linked).
    linked: Mutex<HashMap<String, (PathBuf, u64)>>,
}

impl Store {
    pub fn open(dir: PathBuf) -> std::io::Result<Self> {
        std::fs::create_dir_all(dir.join("blobs"))?;
        std::fs::create_dir_all(dir.join("manifests"))?;
        let s = Self {
            dir,
            used: AtomicU64::new(0),
            tmp_counter: AtomicU64::new(0),
            linked: Mutex::new(HashMap::new()),
        };
        s.reindex_linked();
        let mut used = 0;
        s.for_each_blob(|_, _, len| used += len);
        s.used.store(used, Ordering::Relaxed);
        Ok(s)
    }

    pub fn used_bytes(&self) -> u64 {
        self.used.load(Ordering::Relaxed)
    }

    fn blob_path(&self, hash: &str) -> PathBuf {
        self.dir.join("blobs").join(&hash[..2]).join(hash)
    }

    fn manifest_path(&self, room: &str) -> PathBuf {
        self.dir.join("manifests").join(format!("{room}.json"))
    }

    pub fn has_blob(&self, hash: &str) -> bool {
        valid_hash(hash) && self.blob_path(hash).is_file()
    }

    /// Path to read a blob from: the stored copy, else the file of a linked
    /// project (if it still has the size it was linked with; the client
    /// verifies the hash, so an edited file is caught there).
    pub fn blob_file(&self, hash: &str) -> Option<PathBuf> {
        if self.has_blob(hash) {
            return Some(self.blob_path(hash));
        }
        if !valid_hash(hash) {
            return None;
        }
        let linked = self.linked.lock().unwrap_or_else(|e| e.into_inner());
        let (path, size) = linked.get(hash)?;
        let md = std::fs::metadata(path).ok()?;
        (md.is_file() && md.len() == *size).then(|| path.clone())
    }

    fn manifests(&self) -> Vec<Manifest> {
        let mut out = Vec::new();
        if let Ok(entries) = std::fs::read_dir(self.dir.join("manifests")) {
            for e in entries.flatten() {
                if let Ok(bytes) = std::fs::read(e.path())
                    && let Ok(m) = serde_json::from_slice::<Manifest>(&bytes)
                {
                    out.push(m);
                }
            }
        }
        out
    }

    /// Rebuilds the hash -> linked file index from the saved manifests.
    fn reindex_linked(&self) {
        let mut index = HashMap::new();
        for m in self.manifests() {
            let Some(dir) = m.linked_dir.as_deref() else {
                continue;
            };
            for f in m.files {
                let path = f.path.split('/').fold(PathBuf::from(dir), |p, c| p.join(c));
                index.insert(f.hash, (path, f.size));
            }
        }
        *self.linked.lock().unwrap_or_else(|e| e.into_inner()) = index;
    }

    pub fn missing(&self, hashes: &[String]) -> Vec<String> {
        let mut seen = HashSet::new();
        hashes
            .iter()
            .filter(|h| seen.insert(h.as_str()))
            .filter(|h| !self.has_blob(h))
            .cloned()
            .collect()
    }

    /// A temp file for an upload in progress; `commit_blob` moves it in place.
    pub fn temp_path(&self) -> PathBuf {
        let n = self.tmp_counter.fetch_add(1, Ordering::Relaxed);
        self.dir
            .join("blobs")
            .join(format!(".up-{}-{n}", std::process::id()))
    }

    /// Checks the digest of a finished upload and stores it under its hash.
    pub fn commit_blob(
        &self,
        tmp: &Path,
        expected: &str,
        digest: &str,
        len: u64,
    ) -> Result<(), String> {
        if digest != expected {
            let _ = std::fs::remove_file(tmp);
            return Err("content does not match its hash".into());
        }
        let dest = self.blob_path(expected);
        if dest.is_file() {
            let _ = std::fs::remove_file(tmp); // someone else stored it meanwhile
            return Ok(());
        }
        std::fs::create_dir_all(dest.parent().unwrap()).map_err(|e| e.to_string())?;
        std::fs::rename(tmp, &dest).map_err(|e| e.to_string())?;
        self.used.fetch_add(len, Ordering::Relaxed);
        Ok(())
    }

    pub fn manifest(&self, room: &str) -> Option<Manifest> {
        let bytes = std::fs::read(self.manifest_path(room)).ok()?;
        serde_json::from_slice(&bytes).ok()
    }

    /// Validates and stores a room's manifest. Every blob must already be
    /// uploaded; `Err((missing, message))` lists the ones that are not.
    pub fn put_manifest(
        &self,
        room: &str,
        mut m: Manifest,
        max_files: usize,
        max_total: u64,
        now: u64,
    ) -> Result<Manifest, (Vec<String>, String)> {
        m.linked_dir = None;
        let total = Self::check_manifest(&m, max_files, max_total)?;
        let hashes: Vec<String> = m.files.iter().map(|f| f.hash.clone()).collect();
        let missing = self.missing(&hashes);
        if !missing.is_empty() {
            return Err((missing, "upload the missing blobs first".into()));
        }
        self.save_manifest(room, m, total, now)
    }

    /// A manifest whose files stay in `dir` (see the module docs). Each file
    /// must exist there with the listed size. The caller makes sure the
    /// request comes from this computer.
    pub fn put_linked_manifest(
        &self,
        room: &str,
        mut m: Manifest,
        dir: &Path,
        max_files: usize,
        now: u64,
    ) -> Result<Manifest, String> {
        if !dir.is_absolute() || !dir.is_dir() {
            return Err("linked_dir must be an existing absolute folder".into());
        }
        // Linked files take no space here: no project size limit.
        let total = Self::check_manifest(&m, max_files, u64::MAX).map_err(|e| e.1)?;
        for f in &m.files {
            let p = f.path.split('/').fold(dir.to_path_buf(), |p, c| p.join(c));
            match std::fs::metadata(&p) {
                Ok(md) if md.is_file() && md.len() == f.size => {}
                _ => return Err(format!("{} changed or is missing; share again", f.path)),
            }
        }
        m.linked_dir = Some(dir.to_string_lossy().into_owned());
        let saved = self.save_manifest(room, m, total, now).map_err(|e| e.1)?;
        self.reindex_linked();
        Ok(saved)
    }

    fn check_manifest(
        m: &Manifest,
        max_files: usize,
        max_total: u64,
    ) -> Result<u64, (Vec<String>, String)> {
        if m.files.len() > max_files {
            return Err((vec![], format!("too many files (max {max_files})")));
        }
        let mut paths = HashSet::new();
        let mut total: u64 = 0;
        for f in &m.files {
            if !valid_path(&f.path) {
                return Err((vec![], format!("bad path: {}", f.path)));
            }
            if !valid_hash(&f.hash) {
                return Err((vec![], format!("bad hash for {}", f.path)));
            }
            if !paths.insert(f.path.to_ascii_lowercase()) {
                return Err((vec![], format!("duplicate path: {}", f.path)));
            }
            total = total.saturating_add(f.size);
        }
        if total > max_total {
            return Err((
                vec![],
                format!("project too large ({total} bytes, max {max_total})"),
            ));
        }
        Ok(total)
    }

    fn save_manifest(
        &self,
        room: &str,
        mut m: Manifest,
        total: u64,
        now: u64,
    ) -> Result<Manifest, (Vec<String>, String)> {
        m.by.truncate(64);
        m.total = total;
        m.updated_unix = now;
        let path = self.manifest_path(room);
        let tmp = path.with_extension("json.tmp");
        let bytes = serde_json::to_vec(&m).map_err(|e| (vec![], e.to_string()))?;
        std::fs::write(&tmp, bytes)
            .and_then(|_| std::fs::rename(&tmp, &path))
            .map_err(|e| (vec![], e.to_string()))?;
        Ok(m)
    }

    pub fn remove_manifest(&self, room: &str) {
        let _ = std::fs::remove_file(self.manifest_path(room));
        self.reindex_linked();
    }

    fn for_each_blob(&self, mut f: impl FnMut(&Path, Option<SystemTime>, u64)) {
        let Ok(shards) = std::fs::read_dir(self.dir.join("blobs")) else {
            return;
        };
        for shard in shards.flatten() {
            let Ok(entries) = std::fs::read_dir(shard.path()) else {
                continue;
            };
            for e in entries.flatten() {
                if let Ok(md) = e.metadata()
                    && md.is_file()
                {
                    f(&e.path(), md.modified().ok(), md.len());
                }
            }
        }
    }

    /// Deletes blobs no manifest refers to (and stale upload temp files).
    /// Returns how many files were removed.
    pub fn gc(&self) -> usize {
        // A linked manifest serves from its folder: a copy uploaded earlier
        // for the same room is not needed any more.
        let mut keep = HashSet::new();
        for m in self.manifests() {
            if m.linked_dir.is_none() {
                keep.extend(m.files.into_iter().map(|f| f.hash));
            }
        }
        let now = SystemTime::now();
        let old = |t: Option<SystemTime>| {
            t.and_then(|t| now.duration_since(t).ok())
                .is_none_or(|age| age > GC_GRACE)
        };
        let mut doomed = Vec::new();
        self.for_each_blob(|p, modified, len| {
            let name = p.file_name().and_then(|n| n.to_str()).unwrap_or("");
            if !keep.contains(name) && old(modified) {
                doomed.push((p.to_path_buf(), len));
            }
        });
        // Upload temp files sit directly in blobs/.
        if let Ok(entries) = std::fs::read_dir(self.dir.join("blobs")) {
            for e in entries.flatten() {
                if e.file_name().to_string_lossy().starts_with(".up-")
                    && let Ok(md) = e.metadata()
                    && old(md.modified().ok())
                {
                    let _ = std::fs::remove_file(e.path());
                }
            }
        }
        let mut n = 0;
        for (p, len) in doomed {
            if std::fs::remove_file(&p).is_ok() {
                self.used
                    .fetch_sub(len.min(self.used_bytes()), Ordering::Relaxed);
                n += 1;
            }
        }
        n
    }
}

/// Incremental SHA-256 for streamed uploads.
pub struct Hasher(Sha256);

impl Default for Hasher {
    fn default() -> Self {
        Self(Sha256::new())
    }
}

impl Hasher {
    pub fn update(&mut self, data: &[u8]) {
        self.0.update(data);
    }
    pub fn finish(self) -> String {
        hex::encode(self.0.finalize())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sha(data: &[u8]) -> String {
        let mut h = Hasher::default();
        h.update(data);
        h.finish()
    }

    #[test]
    fn paths_and_hashes() {
        assert!(valid_path("Assets/tex/a.png"));
        assert!(valid_path(".ilmeee/project.json"));
        for bad in [
            "",
            "/etc/passwd",
            "a/../b",
            "..",
            "a//b",
            "C:/x",
            "a\\b",
            "./a",
            "a/",
        ] {
            assert!(!valid_path(bad), "{bad}");
        }
        assert_eq!(
            sha(b"abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        assert!(valid_hash(&sha(b"abc")));
        assert!(!valid_hash("ABC"));
    }

    #[test]
    fn store_round_trip_and_gc() {
        let dir = std::env::temp_dir().join(format!("ilmeee-rt-files-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let s = Store::open(dir.clone()).unwrap();
        let data = b"hello blob";
        let h = sha(data);
        assert_eq!(s.missing(&[h.clone(), h.clone()]), vec![h.clone()]);

        let tmp = s.temp_path();
        std::fs::write(&tmp, data).unwrap();
        assert!(s.commit_blob(&tmp, &"0".repeat(64), &h, 10).is_err());
        assert!(!tmp.exists());
        std::fs::write(&tmp, data).unwrap();
        s.commit_blob(&tmp, &h, &h, data.len() as u64).unwrap();
        assert!(s.has_blob(&h));
        assert_eq!(s.used_bytes(), data.len() as u64);

        let entry = |p: &str, hash: &str| FileEntry {
            path: p.into(),
            hash: hash.into(),
            size: 10,
        };
        let m = |files| Manifest {
            files,
            by: "me".into(),
            ..Default::default()
        };
        let other = sha(b"not uploaded");
        let err = s
            .put_manifest("r", m(vec![entry("a.txt", &other)]), 10, 100, 1)
            .unwrap_err();
        assert_eq!(err.0, vec![other]);
        assert!(
            s.put_manifest("r", m(vec![entry("../x", &h)]), 10, 100, 1)
                .is_err()
        );
        assert!(
            s.put_manifest("r", m(vec![entry("a", &h), entry("A", &h)]), 10, 100, 1)
                .is_err()
        );
        assert!(
            s.put_manifest("r", m(vec![entry("a", &h)]), 10, 5, 1)
                .is_err()
        );
        let saved = s
            .put_manifest("r", m(vec![entry("a.txt", &h)]), 10, 100, 7)
            .unwrap();
        assert_eq!(saved.total, 10);
        assert_eq!(s.manifest("r").unwrap().files, saved.files);

        // Linked: served from the folder, never copied; refused when a file
        // does not match.
        let proj = dir.join("project");
        std::fs::create_dir_all(proj.join("Assets")).unwrap();
        std::fs::write(proj.join("Assets").join("big.bin"), b"linked data").unwrap();
        let lh = sha(b"linked data");
        let lm = Manifest {
            files: vec![FileEntry {
                path: "Assets/big.bin".into(),
                hash: lh.clone(),
                size: 11,
            }],
            ..Default::default()
        };
        assert!(
            s.put_linked_manifest("l", lm.clone(), Path::new("rel"), 10, 1)
                .is_err()
        );
        let mut wrong = lm.clone();
        wrong.files[0].size = 3;
        assert!(s.put_linked_manifest("l", wrong, &proj, 10, 1).is_err());
        s.put_linked_manifest("l", lm, &proj, 10, 1).unwrap();
        assert!(!s.has_blob(&lh)); // nothing copied
        assert_eq!(s.blob_file(&lh), Some(proj.join("Assets").join("big.bin")));
        std::fs::write(proj.join("Assets").join("big.bin"), b"edited").unwrap();
        assert_eq!(s.blob_file(&lh), None); // changed since sharing
        s.remove_manifest("l");

        // Referenced: kept. Unreferenced but young: kept (grace period).
        assert_eq!(s.gc(), 0);
        s.remove_manifest("r");
        assert_eq!(s.gc(), 0);
        assert!(s.has_blob(&h));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
