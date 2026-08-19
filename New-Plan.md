# Terminal Modular Folder Structure

## Goal

Kelompokkan codebase berdasarkan modul agar komponen, handler, dan lifecycle yang saling berhubungan berada dalam satu tempat. Pemindahan struktur tidak boleh mengubah tampilan atau perilaku aplikasi.

## Target structure

```text
Terminal/
├─ Assets/
│  └─ ui/
│     ├─ core.json
│     └─ screens/
├─ src/
│  ├─ native/
│  │  ├─ window-shell/
│  │  │  ├─ application_container.h
│  │  │  ├─ application_container.cpp
│  │  │  ├─ window_container.h
│  │  │  ├─ window_container.cpp
│  │  │  ├─ tabs_component.h
│  │  │  ├─ tabs_component.cpp
│  │  │  ├─ route_handler.h
│  │  │  └─ route_handler.cpp
│  │  ├─ components/
│  │  │  ├─ button/
│  │  │  ├─ card/
│  │  │  ├─ checkbox/
│  │  │  ├─ combo/
│  │  │  ├─ container/
│  │  │  ├─ dialog/
│  │  │  ├─ input/
│  │  │  ├─ list/
│  │  │  ├─ screen/
│  │  │  ├─ scrollbar/
│  │  │  ├─ text/
│  │  │  ├─ toggle/
│  │  │  └─ window/
│  │  ├─ rendering/
│  │  ├─ accessibility/
│  │  ├─ platform/
│  │  ├─ config/
│  │  └─ theme/
│  ├─ application/
│  │  ├─ adapters/
│  │  └─ bridge/
│  ├─ logic/
│  │  ├─ application/
│  │  ├─ features/
│  │  ├─ platform/
│  │  └─ storage/
│  ├─ updater/
│  │  ├─ updater.h
│  │  └─ updater.cpp
│  └─ main.cpp
├─ distribution/
│  ├─ installer/
│  ├─ packaging/
│  ├─ update-feed/
│  └─ scripts/
├─ tests/
└─ Terminal.sln
```

## Module ownership

### `src/native/window-shell`

Satu tempat untuk top-level window, tab screen, route selection, multi-window lifecycle, close handling, taskbar/tray coordination, dan komunikasi secondary launch. Setiap `WindowContainer` tetap memiliki `route-tabs` dan satu screen aktif.

### `src/native/components`

Berisi komponen UI reusable. Komponen hanya menangani measure, layout, paint, input, focus, dan event generik. Komponen tidak memuat business logic.

### `src/native/rendering`, `accessibility`, `platform`, `config`, dan `theme`

Berisi seluruh implementasi teknis raw Win32: GDI rendering, UI Automation, Windows API integration, UI config resolution, DPI, serta theme.

### `src/application`

Menjadi penghubung event UI dengan business logic. Adapter menerjemahkan action dan payload UI menjadi pemanggilan typed logic serta mengembalikan patch/state ke UI.

### `src/logic`

Berisi business feature, persistence, process launch, dan facade aplikasi. Folder ini tidak mengetahui detail komponen atau rendering UI.

### `src/updater` dan `distribution`

`src/updater` berisi runtime update client yang dikompilasi ke aplikasi. `distribution` berisi installer, Velopack packaging, feed generation, dan release scripts yang tidak menjadi bagian dari UI/business runtime.

## Rules

- Kelompokkan kode dalam satu folder modul, bukan satu file besar.
- `window-shell` boleh mengatur window dan route, tetapi tidak menjalankan business logic.
- `route-tabs` tetap bagian permanen dari setiap `WindowContainer` dan item-nya dibuat otomatis dari screen config.
- Screen baru tetap dibuat dari JSON di `Assets/ui/screens`.
- Action screen masuk melalui adapter, bukan langsung memanggil `src/logic` dari komponen.
- Semua window memakai identity aplikasi yang sama agar dikelompokkan dalam satu ikon taskbar.
- Normal launch atau pilihan Jump List membuat `WindowContainer` baru; pilihan route membuka screen tersebut di window baru.
- Pemindahan file dilakukan tanpa redesign, perubahan behavior, atau penambahan dependency.

## Migration boundary

Pemindahan dilakukan per folder dan hanya mencakup perubahan path, include, project source list, serta build script. Jangan mencampur pemindahan struktur dengan perubahan renderer, UI, business logic, installer contract, atau updater behavior.

## Phase 2 — Module contract (coordinator pattern)

Fase ini dijalankan **setelah** pemindahan struktur selesai dan build hijau. Tujuannya membuat feature plug-and-play: menambah feature = menambah satu modul terisolasi, tanpa menyentuh feature lain.

### Pola coordinator

Setiap feature memiliki satu **coordinator** — satu-satunya pintu masuk folder feature. Gate hanya mengenal coordinator, tidak pernah file internal feature.

