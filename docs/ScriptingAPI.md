# Ilmeee Scripting API (C++ Native Script)

Script project adalah class C++ di `assets/scripts/` yang diturunkan dari
`Component`. Editor meng-compile tiap `.cpp` menjadi shared library dan
menjalankannya saat Play. Header SDK:

| Header | Isi |
|---|---|
| `core_engine/core/Component.hpp` | `Component`, `ScriptProperties` (include ini saja) |
| `core_engine/script/GameObject.hpp` | `Vec3`, `Transform`, `GameObject`, `GameObjectRef`, `ObjectRef<T>` |
| `core_engine/script/SceneObjects.hpp` | `AudioGameObject`, `VideoPlayerGameObject`, `LightGameObject`, `CameraGameObject`, `UiGameObject`, `UiTextGameObject`, `UiImageGameObject`, `UiButtonGameObject`, `TextGameObject`, `DialogGameObject` |

Versi API: `ilmeee::script::kApiVersion = 5`. Script yang di-compile untuk
versi lain ditolak, cukup tekan **Recompile**.

## Kerangka script

```cpp
// assets/scripts/spherecontroller.hpp
#pragma once
#include <core_engine/core/Component.hpp>

class spherecontroller : public Component {
public:
    float speed = 3.0f;
    Vec3 tempat{0, 1, 0};
    GameObjectRef target;
    ObjectRef<AudioGameObject> musik;

    void DescribeProperties(ScriptProperties &properties) override;
    void Start() override;
    void Update(float deltaTime) override;
};
```

```cpp
// assets/scripts/spherecontroller.cpp
#include "spherecontroller.hpp"

void spherecontroller::DescribeProperties(ScriptProperties &properties) {
    properties.Field("Kecepatan", speed);
    properties.Field("Tempat", tempat);
    properties.Field("Tujuan", target);
    properties.Field("Musik", musik);
}

void spherecontroller::Start() {
    gameObject.SetPosition(tempat);
    if (musik.IsValid()) musik.Get().Play();
    Log("Mulai di " + gameObject.Name());
}

void spherecontroller::Update(float deltaTime) {
    if (IsKeyDown("W")) gameObject.Translate({0, 0, -speed * deltaTime});
}
```

## Component

| Anggota | Keterangan |
|---|---|
| `gameObject` | Object yang memasang script ini |
| `Awake()`, `Start()` | Dipanggil sekali saat Play dimulai |
| `Update(float deltaTime)` | Tiap frame selama Play (bukan saat Pause) |
| `OnEnable()`, `OnDisable()` | Saat script aktif / berhenti |
| `DescribeProperties(ScriptProperties&)` | Daftar field yang tampil di Inspector |
| `Log(msg)` | Console > Output, awalan `[Script]` |
| `LogWarning(msg)` | Sama, dengan `[WARNING]` |
| `LogError(msg)` | Dengan `[ERROR]`, dan membuka Console di editor |
| `IsKeyDown(key)` | Selama tombol ditahan. `"W"`, `"A"`, `"S"`, `"D"`, `"Space"`, `"E"`, `"Enter"`, `"Interact"` |
| `IsKeyPressed(key)` | Hanya di frame pertama tombol ditekan (menu, dialog, interaksi) |

`"E"` dan `"Interact"` sama: tombol E di keyboard, atau Button UI dengan
Action `Interact` (untuk Android). WASD ikut joystick UI.

## Coroutine (jeda tanpa memblok game)

Fungsi member yang mengembalikan `Coroutine` bisa berhenti sementara dengan
`co_await`, lalu lanjut di frame berikutnya / setelah waktu tertentu (mirip
`IEnumerator` + `yield return` di Unity, memakai coroutine C++20).

```cpp
class Lampu : public Component {
public:
    LightGameObject lampu;
    Coroutine Kedip() {
        for (int i = 0; i < 3; ++i) {
            lampu.SetIntensity(0);  co_await WaitForSeconds(0.5f);
            lampu.SetIntensity(5);  co_await WaitForSeconds(0.5f);
        }
        Log("selesai kedip");
    }
    Coroutine Intro() {
        Log("tunggu 2 detik...");
        co_await WaitForSeconds(2);
        co_await Kedip();                       // tunggu coroutine lain selesai
        co_await WaitUntil([this] { return IsKeyPressed("E"); });
        Log("E ditekan!");
    }
    void Start() override {
        lampu = gameObject.As<LightGameObject>();
        StartCoroutine(Intro());
    }
};
```

