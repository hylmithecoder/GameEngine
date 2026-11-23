# Realtime Collab — edit scene rame-rame

Beberapa editor Ilmeee masuk ke **room** yang sama dan mengedit satu scene
bersama secara realtime (transform terlihat saat di-drag, objek baru/hapus,
komponen, lighting/bloom).

```
 Editor A ─┐                                    ┌─ Editor B
 (C++,     │  wss://realtimeprotokol.ilmeee.com │  (C++,
 Collab-   ├──────────►  servertunnel edge  ◄───┤  CollabClient)
 Client)   │            (TLS, WebSocket splice)  │
           └────────► ilmeee-realtime (Rust) ◄──┘
                      127.0.0.1:$PORT, /ws, /health
```

## 1. Deploy server (sekali)

Server ada di `server/realtime` (Rust, axum). CI: `.github/workflows/deploy.yaml`
— clippy + test → build binary **static musl** → smoke test `/health` →
upload ke servertunnel (sama seperti projectmanager-autolift).

**Dashboard tunnel** (Aplikasi → *Realtime Protokol*):

| Field        | Isi                                                     |
|--------------|---------------------------------------------------------|
| Health check | `/health`                                               |
| Domain       | `realtimeprotokol.ilmeee.com`                           |
| Environment  | `ILMEEE_RT_TOKEN=<token acak>` (wajib untuk internet)   |

Token buat dengan `openssl rand -hex 24`. Opsional: `RT_MAX_ROOMS` (256),
`RT_MAX_PEERS` (32/room), `RT_MAX_KEYS` (50000/room), `RT_MAX_MESSAGE_BYTES`
(1 MiB), `RT_ROOM_TTL_SECS` (7 hari; room kosong lebih lama dari ini dihapus),
`RT_DATA_DIR` (default `$HOME/rooms`; kosong = tanpa simpan ke disk),
`RUST_LOG` (default `info`). `HOST`/`PORT` diisi deployer.

**GitHub** → Settings → Environments → `production`:

- secret `DEPLOY_TOKEN` — deploy token app dari dashboard
- variable `DEPLOY_URL` — `https://<host dashboard>/deploy/v1/apps/<slug>/releases`

Deploy jalan otomatis saat push ke `main` yang mengubah `server/realtime/**`
atau workflow-nya, atau manual lewat *Run workflow*. Pull request hanya test.

Cek: `curl https://realtimeprotokol.ilmeee.com/health` →
`{"status":"ok","rooms":…,"peers":…}`.

## 2. Pakai di editor

**Windows → Realtime Collab**

- **Server**: `wss://realtimeprotokol.ilmeee.com/ws` (default)
- **Token**: nilai `ILMEEE_RT_TOKEN` server
- **Room**: default = nama folder project. Semua orang yang mau kerja bareng
  pakai room yang sama.
- **Name / Color**: tampil di daftar People milik orang lain.

Klik **Join room**:

- Room **kosong** → scene kamu di-upload dan jadi scene room.
- Room **sudah ada isinya** → scene lokal kamu **diganti** dengan scene room
  (save dulu kalau mau disimpan).

Setting disimpan di `~/.ilmeeeengine/collab.json` (chmod 600, berisi token;
room diingat per project).

### Syarat

- Semua orang membuka **project yang sama** (mis. lewat git). Yang dikirim
  hanya data scene; asset (model, tekstur, audio, script) dirujuk dengan path
  relatif project dan harus ada di tiap mesin. Kalau asset tidak ada, Console
  menampilkan `could not load '<obj>' (<path> missing in this project?)`.
- Selama **Play**, tidak ada yang dikirim/diterapkan; perubahan dari orang lain
  ditahan dan diterapkan setelah **Stop**.

### Yang di-sync

| Ikut                                                         | Belum               |
|--------------------------------------------------------------|---------------------|
| Tambah / hapus / rename / reparent objek                     | Canvas / UI (GameUI)|
| Transform (live saat drag gizmo, 20 Hz)                      | Asset file (upload) |
| Semua komponen yang ada di format `.ilmeeescene` (light, camera, physics, collider, audio, video, dialog, animation, spring bone, world text, script + field) | Undo per-user |
| Tekstur per surface, background, NPR, bloom                  | Lock per objek      |
| Presence: siapa sedang memilih objek apa                     |                     |

### Konflik

Last-writer-wins per objek per *slot* (`data` = semua kecuali transform,
`xform` = transform), urutan ditentukan server. Dua orang drag objek yang sama
→ hasil akhir sama di semua editor (yang terakhir sampai di server menang).
Dua orang mengubah komponen berbeda di objek yang sama → slot `data` yang sama,
jadi yang terakhir menang untuk seluruh komponen objek itu.

### Lock & antrian edit

- **Memilih objek = mengunci objek itu untukmu.** Orang lain yang memilih
  objek yang sama masuk **antrian** (FIFO): inspector-nya read-only dengan
  banner *"Locked: X is editing this — You are #1 in the queue"*, gizmo tidak
  muncul, rename/drag/delete di Hierarchy dimatikan, dan di Hierarchy ada tag
  nama + warna pemegang lock.
- Lock pindah ke antrian berikutnya saat pemegang **memilih objek lain /
  deselect / keluar room / putus koneksi**. Yang mendapat giliran dapat pesan
  *"your turn"* di Console.