```text
src/logic/features/chrome/
├─ chrome_coordinator.h        ← satu-satunya file yang dikenal gate
├─ chrome_profiles.cpp         ← internal, tidak boleh di-include dari luar folder
├─ chrome_visible_set.cpp      ← internal
└─ chrome_scan.cpp             ← internal
```

Analogi restoran: gate = pelayan, coordinator = kepala dapur tiap stasiun, file internal = juru masak. Pelayan hanya berbicara dengan kepala dapur; juru masak stasiun lain tidak boleh saling memanggil.

### Aturan dependency

- Panah dependency hanya satu arah: UI JSON → `UiConfigGate` → adapter → gate logic → coordinator → internal feature.
- Coordinator antar feature tidak boleh saling memanggil; komunikasi antar feature (jika benar-benar diperlukan) hanya lewat gate.
- Coordinator tidak boleh mengetahui UI, viewState, adapter, atau JSON. Ia mengekspos method typed dan data polos.
- Gate adalah direktori kecil: hanya mendaftasi dan mengekspos coordinator (`gate.Chrome()`, `gate.Terminal()`, dst.). Gate berubah hanya saat feature ditambah/dihapus, bukan saat feature berubah internal.

### Menambah feature baru (3 artefak + 1 baris)

1. Buat folder feature di `src/logic/features/<nama>/` berisi coordinator dan file internalnya.
2. Daftarkan coordinator di gate — satu baris registrasi.
3. Buat adapter di `src/application/adapters/` yang menerjemahkan action/viewState ke pemanggilan coordinator.
4. Buat screen JSON di `Assets/ui/screens/` dan jalankan `tools/Merge-UiConfig.ps1`.

Menghapus feature = menghapus keempat hal di atas; tidak boleh ada file lain yang berubah.

### Batasan fase

- Tidak mengubah behavior aplikasi; hanya menata ulang permukaan pemanggilan.
- Dilakukan per feature: pindahkan satu feature ke pola coordinator, build, test, baru lanjut ke feature berikutnya.
- Kontrak UI (schema JSON, adapter event, UiPatch) tidak berubah pada fase ini.

## Phase 3 — UI drop-in (model food court)

Aplikasi diperlakukan seperti food court: tiap screen adalah "stan" berupa folder mandiri. Orchestrator memindai folder tiap build — menu valid ditampilkan, folder invalid dilewati dengan peringatan (aplikasi tetap jalan), folder dihapus = stan hilang tanpa merusak yang lain. Wiring business logic hanya dilakukan saat screen membutuhkannya ("bilang ke pelayan" = buat adapter + daftarkan).

### Bentuk folder screen

```text
Assets/ui/screens/
├─ terminal/
│  └─ screen.json        ← wajib; route id = nama folder
├─ settings/
│  └─ screen.json
└─ <screen-baru>/         ← taruh folder → muncul; hapus folder → hilang
   └─ screen.json
```

- Semua milik screen tinggal di dalam foldernya (JSON, dan nanti partial/icon bila perlu).
- Komponen tetap shared di `src/ui/components/`; tidak ada C++ per screen.
- File datar lama (`screens/<route>.json`) tetap didukung selama transisi.

### Aturan drop-in

1. **Auto-scan**: `tools/Merge-UiConfig.ps1` membaca `screens/<route>/screen.json`; tidak ada pendaftaran terpusat.
2. **Valid = tampil, invalid = dilewati**: validasi dilakukan per folder; folder yang gagal schema diberi peringatan dan tidak mematikan aplikasi (mengubah perilaku gate yang saat ini fatal).
3. **Aman dihapus**: referensi route yang tidak ada (navigasi/initialRoute) menjadi peringatan + kontrol dinonaktifkan, bukan crash.
4. **Logic opsional**: screen tanpa action berjalan murni dari JSON; wiring logic = satu adapter yang self-register (nama adapter = nama folder).

### Langkah bertahap (walk before lock — refine sambil jalan)

1. **Walk 1**: dukungan folder di merge tool + screen pertama (`terminal`) tampil end-to-end. Tanpa perubahan C++.
2. **Walk 2**: isolasi validasi per folder di gate (invalid tidak membunuh aplikasi). Perubahan C++ kecil di `resolved_ui_document`/gate.
3. **Walk 3**: referensi route yang hilang ditoleransi (peringatan, bukan fatal).
4. **Walk 4**: adapter self-registration untuk wiring logic; satu screen logic penuh (terminal) dengan action + viewState nyata.

Tiap walk diakhiri build hijau + test hijau sebelum lanjut. Detail tiap walk boleh berubah sesuai temuan saat implementasi — dokumen ini mengikuti realita, bukan sebaliknya.

### Batasan fase

- Skema JSON dan kontrak UiPatch tidak berubah kecuali dipaksa oleh walk terkait.
- Screen lama tidak boleh berubah tampilannya; perubahan hanya mekanisme pemindaian dan toleransi error.