| API | Keterangan |
|---|---|
| `StartCoroutine(Fungsi())` | Jalan langsung sampai `co_await` pertama. Mengembalikan `CoroutineId` (0 kalau sudah selesai) |
| `StopCoroutine(id)` / `StopAllCoroutines()` | Hentikan. Aman dipanggil dari dalam coroutine (termasuk untuk dirinya sendiri) |
| `IsCoroutineRunning(id)` | `true` selama masih berjalan |
| `co_await WaitForSeconds(detik)` | Jeda dengan waktu game (deltaTime yang sama dengan `Update`) |
| `co_await WaitForNextFrame()` | Lanjut di frame berikutnya |
| `co_await WaitUntil(fungsi)` / `WaitWhile(fungsi)` | Dicek tiap frame; lanjut saat `true` / saat `false` |
| `co_await FungsiLain()` | Jalankan coroutine lain sampai selesai, lalu lanjut |
| `co_return;` | Selesai lebih awal |

Aturan:
- Coroutine maju sekali per frame, **setelah** `Update`. Coroutine yang dibuat
  di frame ini (termasuk dari `Start`) baru dilanjutkan frame berikutnya.
- Semua coroutine dihapus saat Play di-Stop (bersama component-nya).
- Pakai fungsi **member** (aman memakai `this`). Jangan jadikan lambda yang
  meng-capture sebagai coroutine: capture-nya hilang setelah `StartCoroutine`.
- Exception di dalam coroutine menghentikan script itu (pesan error muncul di
  Console), sama seperti exception di `Update`.

## Field Inspector (`ScriptProperties::Field`)

| Tipe C++ | Tampilan Inspector |
|---|---|
| `float` | DragFloat |
| `bool` | Checkbox |
| `std::string` | InputText (panjang bebas, maks. 65535 byte saat disimpan) |
| `Vec3` | DragFloat3 (X, Y, Z) |
| `GameObjectRef` | Pilihan object / drag dari Hierarchy |
| `ObjectRef<T>` | Sama seperti `GameObjectRef`, tapi `Get()` langsung bertipe `T` |
| `std::vector<T>` | List: tiap item pakai widget tipe `T`, tombol **X** (hapus), **+ Add** (salin item terakhir), **Clear** |

`T` di `std::vector<T>` boleh semua tipe di atas kecuali `std::vector` lagi
(list di dalam list tidak didukung). Maksimal 4096 item per list.

Nama field harus unik. Nilai default `float`/`Vec3` harus finite. Nilai dari
Inspector tersimpan di `.ilmeeescene` (Vec3 sejak format v1.10, `string` dan
`std::vector` sejak v1.14).

```cpp
class Spawner : public Component {
public:
    std::string judul = "Gelombang 1";
    std::vector<std::string> dialog{"Halo", "Siap?"};
    std::vector<Vec3> titik;
    std::vector<ObjectRef<AudioGameObject>> suara;

    void DescribeProperties(ScriptProperties &p) override {
        p.Field("Judul", judul);
        p.Field("Dialog", dialog);
        p.Field("Titik Spawn", titik);
        p.Field("Suara", suara);
    }
    void Start() override {
        for (const auto &baris : dialog) Log(judul + ": " + baris);
        for (auto &s : suara) if (s.IsValid()) s.Get().Play();
    }
};
```

Tipe lain (mis. `int`) gagal compile dengan pesan *constraint not satisfied*
pada `Field`. Untuk menambah tipe baru di engine: tambah nilai di akhir
`ScriptValueType`, spesialisasi `ilmeee::script::FieldType<T>`, lalu
`ReadScalar`/`WriteScalar` (`NativeScripts.cpp`), widget Inspector, dan format
scene — `std::vector<T>` untuk tipe baru itu otomatis ikut jalan.

`Transform` tidak bisa jadi satu field. Kalau perlu, pakai tiga field
`Vec3` (posisi, rotasi, skala), lalu gabungkan:

