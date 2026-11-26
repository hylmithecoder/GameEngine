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

- Semua orang membuka **project yang sama**. Scene dikirim live; asset (model,
  tekstur, audio, script) dirujuk dengan path relatif project dan harus ada di
  tiap mesin — paling gampang lewat **file project di server** (lihat
  "Device baru tanpa folder project" di bawah), atau git/salinan manual. Kalau
  asset tidak ada, Console menampilkan
  `could not load '<obj>' (<path> missing in this project?)`.
- Selama **Play**, tidak ada yang dikirim/diterapkan; perubahan dari orang lain
  ditahan dan diterapkan setelah **Stop**.

### Yang di-sync

| Ikut                                                         | Belum               |
|--------------------------------------------------------------|---------------------|
| Tambah / hapus / rename / reparent objek                     | Canvas / UI (GameUI)|
| Transform (live saat drag gizmo, 20 Hz)                      | Asset baru otomatis (upload ulang manual) |
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

### Device baru tanpa folder project

Server menyimpan **file project** per workspace, jadi device yang masih fresh
bisa langsung join:
- **Go online** (centang *Upload the project files…*, default nyala) meng-upload
  project dulu, baru membuka editor. Hanya file yang belum ada di server yang
  dikirim (disimpan per SHA-256; file sama di project/room lain cuma disimpan
  sekali).
- Di device lain, kartu workspace menampilkan `Project files: N (ukuran)`.
  **Join** → download ke cache
  `~/.ilmeeeengine/workspaces/<host server>/<workspace>/` → editor terbuka di
  situ dan join. Join berikutnya hanya mengambil file yang berubah dan
  menghapus file yang sudah dibuang dari project.
- **Upload files** (kartu workspace, untuk pemilik salinan asli) mengirim ulang
  perubahan asset — mis. setelah import tekstur/model baru. Belum otomatis.
- **Use my own copy** menautkan folder yang sudah kamu punya (git dsb.)
  sebagai ganti download.
- Tidak ikut di-upload: `build/`, `.git/`, `.cache/`, `.vscode/`, `.idea/`,
  `.ilmeee/native-scripts/` (script hasil compile), `compile_commands.json`,
  `TempScene_*.json`, `*.blend1`, file sampah OS, plus semua yang ditulis di
  `.ilmeeeignore` di root project (satu per baris: `folder/`, `*.ext`, atau
  path; `#` komentar). Cache hash lokal: `.ilmeee/sync-state.json`.
- Batas server (env): `RT_MAX_FILE_BYTES` (512 MiB per file),
  `RT_MAX_PROJECT_BYTES` (4 GiB per project), `RT_MAX_PROJECT_FILES` (100 000),
  `RT_MAX_STORAGE_BYTES` (16 GiB total), lokasi `RT_FILES_DIR`
  (default `<data dir>/files`). File yang tidak dirujuk manifest mana pun
  dihapus otomatis setelah 1 jam; manifest ikut terhapus saat room kedaluwarsa.
- **Kalau ada file yang gagal** (nama tidak valid di Windows, file hilang di
  server, koneksi putus sesaat), file lain tetap di-download: tiap file dicoba
  sampai 3x, lalu yang gagal ditulis di **Logs** Hub (`not downloaded: <path>:
  <alasan>`) dan editor tetap dibuka dengan file yang sudah ada. Download baru
  berhenti total kalau server tidak bisa dihubungi 3 file berturut-turut.
- **Nama file yang tidak bisa dipakai di Windows** — karakter `< > " | ? *`,
  nama device (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`9`, `LPT1`–`9`, juga
  dengan ekstensi seperti `aux.png`), nama yang berakhiran titik/spasi, dua file
  yang hanya beda huruf besar/kecil, dan path > 150 karakter — dilaporkan
  sebagai *warning* saat **Go online / Upload files**. Ganti namanya di project
  asli supaya peer Windows bisa mengambilnya.
- Path asset di scene selalu disimpan relatif ke project dengan `/`
  (`Assets/model.pmx`). Asset **di luar** folder project tetap tersimpan
  absolut — file itu tidak ikut di-upload dan tidak akan ada di device lain;
  pindahkan ke dalam project dulu.
- Salinan hasil download adalah milik server: perubahan file lokal di folder
  cache akan ditimpa versi server saat Join berikutnya (scene tetap aman, karena
  scene datang dari room). Upload hanya dari salinan asli.

### Di jaringan lokal (LAN) tanpa internet

Hub → **Online Workspaces** punya dua tab: **Online server** dan **On this
network (LAN)**. Kartu workspace diberi label `ONLINE` (biru) atau `LAN`
(hijau), dan salinan hasil download disimpan terpisah
(`~/.ilmeeeengine/workspaces/lan-<id server>/<workspace>/`, jadi tetap ketemu
walau IP host berganti).

- **Host on LAN** menjalankan `ilmeee-realtime` (ikut di paket Linux/Windows)
  di komputer ini: TCP **9230** (WebSocket + HTTP) dan UDP **9231** (discovery).
  *LAN password* opsional (kosong = semua orang di jaringan ini boleh join).
  File project disimpan di komputer host (`~/.ilmeeeengine/lan-server/`).