- Pemegang yang diam **30 detik** padahal ada yang antri otomatis menyerahkan
  lock (dan ikut antri lagi di belakang). Tombol **Hand over** di inspector
  menyerahkan langsung.
- Hapus objek ditolak bila objek itu atau salah satu anaknya sedang dipegang
  orang lain.
- Lock bersifat *advisory* di server (op tetap diterima); yang menegakkan
  adalah UI editor. Perubahan tidak lewat seleksi (mis. reparent anak ke objek
  yang dikunci, tool MCP) tetap last-writer-wins.

### Kehadiran di Scene view

Tiap peer terlihat dengan warnanya sendiri:
- **Kamera editor** mereka sebagai frustum kecil + nama (tombol **Go to view**
  di jendela Collab memindahkan kameramu ke posisi mereka).
- **Kursor** mereka di dunia 3D (titik kena objek/lantai) + nama.
- **Seleksi** mereka sebagai kotak bounding + "(editing)" / "(waiting)".
- Di jendela Collab: objek yang sedang mereka edit dan panel yang sedang mereka
  sentuh (*in Inspector*, *in Hierarchy*, ...). Masing-masing bisa
  dimatikan lewat checkbox *Cameras / Cursors / Selections*.

Presence dikirim maks. 10x/detik dan hanya saat berubah.

**Undo (Ctrl+Z) bersifat lokal**: snapshot undo berisi seluruh scene, jadi
undo bisa ikut membatalkan perubahan orang lain yang datang setelahnya.

### Dari Ilmeee Hub: Online Workspaces

Halaman **Online Workspaces** di Hub (berbagi pengaturan dengan editor lewat
`~/.ilmeeeengine/collab.json`):
- Isi **Server**, **Token**, **Your name** sekali; daftar workspace di server
  muncul dan diperbarui tiap 5 detik (siapa yang online + warnanya, jumlah
  objek, kapan terakhir aktif).
- **Put a project online**: pilih project + nama workspace → **Go online**
  membuka editor dan langsung join (workspace kosong mengambil scene project
  itu).
- **Join** di kartu workspace: membuka salinan lokal yang sudah ditautkan;
  belum ada → pilih folder project-mu (salinan project yang sama) sekali,
  tautan disimpan.
- Editor dibuka dengan `--collab-room <nama>` dan join otomatis ±1,5 s setelah
  scene termuat.

## 3. Jalankan lokal (dev)

```bash
cd server/realtime && make run          # ws://127.0.0.1:9230/ws, token "dev", data di ./data
# editor: Server = ws://127.0.0.1:9230/ws, Token = dev
```

Test:

```bash
cd server/realtime && make lint test     # 14 test Rust (unit + end-to-end)
nix-shell --run "cmake --build build --target TestCollabClient"
./build/bin/TestCollabClient             # offline: codec + aturan konflik
ILMEEE_RT_URL=ws://127.0.0.1:9230/ws ILMEEE_RT_TOKEN=dev ./build/bin/TestCollabClient
```

## 4. Protokol (untuk tool lain)

Envelope sama dengan IPC engine (`include/core_engine/net/Message.hpp`):
`{"v":1,"type":…,"payload":{…}}`, request pakai `id`, balasan pakai `re`.

| Arah | Pesan | Payload |
|------|-------|---------|
| C→S | `hello` (event, pertama) | `{protocol:1, role, pid, token}` |
| S→C | `welcome` / `bye` | `{protocol, role:"realtime", connection, server}` / `{reason, retry:false}` |
| C→S | `room.join` (request) | `{room, name, color}` → `{self, room, seq, fresh, peers[], state[], locks[]}` |
| C→S | `room.leave` (request) | `{}` |
| C→S | `op` | `{key, slot, data}` atau `{key, del:true}` |
| S→C | `op` (ke semua, termasuk pengirim) | `{seq, from, key, slot, data, del}` |
| C→S | `presence` | objek bebas ≤ 4 KB; editor: `{selection, selectionName, cam:[x,y,z,yaw,pitch,fov], cursor:[x,y,z]\|null, panel}` |
| S→C | `presence` | `{from, data}` |
| S→C | `peer.joined` / `peer.left` | `{id, name, color, presence}` / `{id, reason}` |
| S→C | `notice` | `{level, message}` |
| HTTP | `GET /rooms` + `Authorization: Bearer <token>` | `{rooms:[{name, peers:[{name,color}], objects, seq, updated_unix}]}` (terbaru dulu; 401 bila token salah) |
| C→S | `ping` (request) | `{}` → `{unix}` |
| C→S | `lock.acquire` / `lock.release` | `{key}` (maks. 16 lock/antrian per peer) |
| S→C | `lock` (ke semua) | `{key, holder, queue[]}` — holder 0 = bebas; tabel lock juga ada di reply `room.join` sebagai `locks[]` |

Key editor: objek = `o<objectId 16 hex>` dengan slot `data`
(`{blob, parent, rigCamera, name}`, blob = base64 `.ilmeeescene` 1 entity) dan
`xform` (`{p, r, s}`); setting scene = key `env`, slot `data`. Detail:
`include/core_engine/CollabCodec.hpp`.