```cpp
Vec3 pos, rot, skala{1, 1, 1};
// ...
gameObject.SetTransform({pos, rot, skala});
```

## GameObject

Berlaku juga untuk semua object bertipe di bawah.

| Fungsi | Keterangan |
|---|---|
| `IsValid()` | Object masih ada di scene |
| `Id()`, `Name()` | Identitas dan nama di Hierarchy |
| `GetTransform()` / `SetTransform(Transform)` | `position`, `rotation` (euler, derajat), `scale` |
| `GetPosition()` / `SetPosition(Vec3)` | Posisi saja |
| `Translate(Vec3)` | Geser relatif dari posisi sekarang |
| `Find(name)` | Cari object lain berdasarkan nama |
| `Is(ObjectKind)` | `Light`, `Camera`, `Audio`, `Video`, `Ui` (elemen Canvas), atau `Dialog`? |
| `As<T>()` | Pandangan bertipe, misalnya `As<AudioGameObject>()` |

Transform bersifat **local**, yaitu relatif ke parent. Untuk object di root,
nilainya sama dengan world.

`Vec3` mendukung `+`, `-`, dan `* float`.

## Object bertipe

Cara mendapatkannya:

```cpp
auto audio = target.Get().As<AudioGameObject>(); // dari GameObjectRef
auto video = gameObject.Find("Video Screen").As<VideoPlayerGameObject>();
ObjectRef<LightGameObject> lampu;                 // langsung sebagai field
```

`IsValid()` bernilai false kalau object tidak punya komponen tersebut, jadi
selalu cek dulu. Semua setter mengembalikan `bool`: `false` berarti nilainya
ditolak (bukan angka finite, di luar rentang, atau komponennya tidak ada).

### AudioGameObject

Untuk object Audio Source atau object apa pun yang punya komponen Audio Source.

| Fungsi | Keterangan |
|---|---|
| `Play()` | Mulai dari awal. Kalau dipanggil di `Start()`, menggantikan *Play On Start* |
| `Stop()` | Berhenti. Di `Start()`, mencegah *Play On Start* |
| `IsPlaying()` | Sedang terdengar |
| `Get/SetVolume(float)` | ≥ 0, langsung berlaku |
| `Get/SetClip(path)` | Path relatif project, berlaku di `Play()` berikutnya |
| `Get/SetLoop(bool)` | Berlaku di `Play()` berikutnya |
| `Get/SetSpatial(bool)` | 3D (ikut posisi kamera game) atau 2D |
| `Get/SetMinDistance`, `Get/SetMaxDistance` | Jarak redaman 3D |
| `IsEnabled()` / `SetEnabled(bool)` | Komponen nonaktif tidak bisa di-Play |

### VideoPlayerGameObject

| Fungsi | Keterangan |
|---|---|
| `Play()` | Mulai dari awal. Di `Start()`, menggantikan *Play On Start* |
| `Stop()` | Berhenti dan layar kosong |
| `IsPlaying()` | Sedang berjalan (false setelah selesai kalau tidak loop) |
| `Get/SetClip(path)` | Mengganti clip saat berjalan akan memuat ulang video |
| `Get/SetLoop(bool)` | Sama, memuat ulang |
| `Get/SetVolume(float)` | Volume suara video |
| `Get/SetUnlit(bool)` | Terang penuh, atau ikut pencahayaan |
| `IsEnabled()` / `SetEnabled(bool)` | |

Pause/Seek per object belum ada. Pause global ikut tombol Pause editor.

### LightGameObject

| Fungsi | Keterangan |
|---|---|
| `Get/SetColor(Vec3)` | RGB 0..1 |
| `Get/SetIntensity(float)` | ≥ 0 |
| `Get/SetRange(float)` | Point / Spot |
| `Get/SetSpotAngle(float)` | Derajat, 0..179 |
| `GetType()` / `SetType(LightGameObject::Type)` | `Directional`, `Point`, `Spot` |

### CameraGameObject

| Fungsi | Keterangan |
|---|---|
| `IsOrthographic()` / `SetOrthographic(bool)` | Perspective atau Orthographic |
| `Get/SetFov(float)` | Derajat, 1..179 |
| `Get/SetOrthoSize(float)` | Setengah tinggi view orthographic |
| `Get/SetNear`, `Get/SetFar` | Clip plane |