- **Share a project on this computer's LAN server** = "Go online", tapi ke
  server LAN sendiri.
- **Workspaces on this network** mencari otomatis tiap 10 detik:
  1. *Broadcast UDP* ke alamat broadcast tiap interface, dihitung dari netmask
     asli (WiFi `/16` → `172.18.255.255`, bukan tebakan `x.x.x.255`), plus
     `255.255.255.255`. Menjangkau satu subnet, tanpa internet.
  2. *Rendezvous* lewat server online (kalau token online diisi): host LAN
     mengumumkan IP LAN-nya tiap menit ke `POST /lan/announce`; server
     mengelompokkannya per IP publik. Device lain di WiFi yang sama (IP publik
     sama) mendapat daftarnya dari `GET /lan/nearby` lalu konek **langsung** ke
     IP LAN itu. Ini yang menemukan host di subnet lain, mis. `172.18.9.42` ↔
     `172.18.10.4` yang masing-masing `/24`. Server online hanya menyimpan
     alamat, bukan data/asset.
  3. Tiap alamat dicek `GET /health` dulu; yang tidak bisa dijangkau (AP
     *client isolation*) tidak ditampilkan → pakai server online.
- Editor yang dibuka dari kartu LAN mendapat URL/token server LAN lewat
  environment (`ILMEEE_COLLAB_URL`, `ILMEEE_COLLAB_TOKEN`) hanya untuk sesi itu;
  setting server online di `collab.json` tidak berubah.
- Firewall: buka TCP 9230 + UDP 9231 di komputer host (Windows menanyakannya
  saat pertama kali Host on LAN; NixOS: `networking.firewall.allowedTCPPorts`
  / `allowedUDPPorts`).

Server: `RT_DISCOVERY_PORT` (0 = mati, default; Hub memakai 9231),
`RT_SERVER_ID`, `RT_SERVER_NAME`. Kode: `server/realtime/src/lan.rs`,
`include/core_engine/net/LanDiscovery.hpp`.

## 3. Jalankan lokal (dev)

```bash
cd server/realtime && make run          # ws://127.0.0.1:9230/ws, token "dev", data di ./data
# editor: Server = ws://127.0.0.1:9230/ws, Token = dev
```

Test:

```bash
cd server/realtime && make lint test     # 22 test Rust (unit + end-to-end)
nix-shell --run "cmake --build build --target TestCollabClient"
./build/bin/TestCollabClient             # offline: codec + aturan konflik
ILMEEE_RT_URL=ws://127.0.0.1:9230/ws ILMEEE_RT_TOKEN=dev ./build/bin/TestCollabClient
# file project: SHA-256, aturan ignore, lalu publish -> download fresh -> update
ILMEEE_RT_URL=ws://127.0.0.1:9230/ws ILMEEE_RT_TOKEN=dev ./build/bin/TestProjectSync
# + cek "1 file gagal tidak menghentikan yang lain" (menghapus blob di server):
ILMEEE_RT_URL=... ILMEEE_RT_TOKEN=dev ILMEEE_RT_FILES_DIR=server/realtime/data/files ./build/bin/TestProjectSync
# LAN: server dengan RT_DISCOVERY_PORT=9231 RT_SERVER_ID=lan-test, lalu
ILMEEE_RT_URL=ws://127.0.0.1:9230/ws ILMEEE_RT_TOKEN=dev ILMEEE_LAN_UDP=9231 ./build/bin/TestLanDiscovery
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
| HTTP | `GET /rooms` + `Authorization: Bearer <token>` | `{rooms:[{name, peers:[{name,color}], objects, seq, updated_unix, files:{count,bytes,updated_unix}\|null}]}` (terbaru dulu; 401 bila token salah) |
| HTTP | `POST /files/missing` | `{hashes:[sha256…]}` → `{missing:[…]}` |
| HTTP | `PUT /files/blob/<sha256>` | isi file mentah; 201 disimpan, 200 sudah ada, 400 isi ≠ hash, 413 terlalu besar, 507 penyimpanan penuh |
| HTTP | `GET /files/blob/<sha256>` | isi file (404 bila tidak ada) |
| HTTP | `PUT /rooms/<room>/files` | `{by, files:[{path,hash,size}]}` → `{ok,count,bytes,updated_unix}`; 409 `{missing}` bila blob belum di-upload; 400 path tidak aman (`..`, absolut, `\`, `:`) |
| HTTP | `GET /rooms/<room>/files` | manifest `{by, files[], total, updated_unix}`; 404 bila belum ada |
| C→S | `ping` (request) | `{}` → `{unix}` |
| C→S | `lock.acquire` / `lock.release` | `{key}` (maks. 16 lock/antrian per peer) |
| S→C | `lock` (ke semua) | `{key, holder, queue[]}` — holder 0 = bebas; tabel lock juga ada di reply `room.join` sebagai `locks[]` |

Key editor: objek = `o<objectId 16 hex>` dengan slot `data`
(`{blob, parent, rigCamera, name}`, blob = base64 `.ilmeeescene` 1 entity) dan
`xform` (`{p, r, s}`); setting scene = key `env`, slot `data`. Detail:
`include/core_engine/CollabCodec.hpp`.
