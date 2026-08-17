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