### UiGameObject

Elemen Canvas (Text, Image, Button, Joystick). Dapatkan dengan field
`ObjectRef<UiGameObject>` (dropdown Inspector juga menampilkan elemen UI di
bagian **UI**), atau `gameObject.Find("Text 1").As<UiGameObject>()`.

| Fungsi | Keterangan |
|---|---|
| `Get/SetText(text)` | Teks Text/Button |
| `Get/SetImage(path)` | Gambar elemen Image atau sprite Button, path relatif project |
| `Get/SetAction(action)` | Action Button |
| `IsVisible()` / `SetVisible(bool)`, `Show()`, `Hide()` | Elemen tersembunyi tidak digambar dan tidak bisa diklik |
| `GetPosition()` / `SetPosition(Vec3)` / `Translate(Vec3)` | `x`, `y` dalam pixel referensi dari anchor (`z` diabaikan) |
| `Get/SetSize(Vec3)` | `x` = lebar, `y` = tinggi |
| `Get/SetFontSize(float)` | 1..512 |
| `Get/SetColor(Vec3)`, `Get/SetAlpha(float)` | Warna teks / tint Image / knob joystick |
| `Get/SetBackground(Vec3)`, `Get/SetBackgroundAlpha(float)` | Warna latar |
| `WasClicked()` | Diklik / di-tap sejak `Update()` sebelumnya. Berlaku untuk semua jenis elemen, bukan hanya Button |

Semua perubahan UI dari script (dan dari Dialog) dikembalikan saat **Stop**.
Centang **Visible** di Inspector elemen UI untuk kondisi awalnya; di editor
elemen tersembunyi tampil samar dan diberi tanda `[hidden]` di Hierarchy.
Teks Text sekarang otomatis turun baris (word wrap) sesuai lebar elemen.

### Custom script pada UI

Pilih elemen Text, Image, atau Button di Canvas, lalu **Inspector > Add
Component** untuk menempelkan script C++ yang sudah dikompilasi. `gameObject`
di script itu menunjuk langsung ke elemen UI-nya; `Awake`, `Start`, `Update`,
dan enable/disable dijalankan seperti script pada objek 3D. Binding dan field
Inspector disimpan di scene serta ikut Build Linux/Android.

`UiTextGameObject`, `UiImageGameObject`, dan `UiButtonGameObject` mewarisi API
`UiGameObject`, tetapi `IsValid()` juga memeriksa jenis elemen yang benar.
Gunakan `ObjectRef<...>` untuk target dan drag elemen Hierarchy atau pilih
dropdown di Inspector. `WasClicked()` diperiksa di `Update()`, bukan `Start()`.

```cpp
// Tempelkan script ini pada Button UI. Pilih label dunia pada field title.
#include <core_engine/core/Component.hpp>
class ChangeTitle : public Component {
public:
    ObjectRef<TextGameObject> title;
    void DescribeProperties(ScriptProperties &p) override {
        p.Field("title", title);
    }
    void Start() override {
        gameObject.As<UiButtonGameObject>().SetText("Bicara");
    }
    void Update(float) override {
        if (gameObject.As<UiButtonGameObject>().WasClicked() && title.IsValid())
            title.Get().SetText("Halo, selamat datang!");
    }
};
```

### TextGameObject — Text di dunia 3D

Hierarchy: klik kanan **Scene > Create > Text (3D)**, atau **Create Child >
Text (3D)** pada objek karakter. Ini bukan Canvas: posisi/rotasi/skala memakai
unit dunia dan parent transform. Glyph berupa bidang XY menghadap lokal +Z,
memakai depth test sehingga tertutup benda di depannya. Atur Content,
Font size, Color, Alignment dan Visible di Inspector. Teks beberapa baris
didukung dengan newline. Undo/Redo, save/reopen, dan ekspor didukung.

