# Agent Guide — Open Terminal Greenfield Native

Dokumen ini berlaku untuk repository `C:\VSCODE\Teminal`. Instruksi user untuk task aktif selalu
menjadi prioritas utama.

## Source ownership dan batas repository

- `C:\VSCODE\Teminal` adalah repository canonical untuk aplikasi native greenfield yang baru.
  Source, project file, asset, test, dan dokumentasi baru dibuat langsung di repository ini.
- `Open-terminal\` dan `Open-terminal-native\` adalah nested repository independen yang sudah
  mempunyai Git masing-masing dan diabaikan oleh parent repository.
- Kedua nested repository tersebut reference-only untuk aplikasi baru. Boleh dibaca secara sempit
  untuk memahami behavior, inventory komponen, dan kontrak business yang sudah ada, tetapi jangan
  mengimpor, me-link, atau menjadikannya dependency source/build/runtime aplikasi baru.
- Jangan mengedit nested repository kecuali user secara eksplisit menargetkan repository tersebut.
  Jika ditargetkan, tetap bekerja hanya di repository itu dan baca instruksi, plan, status Git, serta
  validation command live miliknya sebelum bertindak.
- Jangan menambahkan nested repository, build output-nya, atau file Git internalnya ke parent Git.
- `docs\PAT.txt` adalah credential lokal. Jangan membaca, menampilkan, memindahkan, menghapus,
  mengedit, atau memasukkannya ke Git.
- `Terminal-v1`, `Teminal-Next`, dan `Teminal-Next-Fast` bukan dependency. Perlakukan sebagai
  retired/reference-only dan jangan dibuat ulang tanpa permintaan eksplisit.

## Preflight dan disiplin perubahan

1. Pastikan `git rev-parse --show-toplevel` menunjuk ke `C:\VSCODE\Teminal` untuk pekerjaan aplikasi
   baru.
2. Baca dokumen rencana/acceptance yang berlaku, lalu periksa `git status` sebelum mengedit.
3. Pertahankan perubahan user atau agent lain; jangan reset, restore, atau memformat area di luar
   scope.
4. Gunakan PowerShell untuk build dan validasi Windows.
5. Jangan menyimpan token, password, key, package, installer, EXE, DLL, PDB, MSI, cache runtime,
   atau file sementara di repository.
6. Jangan commit, push, membuat PR, atau menjalankan release kecuali user meminta secara eksplisit.

## Target teknologi dan runtime

- Aplikasi baru adalah C++20 raw Win32, ringan, dan standalone.
- `Termial-plan.md` §25 sudah mengunci Phase 0A/0B. Implementasi tidak boleh memilih ulang generator,
  MSVC/SDK, dependency, renderer graph, config identity/path, measurement route, IPC, feed/channel,
  package/release transport, atau uninstall-data UX. Availability/version check boleh diulang; mismatch
  harus gagal jelas, bukan memakai version terbaru yang kebetulan tersedia.
- Build adalah native Visual Studio/MSBuild `OpenTerminalNative.sln`, VS Build Tools 2022 17.14.36,
  MSVC 14.44.35207 (`cl` 19.44.35228), Windows SDK 10.0.26100.0, x64, dan C++20. Pin lengkap,
  dependency hash, serta command canonical berada di plan §23/§25.2.
- App-owned visual memakai Direct2D + DirectWrite pada satu primary surface per top-level window.
  Flip-model adalah baseline measurement-gated; bitblt `SEQUENTIAL` adalah exact fallback §25.3.
  Dialog tetap in-surface. Hanya Combo popup yang memakai owned layered popup/DIB/DC render path.
- Jangan menambahkan .NET, WinForms, WebView2, Electron, Node.js, browser engine, HTML/CSS, Tailwind,
  atau framework UI pihak ketiga tanpa keputusan baru dari user.
- .NET SDK 9.0.304 hanya build-machine dependency untuk pinned `vpk` 1.2.0; aplikasi tidak membawa
  managed runtime. Velopack native DLL ikut package output dan tidak boleh di-commit.
- First frame harus cepat. Parsing konfigurasi kecil yang diperlukan untuk menggambar boleh terjadi
  sebelum window tampil; scan, network, plugin discovery, dan pekerjaan file berat tidak boleh
  menahan first frame.
- Operasi lambat berjalan di worker dan mengirim hasil kembali ke UI thread. Worker tidak boleh
  menyentuh HWND secara langsung.

## Fresh UI, bukan migrasi

- Bangun UI runtime baru dari nol. UI lama hanya menjadi referensi behavior dan visual.
- Jangan memakai legacy `widgets.cpp`, class/header UI lama, project file lama, atau `ui.json` lama.
- Jangan membuat migration adapter, compatibility layer, atau auto-import untuk schema UI lama.
- Schema UI baru harus mempunyai identity dan version sendiri serta resource/storage path yang tidak
  dapat membaca file UI lama secara tidak sengaja.
- Identity exact adalah `yuzha.open-terminal-native.ui`; embedded default memakai
  `Assets\ui\open-terminal-native.ui.default.v1.json`/`IDR_UI_DEFAULT_JSON` 201 dan override hanya
  `%LOCALAPPDATA%\Yuzha\OpenTerminalNative\ui\override.v1.json`.
- Jangan menghapus atau mengganti implementasi lama sebagai bagian scaffold. Cutover hanya dilakukan
  setelah UI baru dan integrasi business sudah dibuktikan, dan penghapusan legacy memerlukan scope
  eksplisit.

## Konfigurasi UI JSON

- Seluruh keputusan presentasi berasal dari JSON baru: window, screen dan component tree; static
  text; layout; size; spacing; alignment; typography; color; border; thickness; radius; visual state;
  navigation binding; dan action binding.
- C++ hanya memiliki mekanisme native dan algoritma, seperti Win32 messages, HWND lifecycle, DPI,
  measure/layout, focus, keyboard, accessibility, painting, clipping, invalidation, dan cleanup.
  Jangan menyimpan keputusan visual tersembunyi atau fallback warna/ukuran di C++.
- Jika konfigurasi tidak valid, laporkan error yang jelas. Jangan diam-diam memakai style hardcoded.
- Parse, validate, merge, dan resolve token hanya saat load/reload. Paint dan layout memakai typed
  resolved structs; jangan membaca atau parse JSON di `WM_PAINT` atau hot path lain.

## UiConfigGate

`UiConfigGate` adalah satu-satunya pintu antara file JSON dan UI runtime.

Tanggung jawab:

- membaca embedded default dan override baru;
- memvalidasi schema/version serta component references;
- resolve token dan menghasilkan typed `ResolvedUiDocument`;
- menyediakan definisi window, screen, container, component, style, navigation, dan action;
- mengelola reload/config generation dan diagnostic.

Larangan:

- component dan container tidak membaca atau parse JSON langsung;
- gate tidak membuat HWND, menggambar, menangani interaction, atau menjalankan business logic.

## Component ownership

- V1 mengikuti kebutuhan native yang sudah ada: Window, Screen, Container, Text, Button beserta
  variant, Input, Combo, Checkbox, Toggle, Card, List, dan Dialog. Registry/factory harus membuat
  component baru mudah ditambahkan tanpa memperbesar dispatcher pusat.
- Setiap jenis component mempunyai directory sendiri dan satu `.cpp` utama. Di dalam `.cpp`, pisahkan
  blok lifecycle, component logic, dan component UI/measure/layout/paint secara jelas.
- Seluruh logic khusus component harus berada pada component tersebut. Contoh: Input memiliki focus,
  vertical alignment, frame repaint, selection, dan native edit behavior; Button memiliki hover,
  pressed, keyboard focus, serta paint state-nya sendiri.
- Container juga component. Container boleh memiliki logic dan UI miliknya sendiri, seperti
  background, border, padding, gap, child layout, scroll, clipping, dan invalidation.
- Jangan membuat pengganti baru untuk `widgets.cpp` yang mengumpulkan logic Button, Input, Combo,
  Checkbox, dan component lain di satu file.
- Shared primitives hanya berisi operasi teknis generik: Direct2D/DirectWrite/DXGI resource ownership,
  layered-popup DIB/DC presentation, native-peer GDI lease/cache, rounded drawing, DPI scaling, text
  measurement, rectangle math, clipping, dan invalidation union.
  Shared primitive tidak boleh bercabang berdasarkan jenis component.

## Container dan multi-window

### ApplicationContainer

Satu instance per proses dan menjadi pemilik UI shell tingkat aplikasi:

- menerima startup route dan command dari second launch/Jump List/taskbar;
- mengelola registry seluruh top-level window;
- create, find, activate, reuse, dan close window;
- menerapkan `reuse-per-route` untuk external/taskbar route pada V1: activate window route yang sudah
  ada atau buat satu jika belum ada;
- mengelola hubungan window dengan tray;
- ketika window terakhir ditutup, proses tetap hidup di tray; exit penuh hanya melalui action Exit.

`ApplicationContainer` tidak menjalankan terminal, scan Chrome, persistence, atau aturan business.

### WindowContainer

Satu instance per top-level window dan hanya mengelola UI window tersebut:

- HWND/window frame dan background;
- navigation dan active route;
- assembly component tree;
- outer layout, resize, DPI, clipping, dan child ownership;
- state UI per-window;
- meneruskan window/navigation event ke `ApplicationContainer`;
- meneruskan business action ke `UiApplicationBridge`.

Satu proses boleh mempunyai beberapa window sekaligus. Contoh: Terminal tetap terbuka saat external
route membuka Chrome Launcher dalam window terpisah.

## State ownership

- Shared process state: persisted application data, theme/config generation, cache, business service,
  terminal preferences/history, provider data, bookmark/history, serta Chrome profile data/preset.
- Per-window/component state: active route, geometry, focus, hover, pressed, scroll, navigation state,
  transient status, open dialog, dan draft yang belum disimpan.
- Tentukan ownership dari kebutuhan live setiap feature sebelum menghubungkan business. Jangan membuat
  HWND atau pointer component menjadi business state.

## UiApplicationBridge dan business boundary

- `UiApplicationBridge` adalah satu-satunya boundary UI dengan application/business layer.
- UI mengirim semantic `UiEvent` yang berisi component ID, action ID, event type, dan payload biasa.
- Application mengirim typed view state atau `UiPatch` kembali ke target component.
- Window/navigation event adalah milik `ApplicationContainer`; business action diteruskan melalui
  bridge.
- Bridge hanya routing/translation. Jangan menaruh validation, persistence, file operation, process
  launch, scan, atau aturan business di dalamnya.
- Business logic tidak boleh mengetahui JSON, HWND, GDI, paint, layout, focus, atau component internal.
- Memasang business logic tidak boleh mengubah behavior, parameter, urutan operasi, error semantics,
  async contract, atau persistence contract yang sudah disetujui.

## Urutan delivery

1. Buat schema JSON baru, `UiConfigGate`, typed document, dan diagnostic.
2. Buat native primitives, component registry, component V1, `WindowContainer`, serta
   `ApplicationContainer`.
3. Jalankan aplikasi melalui `StubApplicationBridge` dengan deterministic placeholder data dan
   action. Pada fase ini jangan scan, launch process, menulis settings, atau menjalankan business
   lama.
4. Validasi seluruh UI: state component, resize, DPI, keyboard/focus, repaint, multi-window,
   reuse-per-route, dan tray lifecycle.
5. Hubungkan business lama feature-by-feature melalui adapter dan `UiApplicationBridge` setelah
   stub UI terbukti.
6. Cutover entrypoint hanya setelah UI dan business integration terverifikasi.

## Validation

- Gunakan command canonical plan §23 setelah script tersedia; jangan mengganti generator/runner.
  Urutannya adalah toolchain check, dependency restore, Debug/Release x64 build dan test, lalu command
  measure/package/installed-update yang relevan.
- Minimum setiap phase setelah project tersedia:
  - `git diff --check`;
  - build Debug x64;
  - build Release x64;
  - test parser/schema dan component/event contracts yang relevan;
  - Windows smoke untuk behavior UI/runtime yang berubah.
- Validasi stub harus membuktikan bahwa semua screen dapat dirakit dari JSON dan setiap action/patch
  melewati contract yang benar tanpa business side effect.
- Visual PASS memerlukan aplikasi Windows yang benar-benar terlihat. Build/test saja tidak membuktikan
  layout, focus, hover, repaint, DPI, multi-window, atau tray behavior.
- Laporkan PASS, PARTIAL, atau FAIL berdasarkan checks yang benar-benar dijalankan. Sebutkan blocker
  secara tepat dan jangan memakai hasil agent sebelumnya sebagai bukti baru.