Script: `gameObject.As<TextGameObject>()`,
`gameObject.Find("Text (3D)").As<TextGameObject>()`, atau
`ObjectRef<TextGameObject>`. API-nya: `Get/SetText`, `Get/SetFontSize` (tinggi
baris dalam unit dunia, 0.001..100), `Get/SetColor`, `Get/SetAlpha`,
`Get/SetAlignment(TextGameObject::Alignment::Left/Center/Right)`,
`IsVisible`, `SetVisible`, `Show`, `Hide`, dan transform GameObject biasa.
Perubahan Text saat Play dikembalikan saat Stop.

Font bawaan MiSans Latin ikut paket game (fallback embedded bila tidak ada).
Ini text surface tanpa ekstrusi, bukan mesh huruf tebal, rich text, atau
billboard otomatis. Konten render dibatasi 16 KiB per objek.

### DialogGameObject

Object yang punya komponen **Dialog** (lihat di bawah).

| Fungsi | Keterangan |
|---|---|
| `StartDialog()` | Buka dialog dari baris pertama. `false` kalau dialog lain sedang terbuka, tidak ada baris, atau komponen nonaktif |
| `Next()` | Selesaikan baris yang sedang diketik, atau lanjut ke baris berikutnya (menutup di akhir) |
| `StopDialog()` | Tutup dialog object ini |
| `IsTalking()` | Dialog object ini sedang terbuka |
| `GetLineIndex()` | Baris ke berapa (mulai 0), `-1` kalau tertutup |
| `GetSpeaker()`, `GetLine()` | Nama dan teks lengkap baris sekarang |
| `Get/SetScript(text)` | Isi dialog (format sama dengan Inspector), dipakai di `StartDialog()` berikutnya. **Dialog File** di Inspector lebih diutamakan |
| `IsEnabled()` / `SetEnabled(bool)` | |
| `Get/SetRange(float)` | Jarak interaksi |
| `Get/SetSpeed(float)` | Huruf per detik, 0 = langsung tampil |

## Komponen Dialog (bawaan engine)

Tambahkan lewat **Add Component > Dialog** pada karakter (object apa pun).
Tidak perlu script sama sekali.

| Field | Keterangan |
|---|---|
| Trigger | `Interact` (dekati lalu tekan E / Enter / Button `Interact`), `On Start` (langsung saat Play), `Script Only` (hanya `StartDialog()`) |
| Range | Jarak ke player. Player = object pertama yang punya Movement Script; kalau tidak ada, kamera game |
| Dialog Box | Elemen UI yang muncul selama dialog (panel) |
| Text | Elemen UI yang menerima baris dialog (efek mengetik) |
| Speaker Name | Opsional, menerima nama pembicara; disembunyikan untuk narasi |
| Prompt | Opsional, petunjuk ("[E] Talk") yang muncul saat player dalam jangkauan |
| Lines / Dialog File | Isi dialog langsung di Inspector, atau file teks di project (drag dari Explorer) |
| Typing Speed | Huruf per detik, 0 = langsung |
| Repeatable | Bisa diajak bicara lagi setelah selesai |
| Freeze Player | Movement (WASD/joystick) diabaikan selama dialog; kamera tetap bisa digerakkan |

Tombol **Create Dialog UI** membuat elemen Box / Text / Name yang belum ada
di Canvas, plus **Talk Button** (Button dengan Action `Interact`) sebagai
Prompt, dan langsung menghubungkannya. Banyak karakter boleh
memakai elemen UI yang sama. Saat Play dimulai semua elemen itu
disembunyikan, lalu ditampilkan saat dialog dibuka.

Format dialog, satu baris per kalimat:

```text
# komentar dan baris kosong diabaikan
Sakura: Halo! Kamu murid baru ya?
Iya, salam kenal.
: Angin sore berhembus pelan.
Sakura: Ayo ke kelas.\nNanti telat!
```

- `Nama: teks` → pembicara `Nama` (maks. 40 byte sebelum `: `, jadi
  `Jam 10:30` tetap dianggap teks).
- Tanpa nama → pembicara = nama object pemilik komponen.
- `: teks` → narasi, Speaker Name disembunyikan.
- `\n` → pindah baris di dalam satu kalimat.

### Tombol trigger (Interact)

Ada tiga cara memulai dialog dengan Trigger `Interact`, semuanya hanya saat
player dalam Range:

1. Tombol **E** atau **Enter** di keyboard.
2. Button UI dengan **Action** `Interact` (pilih dari dropdown *Built-in
   actions* di Inspector Button). Ini yang dipakai di HP.
3. Paling rapi: pilih Button `Interact` itu sebagai **Prompt** di komponen
   Dialog. Button-nya otomatis hanya muncul saat player dekat karakter, dan
   bisa diklik / di-tap / diganti tombol E.

Button bisa memakai **sprite** sendiri: drag gambar dari Explorer ke field
**Sprite** di Inspector, ke baris Button di Hierarchy, atau ke Button di
canvas Scene. Sprite menggantikan warna Background, teks tetap digambar di
atasnya (kosongkan Text kalau gambarnya sudah bertulisan), dan sprite jadi
lebih gelap saat ditekan. Script: `UiGameObject::SetImage(path)`.

Saat dialog terbuka: E / Enter, atau klik/tap Dialog Box / Text, akan
menyelesaikan ketikan, lalu lanjut ke baris berikutnya. Hanya satu dialog
yang terbuka dalam satu waktu.

## Contoh

```cpp
// Naikkan volume musik saat Space ditekan, dan lampu berkedip.
void Demo::Update(float dt) {
    waktu += dt;
    auto bgm = musik.Get();
    if (IsKeyDown("Space") && bgm.IsValid())
        bgm.SetVolume(std::min(1.0f, bgm.GetVolume() + dt));

    if (auto l = lampu.Get(); l.IsValid())
        l.SetIntensity(1.0f + 0.5f * std::sin(waktu * 4.0f));
}
```

```cpp
// Putar video sekali, lalu log saat selesai.
void Intro::Start() {
    layar = gameObject.Find("Video Screen").As<VideoPlayerGameObject>();
    if (!layar.IsValid()) { LogError("Video Screen tidak ditemukan"); return; }
    layar.SetLoop(false);
    layar.Play();
}
void Intro::Update(float) {
    if (!selesai && layar.IsValid() && !layar.IsPlaying()) {
        selesai = true;
        Log("Intro selesai");
    }
}
```

`IsPlaying()` sudah true sejak `Play()` dipanggil (atau sejak awal untuk
*Play On Start*), walaupun clip baru dibuka di frame berikutnya. Jadi pola di
atas aman. Kalau clip gagal dibuka, nilainya false dan error-nya muncul di
Console.

```cpp
// Ganti potret karakter sesuai pembicara, dan beri hadiah setelah dialog.
class Potret : public Component {
public:
    ObjectRef<DialogGameObject> npc;
    ObjectRef<UiGameObject> wajah;   // elemen Image
    bool selesai = false;
    void DescribeProperties(ScriptProperties &p) override {
        p.Field("NPC", npc);
        p.Field("Wajah", wajah);
    }
    void Update(float) override {
        auto d = npc.Get();
        auto w = wajah.Get();
        if (!d.IsValid() || !w.IsValid()) return;
        w.SetVisible(d.IsTalking());
        if (d.IsTalking())
            w.SetImage(d.GetSpeaker() == "Sakura" ? "assets/ui/sakura.png"
                                                  : "assets/ui/player.png");
        if (!selesai && wasTalking && !d.IsTalking()) {
            selesai = true; // dialog baru saja ditutup
            Log("Dapat hadiah!");
        }
        wasTalking = d.IsTalking();
    }
private:
    bool wasTalking = false;
};
```

```cpp
// Tombol UI biasa (Text pun bisa) dan tombol keyboard satu kali tekan.
void Menu::Update(float) {
    if (tombolMulai.Get().WasClicked()) tombolMulai.Get().Hide();
    if (IsKeyPressed("Enter")) Log("Enter ditekan sekali");
}
```

## Nama terlarang

Nama class script tidak boleh berupa keyword C++ atau nama SDK:
`Component`, `Vec3`, `Transform`, `GameObject`, `GameObjectRef`,
`ObjectRef`, `AudioGameObject`, `VideoPlayerGameObject`, `LightGameObject`,
`CameraGameObject`, `UiGameObject`, `DialogGameObject`, `ScriptProperties`.

Alias pendek (`Vec3`, `AudioGameObject`, ...) hanya ada saat compile script
(`-DILMEEE_SCRIPT_BUILD`). Di kode engine, pakai `ilmeee::script::...`.

## MCP

Server MCP editor punya tool `list_ui_elements`, `set_ui_element`,
`get_dialog`, `set_dialog` (tambah/ubah komponen Dialog, `createUi: true`
untuk membuat UI-nya) dan `start_dialog` (saat Play, untuk testing). Elemen UI
dirujuk dengan namanya, mis. `{"index": 7, "lines": "Sakura: Halo!",
"createUi": true}` lalu `save_scene`.

Spring bone (physics rambut/dada/rok, model PMX): `get_spring_bones` dan
`set_spring_bones`. `{"index": 12, "auto": true}` mendeteksi bone dari nama
dan menambah collider kepala/dada/paha; field lain (`enabled`, `wind`,
`chains`, `colliders`) menimpa. Jalan saat Play, lalu `save_scene`.
`set_animation` memasang clip (`.ilmeeeanim`/`.vmd`) atau controller ke model
PMX, mis. `{"index": 12, "clip": "assets/animations/PhysicsDemo.ilmeeeanim"}`.

### Animation authoring lewat MCP

Authoring berjalan di editor native. Urutan umum: `list_scene_objects` →
`get_skeleton` → `generate_animation` atau `create_animation` →
`get_animation` / `sample_animation` → `edit_animation` → `set_animation` →
`save_scene`. Pakai index karakter dari hasil `list_scene_objects`; index
bisa berubah setelah struktur scene diedit.

| Tool | Parameter utama | Hasil |
| --- | --- | --- |
| `get_skeleton` | `index`; opsional `group`, `contains`, `offset`, `limit` | Bone, parent, bind position, grup, role humanoid, dan jenis generator |
| `get_animation` | `path`; opsional `index`, `includeKeys`, `bone`, `offset`, `limit` | Metadata, jumlah track/key, kecocokan rig, dan keyframe bila diminta |
| `sample_animation` | `path`, `time`; opsional `index`, `bone`, `loop` | Transform lokal; dengan index juga posisi bone di ruang model |
| `create_animation` | `path`; opsional `index`, `name`, `duration`, `fps`, `ops`, `overwrite` | Clip `.ilmeeeanim` baru, default 3 detik / 30 FPS |
| `generate_animation` | `index`, `path`, `type`; opsional `name`, `duration`, `intensity`, `ops`, `overwrite` | Clip prosedural sesuai role rig |
| `edit_animation` | `path`, `ops`; opsional `index`, `output`, `name`, `overwrite` | Clip hasil edit atau salinan baru |

`path` dan `output` relatif terhadap project aktif. Akses keluar project,
termasuk lewat symlink, ditolak. Output harus `.ilmeeeanim`; untuk mengedit
VMD, isi `output` agar sumber `.vmd` tetap utuh. `create_animation`,
`generate_animation`, dan salinan `output` menolak file yang sudah ada kecuali
`overwrite: true`; `edit_animation` tanpa `output` memperbarui clip asal.
Seluruh batch divalidasi sebelum file disimpan; kegagalan tidak mengubah
clip asal. Edit file langsung tersimpan dan berada di luar Undo/Redo scene.
Tool yang menulis clip dan `set_animation` memerlukan Play dihentikan.

Bone menerima nama asli, nama alternatif, atau `@role`, mis. `@head`,
`@upper_body`, `@arm_l`, `@elbow_r`. Untuk validasi rig dan penggunaan role,
sertakan `index`. Role yang tersedia ditampilkan oleh `get_skeleton`; pemetaan
nama mencakup PMX, Mixamo, VRM, dan Biped, sementara playback mengikuti
skeleton yang didukung importer engine. Sumbu karakter: +Z depan, +X kiri
karakter, +Y atas. Rotasi input/output adalah Euler lokal `[x,y,z]` dalam
derajat; translasi adalah offset lokal dalam satuan model, scale default `[1,1,1]`.
Pada file `.ilmeeeanim`, rotasi tetap disimpan sebagai quaternion XYZW.

`get_skeleton` memfilter halaman bone, sedangkan `roles`/`groups` selalu
merangkum seluruh rig. `contains` mencocokkan substring nama asli/alternatif
secara case-sensitive. `get_animation` selalu menampilkan ringkasan seluruh
track; `bone` memfilter halaman `keys` ketika `includeKeys: true`.
Keduanya memakai `offset` default 0 dan `limit` default 200, maksimal 1000,
dengan `nextOffset: null` pada halaman terakhir. Kecocokan playback dihitung
dengan nama track asli secara persis; nama alternatif dan role diubah menjadi
nama asli ketika membuat key. `sample_animation` memakai interpolasi clip
dan FK tanpa simulasi spring bone/IK; waktu di-clamp ke durasi atau dibungkus
jika `loop: true`. Ini inspeksi numerik pose, bukan render GIF/PNG.

Jenis generator: `idle`, `breathe`, `wave`, `nod`, `shake_head`, `sway`,
`bow`, `talk`, `happy_bounce`, `look_around`. Durasi default menyesuaikan
gerakan; nilai eksplisit 0.2–60 detik, `intensity` 0–3 (default 1).
Pose lengan santai dikoreksi berdasarkan bind position agar T-pose/A-pose
memakai sudut dasar yang sesuai. Generator menambahkan track hanya untuk
role yang ada pada rig.

Contoh argumen `generate_animation` (ganti index sesuai karakter):

```json
{"index":12,"path":"assets/animations/Wave.ilmeeeanim","type":"wave","duration":2.4,"intensity":1}
```

Kemudian `edit_animation` untuk mengatur kepala dan memperlambat gerakan:

```json
{
  "index":12,
  "path":"assets/animations/Wave.ilmeeeanim",
  "ops":[
    {"op":"set_key","bone":"@head","time":0.7,"rotation":[0,-12,5],"ease":"smooth"},
    {"op":"scale_time","factor":1.25}
  ]
}
```

Operasi `ops` (maksimal 2000 per request):

| `op` | Field | Perilaku |
| --- | --- | --- |
| `set_key` | `bone`, `time`; opsional `rotation`, `translation`, `scale`, `ease` | Tambah/ganti key, pertahankan nilai transform yang tidak diberikan dari pose pada waktu itu; durasi bertambah bila perlu |
| `delete_key` | `bone`, `time` | Hapus key di waktu itu; error jika tidak ada |
| `clear_track` | `bone` | Hapus seluruh track bone |
| `add_rotation` | `bone`, `rotation`; opsional `from`, `to` | Tambah rotasi pada key yang sudah ada dalam rentang inklusif, dengan offset quaternion dikalikan dari kiri |
| `mirror` | opsional `from: "left"/"right"`, `bone` | Salin track ke pasangan sisi; default kiri ke kanan; refleksi YZ, target diganti; `bone` memilih satu track |
| `scale_time` | `factor` (0.01–100) | Kalikan seluruh waktu key dan durasi |
| `shift` | `by`; opsional `bone` | Geser waktu key, clamp ke nol, key terakhir menang bila bertumpuk; durasi diperpanjang bila perlu |
| `set_duration` | `duration`; opsional `fps` | Ganti metadata; durasi tidak boleh memotong key yang masih ada |

`set_key.time` 0–600 detik, `set_duration.duration` >0–600 detik, FPS 1–240.
Validasi akhir membatasi clip maksimal 3600 detik / 100000 key / 4096 track.
`ease`: `linear`, `smooth` (default), `ease_in`, `ease_out`. Kurva Bezier
melekat pada key tujuan segmen. `set_key` tanpa `ease` menggunakan `smooth`.

Untuk memasang hasil, panggil `set_animation` dengan
`{"index":12,"clip":"assets/animations/Wave.ilmeeeanim"}`, lalu `save_scene`.
Pemberian clip mengaktifkan animasi dan mengosongkan controller lama kecuali
`enabled`/`controller` juga diberikan secara eksplisit; controller yang diisi
tetap mendapat prioritas. Kecepatan playback `speed` 0–4.

Verifikasi tanpa GPU: build dan jalankan `TestAnimationTooling` serta
`TestMcpServer`; tes tooling mencakup generator, edit atomik, konversi VMD,
pagination, sampling FK, batas project/symlink, dan round trip HTTP.
