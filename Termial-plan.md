# Terminal — Locked Architecture and Delivery Plan

Status: **spesifikasi implementasi V1 final; scaffold dan implementasi boleh langsung dimulai**

Repository canonical: `C:\VSCODE\Teminal`

Tanggal konsolidasi: 2026-08-14

Dokumen ini menggabungkan seluruh keputusan implementasi V1. Toolchain, dependency, renderer, config,
IPC, component contract, feed/channel, packaging, release, uninstall-data UX, runner CI, dan urutan
business integration tidak boleh dipilih ulang saat coding. Exact contract berada di §25. Implementasi
memakai default yang sudah ditentukan; build/test/measurement yang gagal menghasilkan bug untuk
diperbaiki pada implementasi. Measurement dipakai untuk regression dan optimasi, bukan gate untuk
memilih stack atau menahan source pertama.

## 1. Tujuan

Membangun aplikasi Terminal baru sebagai greenfield C++20 raw Win32 yang:

- ringan dan responsif berdasarkan budget startup, input-to-paint, navigation, resize, memory,
  `HWND`, serta USER/GDI handle yang diukur dari installed Release build;
- menampilkan look modern dengan antialiasing, rounded geometry, typography DPI-aware, alpha/tint,
  dan app-owned rendering tanpa menyerahkan pixel aplikasi kepada default visual Win32;
- seluruh presentasi dan assembly UI-nya dikendalikan JSON baru;
- mempunyai component native yang independen dan mudah ditambah;
- mendukung beberapa top-level window dalam satu proses;
- memisahkan UI, window lifecycle, dan business logic melalui kontrak yang jelas;
- dapat dijalankan dan divalidasi lebih dahulu memakai stub/placeholder tanpa business side effect;
- baru menerima business logic setelah UI runtime terbukti benar.

## 2. Boundary repository yang dikunci

### 2.1 Source canonical baru

- Aplikasi baru dibuat langsung di root `C:\VSCODE\Teminal`.
- Root tersebut sudah merupakan repository Git sendiri, sudah mempunyai initial commit, dan menjadi
  satu-satunya source/build domain aplikasi greenfield.
- Source, asset, project file, test, dan dokumen baru berada di root ini, bukan di dalam aplikasi lama.

### 2.2 Nested repository lama

```text
C:\VSCODE\Teminal\Open-terminal
C:\VSCODE\Teminal\Open-terminal-native
```

- Keduanya mempunyai Git sendiri, harus tetap tercantum di parent `.gitignore`, dan diabaikan parent
  Git.
- Keduanya hanya boleh dibaca secara sempit sebagai referensi behavior, visual, inventory screen,
  component, toolchain/build shape, measurement technique, serta deployment/business pattern. Pattern
  yang dipilih plan ini wajib direimplementasi di root dengan identity baru; source lama tetap tidak
  dicopy sebagai dependency.
- Aplikasi baru tidak boleh mengimpor header/source lama, me-link project lama, memanggil binary lama,
  atau menjadikannya dependency build/runtime.
- Jangan mengedit nested repository saat mengerjakan aplikasi baru.
- Jangan memasukkan `.git`, source, atau build output nested repository ke parent Git.

### 2.3 Credential dan artefak

- `docs\PAT.txt` tidak boleh dibaca, ditampilkan, dipindahkan, diubah, dihapus, atau dimasukkan Git.
- Token, password, API key, installer, package, EXE, DLL, PDB, MSI, cache runtime, dan file sementara
  tidak boleh disimpan di repository.
- Jangan commit, push, membuat PR, atau menjalankan release tanpa permintaan eksplisit user.

## 3. Greenfield, bukan migrasi

Keputusan final adalah membuat UI baru, bukan memigrasikan UI lama.

- Legacy `widgets.cpp`, class/header UI, project file, dan `ui.json` lama tidak digunakan.
- Tidak ada migration adapter, compatibility layer, atau auto-import schema lama.
- Tidak ada kewajiban mempertahankan customization UI lama.
- Schema UI baru mempunyai identity, version, embedded default, dan override path sendiri sehingga
  file UI lama tidak mungkin terbaca secara tidak sengaja.
- UI lama hanya referensi; hasil baru tidak perlu mempertahankan struktur internal atau teknik paint
  lama.
- Implementasi lama tidak dihapus pada tahap scaffold. Cutover dan penghapusan legacy adalah tahap
  terpisah setelah UI dan integrasi business baru terbukti.

## 4. Target teknologi

- Bahasa/runtime: C++20 raw Win32.
- Window lifecycle, input, native interop, dan message loop: raw Win32.
- Backend paint final V1: GDI software rendering ke top-down 32-bit BGRA DIB, satu persistent backbuffer
  per top-level window, lalu `BitBlt` ke window DC. Ini mengadopsi pola `BufferedPaint`, cached
  antialiased-corner tiles, `/MT`, dan native Edit dari `Open-terminal-native` ke object model baru.
- Shape, border, fill, clipping, text, focus, alpha/tint, dan state component dirender oleh primitives
  repository-owned. Alpha/tint pada primary opaque surface dikomposisi ke pixel BGRA terhadap resolved
  parent background sebelum present; tidak ada child-window transparency hack.
- Setiap top-level window mempunyai satu primary app-owned render surface. Dialog V1 digambar sebagai
  modal overlay pada surface parent yang sama dan tidak membuat top-level/popup `HWND` sendiri.
- Popup Combo adalah satu-satunya pengecualian V1: popup memakai owned top-level `WS_EX_LAYERED` window
  dengan premultiplied-alpha top-down 32-bit BGRA DIB dan `UpdateLayeredWindow`, sehingga rounded clip
  dan app-owned shadow tetap konsisten saat popup melewati client bounds. Layered child tidak digunakan.
- Direct2D, DirectWrite, Direct3D, DXGI, DirectComposition, GDI+, dan renderer backend alternatif berada
  di luar V1. Kegagalan visual/performance pada GDI diperbaiki dalam backend ini; tidak memicu pemilihan
  stack baru tanpa instruksi user.
- Process DPI awareness dikunci `PerMonitorV2` melalui application manifest. Semua top-level, Combo
  popup, native Edit child, layout, font, hit-test, dan render resource mengikuti DPI window aktif.
- Minimum compatibility baseline V1 adalah Windows 10 22H2 x64 build 19045. Windows 11 x64 pada build
  supported-current yang dicatat saat pengujian menjadi visual dan release-primary gate; Windows 10
  tetap menjadi compatibility gate yang wajib lulus smoke.
- Tidak memakai .NET, WinForms, WebView2, Electron, Node.js, HTML/CSS, Tailwind, atau framework UI
  pihak ketiga kecuali user membuat keputusan baru.
- Tailwind tidak dipakai. Walaupun Tailwind menghasilkan CSS statis untuk web, raw Win32 tidak
  mempunyai DOM/CSS engine; memakainya akan membutuhkan parser, cascade, selector, dan layout engine
  tambahan yang bertentangan dengan target native ringan.
- Konsep token/utility boleh diterapkan melalui structured JSON dan typed C++ tanpa CSS runtime.

## 5. Arsitektur final

```text
new embedded UI JSON + new user override
                    │
                    ▼
              UiConfigGate
       parse · validate · merge · resolve
                    │
                    ▼
            ResolvedUiDocument
                    │
                    ▼
          ApplicationContainer
           process/window lifecycle
              ┌─────┴─────┐
              ▼           ▼
      WindowContainer  WindowContainer
              │           │
              ▼           ▼
       Screen/component trees from JSON
              │
              ▼
        UiApplicationBridge
       UiEvent out · UiPatch in
              │
              ▼
   Stub first, business integration later
```

Dependency direction hanya boleh mengalir ke bawah melalui kontrak. Business layer tidak boleh
mengakses JSON, HWND, GDI, component internals, paint, focus, atau layout.

## 6. Arti “tanpa hardcode”

### 6.1 Semua keputusan UI berasal dari JSON

JSON baru menjadi sumber untuk:

- window, screen, container, dan component tree;
- component ID, urutan, parent-child, visibility, dan enabled state awal;
- static text/label dan placeholder;
- layout, size, min/max size, spacing, padding, gap, alignment, dan flow;
- typography, font reference, color, border, thickness, radius, dan background;
- visual state `normal`, `hover`, `pressed`, `focused`, `selected`, `checked`, dan `disabled`;
- navigation binding, target window, action ID, dan event binding;
- theme/token reference.

C++ tidak boleh menyimpan warna, ukuran, spacing, radius, border thickness, text, atau fallback visual
tersembunyi yang mengambil alih JSON.

### 6.2 Mekanisme native tetap berada di C++

C++ bertanggung jawab menjalankan konfigurasi:

- register window class dan membuat/menghancurkan HWND;
- menerima Win32 messages;
- focus, keyboard, clipboard, IME, accessibility, mouse, capture, dan native selection;
- menghitung measure/layout dari nilai resolved;
- DPI scaling, clipping, invalidation, GDI DIB/cache lifecycle, allocation recovery, native Edit
  interop, dan cleanup;
- hit testing serta dispatch event ke component owner tanpa mengambil alih state machine component;
- algoritma teknis seperti vertical centering dan union antara old/new dirty rectangle;
- safety checks dan kegagalan yang eksplisit.

Rumus atau algoritma boleh berada di C++; input visual dan hasil desainnya berasal dari JSON.

## 7. Schema JSON baru

### 7.1 Kontrak yang sudah dikunci

- Schema mempunyai identity baru dan `version: 1`.
- Schema minimal mengikuti kebutuhan nyata V1 dan harus dapat ditambah tanpa merusak component lama.
- Dokumen default lengkap di-embed ke executable. Source authoring dipecah menjadi
  `Assets\ui\core.json` (envelope, tokens, styles, windows) dan satu file per screen di
  `Assets\ui\screens\<routeId>.json`. `tools\Merge-UiConfig.ps1` menggabungkannya menjadi satu dokumen
  default di `build\generated\ui\terminal.ui.default.v1.json` sebelum `ResourceCompile`; runtime tetap
  menerima tepat satu embedded default dan satu override. Pemecahan ini adalah build-time authoring
  concern, bukan perubahan runtime config contract.
- Optional user override baru diterapkan di atas default baru hanya setelah seluruh override lolos
  parse, schema validation, dan reference validation. Override tidak pernah diterapkan sebagian.
- Override membawa metadata minimum binary/config-contract version memakai version convention §25.4.
  Metadata ini dipakai untuk mendeteksi override yang dibuat oleh build lebih
  baru ketika aplikasi di-rollback.
- Gate menghasilkan typed resolved document; tidak ada string token lookup atau JSON parsing di paint.
- Invalid schema/reference menghasilkan diagnostic jelas dan tidak diam-diam memakai fallback visual
  hardcoded.
- File UI lama tidak dibaca, diubah, atau dimigrasikan.

Bentuk tingkat atas yang dimaksud:

```json
{
  "schema": "yuzha.terminal.ui",
  "version": 1,
  "documentKind": "default",
  "minimumReaderContract": 1,
  "writtenBy": {
    "appVersion": "0.1.0",
    "configContract": 1
  },
  "tokens": {},
  "styles": {},
  "windows": {},
  "screens": {}
}
```

Contoh tersebut menetapkan boundary tingkat atas. Field/default seluruh component V1 dikunci di §25.10;
Phase 1 menuliskannya sebagai schema tanpa menambah pilihan baru.

### 7.2 Grammar dan aturan evolusi yang dikunci

- Dokumen menggunakan JSON UTF-8.
- Nama field memakai `lowerCamelCase`. Stable config ID dan route ID memakai `lower-kebab-case`.
- Nilai ukuran/layout numerik memakai logical pixel pada baseline 96 DPI. C++ mengubahnya ke physical
  pixel saat runtime; JSON tidak menyimpan physical pixel hasil scaling.
- Warna literal memakai `#RRGGBB` atau `#RRGGBBAA`. Preferensi utama tetap token reference. Alpha
  dipertahankan sebagai typed RGBA dan tidak boleh diterima grammar lalu diabaikan paint.
- Setelah resolve, warna typed berbentuk `ResolvedColor = LiteralRgba | SystemColorSlot`. Dark/Light
  biasanya menghasilkan `LiteralRgba`; High Contrast memakai exact object
  `{ "$systemColor": "window|windowText|grayText|highlight|highlightText" }` dan mempertahankan semantic
  `SystemColorSlot` sampai paint.
- Reference memakai object eksplisit `{ "$ref": "tokens.<path>" }`; string biasa tidak ditafsirkan
  diam-diam sebagai token.
- Unknown field, duplicate key, unknown component type, missing reference, reference cycle, salah
  type, dan nilai di luar range ditolak sebagai validation error; tidak diabaikan diam-diam.
- Embedded document dan override masing-masing dibatasi maksimal 4 MiB serta nesting maksimal 64
  level sebelum typed resolution. JSON literal non-standar `NaN`/`Infinity` ditolak parser dan seluruh
  hasil konversi numerik typed wajib lulus `std::isfinite`; limit failure menghasilkan diagnostic biasa,
  bukan crash/stack exhaustion.
- Merge override bersifat recursive untuk object. Scalar mengganti scalar dan array mengganti seluruh
  array. `null` hanya valid untuk field yang secara eksplisit nullable dan bukan operator delete umum.
- Perubahan additive yang tidak mengubah arti field lama tetap dapat berada pada schema version 1.
  Perubahan breaking terhadap grammar, required field, type, atau semantics wajib menaikkan version.
- Runtime menolak schema identity atau version yang tidak didukung.

### 7.3 Runtime contract

```text
load/reload
    → read embedded default
    → read optional new override
    → parse dan validate embedded default
    → parse dan validate override sebagai satu kesatuan
    → merge hanya bila override valid
    → resolve tokens/references
    → publish immutable/typed ResolvedUiDocument + generation

WM_PAINT/layout
    → akses typed resolved structs
    → akses cached render resources yang kompatibel dengan render context aktif
    → tidak membaca file
    → tidak parse JSON
    → tidak resolve string token
    → tidak membuat brush/font/geometry yang seharusnya dapat di-cache
```

Aturan compositing alpha V1:

- alpha di-composite pada runtime hanya di dalam surface pemiliknya;
- component yang memakai alpha terhadap background parent harus app-rendered/`HWND`-less dan digambar
  pada primary surface parent/window yang sama;
- component pemilik `HWND` boleh memakai alpha di atas background yang ia gambar sendiri;
- native child yang membutuhkan warna akhir opaque dapat memakai warna yang di-resolve terhadap
  background parent yang diketahui;
- gate menolak kombinasi alpha/native surface bila background akhir tidak dapat ditentukan. Tidak ada
  silent flatten, alpha yang diabaikan, atau pembacaan pixel parent lintas `HWND`.

## 8. UiConfigGate

`UiConfigGate` adalah satu-satunya pintu JSON menuju UI runtime.

Tanggung jawab:

- membaca embedded default dan override baru;
- parse dan validate schema/version;
- memvalidasi component, window, screen, style, token, action, dan navigation references;
- memastikan Dark, Light, dan High Contrast masing-masing resolve menjadi typed theme contract lengkap;
- memvalidasi bentuk serta kelengkapan `SystemColorSlot`, bukan membaca current RGB system color;
- memvalidasi constraint statis pemakaian alpha terhadap declared surface/ownership component. Gate
  tidak memiliki DPI atau hasil measure/layout runtime dan tidak mengklaim memvalidasi geometry akhir;
- merge default/override;
- resolve token menjadi typed values;
- menghasilkan dan mempublikasikan `ResolvedUiDocument`;
- mengelola reload/config generation dan diagnostic.

Larangan:

- tidak membuat atau menyimpan HWND;
- tidak menggambar;
- tidak menangani focus/hover/click;
- tidak menjalankan component state machine;
- tidak melakukan business operation;
- component/container tidak boleh melewati gate dan membaca JSON langsung.

Alur konsumsi konfigurasi component harus tetap satu arah:

```text
UiConfigGate
    → immutable ResolvedUiDocument
    → WindowContainer/ComponentRegistry memilih typed component definition
    → component menerima typed config untuk instance miliknya
```

Dengan demikian, "semua component terhubung melalui satu gate" berarti hanya `UiConfigGate` yang
berhubungan dengan file/schema JSON. Component tidak memegang path file, parser, atau reference ke
gate; component hanya menerima typed config yang sudah valid ketika dibuat atau ketika generation
baru dipublikasikan.

### 8.1 Kebijakan invalid config dan diagnostic

- Embedded default invalid adalah build defect. Sebelum main UI dibuat, aplikasi menampilkan
  `MessageBoxW` bootstrap yang jelas lalu keluar dengan exit code non-zero.
- Bootstrap diagnostic tersebut boleh memakai compiled string resource dan native system styling.
  Ini satu-satunya pengecualian terhadap aturan seluruh presentasi berasal dari JSON, karena JSON yang
  dibutuhkan untuk menggambar UI justru gagal di-resolve.
- Optional override invalid ditolak seluruhnya. Pada startup aplikasi memakai embedded default; pada
  reload aplikasi mempertahankan resolved document terakhir yang valid.
- Bila binary lama dijalankan setelah rollback dan metadata override membutuhkan binary/config contract
  lebih baru, override dipertahankan byte-for-byte tetapi tidak diterapkan. Aplikasi memakai embedded
  default dan diagnostic menyebut incompatibility/rollback sebagai penyebab.
- V1 tidak membuat atau memelihara `last compatible snapshot` tambahan. Mengembalikan binary ke versi
  yang kompatibel membuat override asli dapat dipakai lagi.
- Error override harus terlihat sekali secara langsung dan tetap tercatat sebagai config diagnostic
  aktif pada Settings/UI Editor sampai reload berikutnya berhasil. Jangan menerapkan sebagian override
  dan jangan menyamarkan kegagalan sebagai keberhasilan.
- Diagnostic minimal membawa file/source identity, schema path atau JSON location bila tersedia,
  error code/category, dan pesan yang dapat ditindaklanjuti tanpa membocorkan secret.
- UI Editor menulis override secara atomik: tulis temporary sibling pada data volume yang sama, flush,
  lalu replace/rename destination. File lama tetap utuh bila write/flush/replace gagal; temporary file
  dibersihkan secara best effort dan candidate baru tetap harus melewati `UiConfigGate` sebelum publish.
- Config diagnostic log berada di
  `%LOCALAPPDATA%\Yuzha\Terminal\logs\ui-config.log`. Kegagalan membuat directory, membuka,
  menulis, atau merotasi log tidak boleh menahan first frame, menggagalkan UI yang valid, atau memicu
  dialog berulang; diagnostic aktif di Settings/UI Editor tetap menjadi jalur user-facing canonical.

### 8.2 Reload V1

- Reload V1 hanya manual dan eksplisit; tidak ada file watcher.
- UI Editor menggunakan entry point reload `UiConfigGate` yang sama, bukan jalur parse khusus.
- Gate lebih dahulu menghasilkan candidate resolved generation tanpa mengganti document aktif.
- Sebelum candidate generation di-commit, setiap `WindowContainer` menormalkan transient UI state
  secara generik: tutup owned Combo popup, selesaikan active IME composition sesuai kontrak Input,
  drain modal stack dari paling dalam, lepaskan seluruh native-peer suppression scope, dan membuktikan
  suppression depth kembali nol. Cleanup tetap dijalankan bila component target hilang atau teardown
  menghasilkan error.
- V1 tidak mempertahankan Combo popup atau Dialog terbuka melewati generation swap. Setelah seluruh
  window berada pada normalized state, reload valid mempublikasikan generation baru secara atomik.
- Reconciliation mempertahankan focus, scroll, dan unsaved draft berdasarkan stable window/screen/
  component identity yang masih ada. State untuk identity yang hilang dibuang secara deterministik.
- Focus restoration hanya dilakukan setelah tree baru selesai direkonsiliasi; target yang hilang tidak
  boleh meninggalkan native peer tersembunyi, suppression token, popup, atau stale UIA provider.
- Reload invalid tidak mengganti UI aktif dan mengikuti diagnostic policy di atas.

## 9. Model component

### 9.1 Component V1

Inventory V1 mengikuti kebutuhan aplikasi native yang sudah ada, tetapi implementasinya baru:

- `Window`;
- `Screen`;
- `Container`;
- `Text`;
- `Button` dengan variant seperti default, primary, subtle, danger, navigation, dan bookmark;
- `Input` untuk single-line dan multiline native edit behavior;
- `Combo`;
- `Checkbox`;
- `Toggle`;
- `Card`;
- `List`;
- `Scrollbar`;
- `Dialog`.

Jenis/variant baru harus bisa didaftarkan melalui registry/factory tanpa membuat central widget
dispatcher membesar.

Minimum schema shape yang sudah canonical:

- setiap component membawa stable `id`, registered `type`, layout/style references, initial
  visibility/enabled state, serta typed event/navigation bindings yang relevan;
- `Button` membawa explicit `variant` dan per-state style reference untuk sedikitnya normal, hover,
  pressed, focused, selected bila variant mendukungnya, dan disabled; tidak ada state color turunan C++;
- `Screen` membawa stable `routeId` dan route/navigation binding yang dapat divalidasi gate;
- `Dialog` membawa explicit `modality` dan `dismissPolicy` untuk Escape, outside-click, serta explicit
  action. V1 hanya menerima in-surface modal behavior yang sesuai §9.6;
- exact field tambahan, type, range, default, style, dan native flags mengikuti §25.10.

### 9.2 Satu component, satu ownership

Setiap jenis component mempunyai directory sendiri dan satu `.cpp` utama. Di dalam `.cpp` terdapat
blok yang jelas:

```cpp
// Lifecycle and native window ownership

// Component logic and state transitions

// Component UI: measure, internal layout, paint, clipping, invalidation
```

Header hanya mengekspos contract yang dibutuhkan container/bridge. Logic dan UI tidak perlu menjadi
dua `.cpp` terpisah.

Tidak ada batas jumlah baris artifisial. File component boleh berukuran 800+ baris bila seluruh isinya
masih merupakan satu tanggung jawab component yang kohesif. Alasan memisahkan file adalah ownership,
bukan line count; yang dilarang adalah satu file pusat berisi logic beberapa jenis component.

Aturan ownership:

- Input memiliki native Edit child, seluruh focus/blur, native edit behavior, vertical alignment,
  selection, frame, resize invalidation, dan paint state Input.
- Button memiliki seluruh hit behavior, hover, pressed, selected, disabled, keyboard focus, capture,
  dan paint state Button meskipun Button tidak memiliki child `HWND`.
- Combo memiliki trigger, app-owned popup, dropdown list, selection, arrow, hover/focus, dismissal,
  keyboard navigation, dan paint miliknya sendiri.
- Scrollbar memiliki orientation, track/thumb measure, hover, pressed, drag capture,
  keyboard/wheel-facing scroll contract, serta paint miliknya sendiri. Scroll offset/content extent
  tetap dimiliki scroll owner seperti Container, List, atau multiline Input.
- Dialog memiliki modal panel, rounded clip, shadow, scrim, focus trap, dismissal, dan overlay paint
  miliknya sendiri meskipun dirender pada surface parent.
- Checkbox, Toggle, Card, List, Text, Screen, Window, dan Container memiliki logic/UI khususnya sendiri.
- Tidak boleh ada logic khusus Input di container, logic Button di screen, atau dispatcher besar yang
  mengetahui internal seluruh component.

### 9.3 Container adalah component

Container boleh dan harus memiliki logic UI yang memang miliknya:

- background dan border container;
- padding dan gap;
- row/column/grid/flow child layout;
- clipping, scrolling, z-order, resize, dan invalidation;
- membuat/menghancurkan child melalui registry/factory;
- membuat/memiliki `ScrollbarComponent` melalui component contract ketika overflow memerlukannya tanpa
  mengambil alih internal hit/drag/paint Scrollbar;
- melakukan spatial hit test dan meneruskan event melalui contract child tanpa mengambil alih internal
  logic/state transition child.

Container tidak menjalankan terminal, scan Chrome, persistence, atau business validation.

### 9.4 Shared native primitives

Shared primitives hanya menyediakan mekanisme generik:

- process-level GDI resource/cache ownership;
- primary-surface DIB context dan specialized layered-popup DIB context;
- antialiased rounded geometry, stroke, fill, alpha compositing, dan text drawing;
- DPI scaling;
- font/text measurement;
- rectangle math;
- clipping;
- invalidation union;
- native Edit/GDI interop yang benar-benar diperlukan;
- native helper yang tidak mengetahui jenis component.

Shared primitive tidak boleh berisi cabang `if Input`, `if Button`, atau aturan visual role tertentu.

Resource ownership yang dikunci:

- `RenderRuntime` process-level memiliki cache generic `HFONT`, `HBRUSH`, `HPEN`, temporary text-mask,
  serta opaque antialiased-corner tile berdasarkan descriptor, DPI, resolved color, radius, dan state;
- setiap top-level window mempunyai `WindowRenderContext` dengan satu screen DC saat paint, satu
  persistent compatible memory DC, satu top-down 32-bit BGRA `CreateDIBSection`, selected-bitmap guard,
  client-size metadata, invalidation union, clipping/layer state, dan mutable per-surface state;
- resize membuat DIB pengganti lebih dahulu lalu menukar resource setelah sukses. Seluruh client dirender
  ulang setelah resize/config/theme/DPI change. Paint biasa memperbarui invalid union di persistent DIB
  lalu satu `BitBlt` untuk `PAINTSTRUCT::rcPaint`; `WM_ERASEBKGND` yang ditangani tidak menghapus frame;
- corner tile mengikuti implementasi terbukti di `Open-terminal-native`: satu opaque top-down 32-bit tile
  per key, analytic edge coverage di-build terhadap final background, empat `BitBlt` untuk corner dan
  `FillRect` untuk bagian lurus. Cache maksimum 96 entry dan dibersihkan pada config/theme/DPI epoch;
- generic software `SourceOverPremultiplied` menangani RGBA/tint/scrim pada DIB primary. Hasil primary
  selalu opaque sebelum present. Text primary memakai cached `HFONT`, `SetBkMode(TRANSPARENT)`,
  `SetTextColor`, `DrawTextW`/`ExtTextOutW`, dan GDI measurement; bidi/complex runs memakai native
  Uniscribe `ScriptStringAnalyse`/`ScriptStringOut` dengan `SSA_GLYPHS`;
- `LayeredPopupRenderContext` milik Combo merender premultiplied BGRA ke compatible DIB/HDC lalu
  mempresentasikannya melalui `UpdateLayeredWindow(ULW_ALPHA)`. Shape memakai software compositor;
  glyph popup dirender ke temporary monochrome/8-bit mask memakai selected `HFONT`, lalu coverage mask
  dikomposisi ke premultiplied BGRA sehingga alpha text valid;
- seluruh `Measure` component memakai resolved metrics, cached GDI font metrics, dan geometry CPU tanpa
  membutuhkan `WindowRenderContext`; contract yang sama bekerja untuk headless test dan hidden window;
- `RenderRuntime` memiliki cache generik process-level `NativePeerGdiResourceCache` untuk physical
  `HFONT` dan `HBRUSH` yang dipakai native peer. Cache ini tidak mengetahui jenis component; component
  owner menentukan resolved descriptor lalu memegang reference-counted lease;
- key `HFONT` minimal terdiri dari resolved font descriptor dan DPI. Solid background `HBRUSH` hanya
  di-key oleh final opaque `COLORREF`; resource epoch mengubah warna yang diminta tetapi bukan identitas
  brush. Dengan demikian native peer berwarna sama dapat berbagi physical brush lintas component/epoch;
  text/background color biasa tetap berupa resolved value dan bukan GDI object;
- GDI object tidak boleh dibuat di `WM_PAINT`, `WM_CTLCOLOREDIT`, atau `WM_CTLCOLORSTATIC`. Cache
  menghapus entry dan physical object dengan zero lease setelah settle dan hanya setelah object tidak
  lagi dipakai native control/DC;
- `RenderRuntime` meregistrasikan seluruh active `WindowRenderContext` dan visible
  `LayeredPopupRenderContext`. Theme/system-color resource-epoch change meng-invalidasi semuanya; active
  popup wajib dirender dan dipresentasikan ulang, bukan menunggu close/reopen;
- config generation/theme change meng-invalidasi resource yang bergantung padanya;
- kegagalan alokasi DIB/cache membersihkan candidate resource, mempertahankan buffer lama bila masih
  valid, menampilkan inline/bootstrap diagnostic, dan tidak menjalankan retry loop tanpa batas;
- exact GDI object graph mengikuti §25.3; component tidak boleh mengetahui handle/present detail.

### 9.5 Visual contract V1 yang sudah diminta

Masalah visual lama tidak diperbaiki dengan mem-port patch `widgets.cpp`; perilaku berikut menjadi
acceptance target untuk default JSON dan component baru:

#### Input

- state unfocused memakai outline/border tipis dengan warna border normal;
- state focused memakai solid accent outline yang lebih tebal, dengan target referensi dua logical
  pixel dibanding satu logical pixel pada state normal; DPI scaling tetap dijalankan mekanisme C++;
- outline hanya tergambar sekali di control bounds, tidak berulang menjadi segmen-segmen di sebelah
  kanan, tidak bocor ke sibling/container, dan tidak meninggalkan stale border setelah focus atau
  resize;
- single-line text rata tengah secara vertikal;
- multiline text tetap rata atas;
- horizontal text alignment tetap rata kiri.
- Font family, size, weight, baseline, advance/spacing, dan perceived density antara app-painted text
  dan native Edit memakai `HFONT` descriptor yang sama. Primary surface memakai GDI text measurement/
  rendering; layered popup memakai coverage mask dari font yang sama agar premultiplied alpha valid.
- `InputComponent::Measure` menghitung minimum safe size pada DPI aktif dari radius, border, padding,
  font metrics, dan seluruh reserved app-painted region. Input menghasilkan `nativePeerContentRect`
  setelah mengecualikan border/padding, Scrollbar track, icon, clear button, atau dekorasi in-bounds lain;
  native Edit rectangle wajib seluruhnya berada di dalam opaque inner geometry dan tidak overlap dengan
  region yang digambar aplikasi.
- Gate hanya memvalidasi range/constraint statis. Geometry containment akhir adalah layout-time
  containment serta non-overlap assertion milik Input; pelanggaran menghasilkan actionable diagnostic
  dan safe non-interactive layout-error presentation, bukan crash atau silent overlap.
- Logical focus berpindah ke Input dengan memanggil `SetFocus` pada Edit child. Sebaliknya,
  `WM_SETFOCUS`/`WM_KILLFOCUS` pada Edit melaporkan perubahan kepada focus coordinator agar logical focus
  tetap sinkron. Reentrancy guard mencegah loop dan tidak ada dua sumber kebenaran focus.
- Saat window deactivate, logical focused address dipertahankan tetapi focus visual inactive; saat
  activate kembali, focus dipulihkan hanya bila target masih valid dan OS focus belum berpindah ke
  target lain. Focus ring native Edit dan app-rendered frame tidak boleh tergambar ganda.
- Saat native peer Input disuspend oleh modal overlay, Input mempertahankan value/draft, caret,
  selection, scroll, dan prior focus, menyembunyikan Edit child, lalu menggambar suspended text snapshot
  tanpa caret/selection pada primary surface agar scrim tidak memperlihatkan area Input kosong. Snapshot
  wajib memakai presentation/masking mode yang sama dan tidak boleh menggambar raw secret dari password
  input. State dipulihkan saat overlay selesai bila component identity masih valid.
- Sebelum Input disuspend oleh modal atau normalized reload, active IME composition diselesaikan dengan
  commit-result semantics (`CPS_COMPLETE`) dan candidate UI ditutup. Input baru boleh snapshot/hide peer
  setelah composition end teramati; bila completion gagal, modal/reload ditunda atau dibatalkan dengan
  diagnostic dan composition tidak pernah di-cancel diam-diam.
- `InputComponent` memiliki lease, bukan physical ownership terpisah, atas `HFONT` dan `HBRUSH` dari
  `NativePeerGdiResourceCache`. Pada theme/resource-epoch atau DPI change, Input memperoleh lease baru,
  mengirim `WM_SETFONT`, memperbarui resolved native text/background state, meng-invalidasi peer/frame,
  lalu melepas lease lama setelah Edit tidak lagi memakainya.
- `WM_CTLCOLOREDIT` hanya mengatur warna DC dan mengembalikan cached brush; Input read-only/disabled juga
  menangani jalur `WM_CTLCOLORSTATIC`. Parent message routing berdasarkan owned child `HWND` tetap
  generik dan meneruskan message ke Input tanpa memindahkan logic warna/font Input ke container.
- Modal suspend tidak melepas GDI lease karena Edit HWND masih hidup. Saat peer dihancurkan, Edit HWND
  dihancurkan sebelum final font lease dilepas. Hidden retained window boleh mempertahankan lease;
  stale theme/DPI resource wajib diperbarui sebelum peer ditampilkan kembali.
- Restore hidden window wajib berurutan: tentukan DPI aktif, hitung ulang measure/layout termasuk
  `nativePeerContentRect`, acquire GDI lease baru, kirim `WM_SETFONT` dan resolved native colors, render
  complete non-blank frame, lalu `ShowWindow`. Tidak boleh ada visible frame dengan font/warna/geometry
  generation lama.
- Hit target Input adalah seluruh bounds component, bukan hanya rectangle native Edit. Pointer pada
  padding valid memfokuskan Edit dan menempatkan caret pada posisi teks terdekat; Input owner melakukan
  translation ini tanpa memindahkan logic ke container. Sinkronisasi visible Scrollbar dengan multiline
  Edit memakai unit line/baris yang didukung native Edit, bukan mengklaim scroll per-pixel.

#### Button

- "lebih kuat" didefinisikan sebagai kontras state terhadap resolved surrounding component surface
  yang meningkat monoton `normal < hover < pressed`, bukan selalu berarti lebih gelap. Fill atau border
  yang menjadi visual boundary wajib memenuhi minimal 3:1 terhadap warna adjacent aktual; pasangan
  foreground/text ukuran normal wajib memenuhi minimal 4.5:1.
- Ramp Primary awal berbeda per theme dan seluruh nilainya tetap token JSON eksplisit:
  - Light: normal `#2563EB`, hover `#1D4ED8`, pressed `#1E40AF`, foreground `#FFFFFF`;
  - Dark: normal `#60A5FA`, hover `#93C5FD`, pressed `#BFDBFE`, foreground `#0F172A`.
  Nilai ini adalah default final V1. High Contrast memakai semantic system colors, bukan ramp ini.
- nilai hover dan pressed ditulis eksplisit serta independen pada JSON untuk setiap Button variant;
- C++ tidak menghitung hover sebagai persentase pressed atau sebaliknya;
- hover dan pressed memakai solid accent border;
- keyboard focus memakai solid focus outline, bukan dashed/dotted Win32 focus rectangle;
- nilai warna, campuran accent, ketebalan, dan radius berasal dari token/style JSON, bukan constant di
  Button C++.
- Button digambar pada primary surface sehingga alpha/tint state dapat di-composite terhadap parent
  background tanpa child-window transparency hack.

Angka lama `35%/25% accent` bukan tabel canonical. Default, subtle, danger, navigation, dan bookmark
memakai mapping exact §25.10 yang diadaptasi dari `Open-terminal-native`; implementation agent tidak
memilih warna/variant baru saat vertical slice.

### 9.6 Model HWND V1 yang dikunci

V1 memakai model satu primary app-owned surface per top-level window dengan native `HWND` hanya untuk
capability OS yang benar-benar diperlukan:

- setiap runtime component tetap merupakan object dengan stable runtime identity, bounds, state,
  measure/layout, hit-test, event, accessibility, dan paint contract miliknya;
- top-level `HWND` serta primary `WindowRenderContext` dimiliki `WindowContainer`; `WindowComponent`
  adalah definisi/composition window, bukan pemilik top-level window kedua;
- Button, Checkbox, Toggle, Text, Card, Screen, Container, dan row/item List tidak memiliki child
  `HWND`; semuanya digambar pada primary surface dan logic khususnya tetap berada di component owner;
- `ListComponent` memakai satu logical viewport, menggambar hanya row/item yang terlihat, dan dilarang
  membuat `HWND` per item;
- `InputComponent` memiliki native Edit child agar text input, selection, clipboard, IME, dan native
  text accessibility tetap tersedia; frame/outline digambar tepat sekali oleh Input pada primary
  surface, sedangkan area Edit memakai resolved final background yang opaque dan bounds-nya wajib
  berada di dalam safe opaque inner geometry;
- multiline Input tidak memakai visible native Edit scrollbar. Bila overflow/JSON config
  memerlukannya, Input memiliki `ScrollbarComponent` pada primary surface dan menyinkronkan scroll
  model ke native Edit melalui Input-owned native behavior;
- trigger `ComboComponent` digambar pada primary surface. Dropdown adalah app-owned popup `HWND`
  ber-`WS_POPUP` dengan minimum `WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` dan
  `LayeredPopupRenderContext`, bukan native `CBS_*` list, agar rounded clip, shadow, dan seluruh visual
  dropdown dikontrol aplikasi;
- Combo popup tidak masuk taskbar/Alt-Tab. Owner top-level window tetap
  active dan logical focus tetap pada Combo trigger; keyboard diterima owner lalu dirutekan ke owning
  Combo selama popup terbuka, sedangkan pointer/hit-test popup tetap dimiliki Combo;
- Combo menutup popup pada selection, Escape, outside click, owner deactivation, focus meninggalkan
  Combo scope, route/config-generation change, atau modal scope baru yang tidak memuat owning Combo;
- Combo popup memilih monitor dari anchor/owner saat dibuka, menjepit bounds ke work area monitor tujuan,
  memilih arah buka yang masih muat, dan menangani perpindahan monitor/DPI saat terbuka melalui
  recompute measure/layout serta `WM_DPICHANGED`; popup tidak boleh tersisa di luar reachable work area;
- `DialogComponent` tidak memiliki popup/top-level `HWND`. Dialog menggunakan generic modal overlay
  plane pada primary parent surface dan memiliki panel, shadow, scrim, focus trap, dismissal, serta
  paint/state dialog itu sendiri;
- `WindowContainer` memiliki generic `ModalOverlayStack`, bukan satu Boolean modal. Setiap push mendapat
  scope token; suppression menggunakan depth/refcount dan native peer hanya di-resume setelah tidak ada
  scope aktif yang masih menekannya. Menutup inner Dialog mengembalikan focus ke outer Dialog, bukan ke
  background screen;
- active overlay scope ditentukan dari stable component identity dan ancestry pada logical component
  tree, bukan relasi parent/child HWND. Native peer dan owned popup mewarisi scope component pemiliknya:
  Input/Combo di top Dialog tetap aktif, sedangkan peer di belakang scrim disuspend meskipun seluruh Edit
  HWND bersaudara di bawah top-level window yang sama;
- ketika modal overlay aktif, native peer di luar top stack scope disuspend dan child `HWND`-nya di-hide
  karena native child selalu berada di atas parent surface. WindowContainer hanya mengatur generic
  scope traversal/refcount; snapshot/state preservation tetap milik component peer;
- Combo popup di luar new top modal scope ditutup sebelum Dialog mengambil focus. Combo yang dimiliki
  component di dalam active Dialog tetap boleh membuka popup non-activating miliknya;
- WindowContainer melakukan generic hit testing/focus/event routing melalui component contract dan
  tidak berisi cabang internal Button/Input/Combo atau state machine component;
- shared window-procedure trampoline hanya meneruskan message ke instance owner dan tidak bercabang
  berdasarkan component type.

Component yang memiliki native window tetap satu component dan seluruh creation, subclassing, message
handling, internal layout, event semantics, serta destruction-nya berada di `.cpp` component tersebut.
Layered child window atau satu render target per Button/row tidak digunakan untuk mensimulasikan alpha.
Exact native Edit flags dan Combo popup Win32 styles mengikuti §25.10; kegagalan diperbaiki tanpa
mengubah surface/ownership model.

### 9.7 Accessibility dan High Contrast ownership

- Component app-rendered yang interaktif memiliki keyboard contract, visible focus, accessible name,
  role, state, action, dan UI Automation provider miliknya.
- Input muncul sebagai tepat satu logical UIA element pada posisi visual/Tab order Input, bukan wrapper
  Input dan Edit HWND sebagai dua element yang diumumkan terpisah.
- `WindowContainer` UIA fragment root mengimplementasikan `IRawElementProviderHwndOverride`.
  `GetOverrideProviderForHwnd(editHwnd)` mengembalikan `InputProvider` milik Input yang tepat dan
  mengembalikan `nullptr` untuk HWND yang tidak dioverride oleh fragment tersebut.
- `InputProvider` merupakan bagian yang dapat dinavigasi dari custom fragment tree dan
  `get_HostRawElementProvider()` mengembalikan hasil `UiaHostProviderFromHwnd(editHwnd)`. Host native
  mempertahankan Text/Value/selection behavior, sedangkan Input provider menambahkan atau mengoverride
  `Name`, `AutomationId`, label relationship, enabled/read-only state, bounds, dan logical navigation.
- `InputProvider::SetFocus` meneruskan focus ke Edit HWND. Provider/runtime identity mengikuti stable
  component identity; peer recreation/config reconciliation menaikkan structure/focus notification yang
  diperlukan dan tidak meninggalkan stale provider.
- Ketika Input native peer disuspend oleh modal, `InputProvider` tetap dialokasikan dengan identity yang
  sama tetapi dikeluarkan dari active modal fragment navigation, melaporkan disabled/non-focusable, dan
  menolak `SetFocus`. Scrim/occlusion saja tidak mengubah `IsOffscreen`; provider kembali ke posisi tree
  semula dengan structure/focus event saat resume.
- Combo popup top-level mempunyai fragment root yang di-host oleh popup HWND melalui
  `UiaHostProviderFromHwnd(popupHwnd)`. Logical parent/child dibentuk melalui bidirectional
  `IRawElementProviderFragment::Navigate`, bukan `IRawElementProviderHwndOverride`: Combo provider
  memiliki required `ExpandCollapse`, popup List provider memiliki `Selection`, dan item memiliki
  `SelectionItem`. Popup tidak muncul lagi sebagai duplicate desktop child.
- `DialogProvider` melaporkan `UIA_WindowControlTypeId`, `UIA_IsDialogPropertyId = TRUE`, serta
  `IWindowProvider::IsModal = TRUE`; Close memetakan ke semantic dismissal dan capability
  move/resize/rotate/minimize/maximize yang tidak tersedia dilaporkan false melalui required fixed
  `ITransformProvider`/`IWindowProvider` contract. Saat modal stack aktif,
  WindowContainer mengekspos hanya top Dialog scope melalui fragment navigation; background provider
  tetap hidup tetapi unreachable, disabled/non-focusable, dan tidak salah dilaporkan offscreen hanya
  karena tertutup scrim. Push/pop menghasilkan structure/focus events yang sesuai.
- List mengekspos hanya logical/virtualized item yang relevan melalui UIA tanpa membuat `HWND` per row.
- WindowContainer menyediakan generic UIA fragment root/navigation bridge; accessible semantics dan
  state tetap berasal dari provider component owner, bukan switch berdasarkan component type.
- Basic component accessibility dimiliki Phase 3A; Combo dan virtualized List patterns dimiliki Phase
  3B. Semuanya diuji dengan keyboard, accessibility inspection tool, serta Narrator smoke. Klaim
  accessibility tidak boleh diluluskan hanya dari adanya `HWND`.
- Ketika Windows High Contrast aktif, runtime memilih resolved High Contrast set tanpa reload JSON.
  High Contrast memakai semantic Windows system colors yang direferensikan config, menghindari alpha
  dekoratif, dan tidak menyampaikan state hanya melalui warna.

## 10. Screen dan composition V1

Named screen bukan business implementation. Pada stub phase, semuanya adalah composition JSON dengan
placeholder data dan action.

Inventory screen V1 yang diambil sebagai referensi dari aplikasi native saat ini:

- Terminal;
- JSON INJECT;
- JSON Editor;
- Chrome Launcher;
- Chrome Profile Manager;
- Settings;
- UI Editor.

Hierarchy referensi:

```text
Top-level navigation
├── Terminal
├── JSON INJECT
│   └── JSON Editor
├── Chrome Launcher
│   └── Chrome Profile Manager
└── Settings
    └── UI Editor
```

Aturan:

- structure, static copy, layout, component binding, dan navigation binding screen berasal dari JSON;
- Screen adalah composite UI component dan hanya mengoordinasikan UI miliknya;
- `ResolvedUiDocument` boleh memuat seluruh definisi screen, tetapi `WindowContainer` hanya merakit
  component tree route aktif pada first frame;
- route lain dirakit saat pertama kali dinavigasi lalu di-cache per window selama identity/config
  generation masih valid; inactive screen tidak dipaint atau dilayout;
- dynamic List selalu virtualized dan hanya materialize data/view row yang diperlukan viewport;
- draft/status/scroll/focus milik instance screen/window;
- screen tidak membaca storage business dan tidak menjalankan business service pada stub phase;
- JSON Editor V1 memakai native multiline Edit tanpa line number, gutter, atau syntax highlighting.
  Custom text editor dengan selection/IME/undo/scroll/render/accessibility milik aplikasi adalah
  arsitektur post-V1, bukan field tambahan kecil pada component V1;
- penambahan screen baru dilakukan melalui config + component/action registration yang eksplisit,
  bukan menambah logic ke dispatcher global.

## 11. ApplicationContainer

`ApplicationContainer` adalah container tingkat proses, satu instance per proses.

Startup contract V1:

1. `VelopackApp::Build().Run()` dipanggil tepat sekali sebagai operasi pertama di `wWinMain`, sebelum
   `RoInitialize`/COM, mutex, IPC receiver, config gate, infrastructure window, atau UI lain; fast-exit hook
   tidak boleh menjalankan startup normal.
2. Normal first instance memperoleh same-user/session mutex, membuat message-only infrastructure window
   sebagai `WM_COPYDATA` receiver, baru membuat route window. Second instance mencoba routing dengan bounded
   backoff maksimal dua detik; kegagalan menghasilkan diagnostic dan orderly non-zero exit, bukan
   diam-diam menjadi instance kedua.
3. Infrastructure handler menyalin dan memvalidasi payload selama call lalu memasukkan semantic event ke
   bounded queue; tidak ada IPC worker thread.

Tanggung jawab:

- bootstrap UI shell setelah config berhasil di-resolve;
- menerima initial route dan command dari second launch, Jump List, atau taskbar;
- memakai Terminal sebagai default route ketika startup tidak membawa route valid;
- memiliki registry seluruh top-level window;
- memiliki satu process-lifetime hidden infrastructure top-level `HWND` yang tidak pernah ditampilkan
  atau masuk taskbar; window ini bukan `HWND_MESSAGE` karena wajib menerima broadcast `TaskbarCreated`;
- memakai infrastructure window yang sama sebagai receiver tray callback, `TaskbarCreated`,
  second-launch route command, dan registered application messages;
- create, find, activate, reuse, dan close window;
- membuat satu `WindowContainer` per top-level window;
- menerapkan V1 `reuse-per-route` untuk external/taskbar route: aktifkan matching window yang sudah
  ada atau buat satu bila belum ada;
- mengelola tray lifetime;
- tetap hidup di tray setelah window terakhir ditutup ketika tray tersedia;
- keluar penuh melalui explicit Exit action, atau melalui close atas last reachable route window ketika
  tray tidak tersedia sehingga proses tidak dapat ditinggalkan tanpa UI yang dapat dijangkau.

Larangan:

- tidak menjalankan terminal atau Chrome;
- tidak scan profile;
- tidak membaca/menulis business settings;
- tidak berisi component-specific logic;
- tidak menjadi service locator untuk business logic.

### 11.1 Tray lifecycle V1

- Selain infrastructure window, registry boleh memiliki maksimal satu `retained hidden route window`.
  Ini invariant keras V1; tidak ada LRU atau cache beberapa hidden route window.
- Registry menegakkan `reuse-per-route` sebagai assertion: satu route ID maksimal mempunyai satu route
  window di gabungan visible + retained-hidden set. Matching external route selalu activate/restore,
  tidak pernah membuat visible/hidden duplicate.
- Klik kiri tray icon mengaktifkan retained hidden route window. Bila tidak ada retained window, klik
  kiri membuat window baru dengan default route Terminal.
- Klik kanan membuka native context menu yang minimal berisi daftar route yang boleh dibuka serta
  explicit Exit.
- Tray icon selalu mengirim callback ke infrastructure window, bukan ke visible/hidden route window
  yang lifecycle-nya dapat berubah.
- Destructive close memakai two-step generic `PrepareClose` lalu `CommitClose`. `PrepareClose` meminta
  setiap dirty participant memilih `Save / Discard / Cancel`: Save hanya mengizinkan close setelah
  bridge melaporkan success, Discard dicatat untuk commit, dan Cancel membatalkan operasi tanpa destroy.
  ApplicationContainer mengoordinasi window-level result tetapi tidak mengimplementasikan persistence,
  validation, atau component-specific dirty logic.
- Jika masih ada route window lain yang visible, window target hanya di-destroy setelah `PrepareClose`
  berhasil dan `CommitClose` dijalankan.
- Hide ke tray bukan destructive close. Jika belum ada retained hidden route window dan tray tersedia,
  last visible route window langsung di-hide sebagai retained meskipun dirty; active route, draft,
  scroll, focus target, geometry, dan runtime identity tetap hidup tanpa Save/Discard prompt.
- Jika retained window lama sudah ada ketika last visible window baru ditutup, window terbaru menjadi
  retained. Retained lama menjalani `PrepareClose`; bila confirmation diperlukan ia dipulihkan sementara.
  Save-success/Discard menghancurkan retained lama lalu meng-hide window terbaru. Cancel membatalkan
  seluruh replacement: window terbaru tetap visible dan retained lama kembali ke hidden state sebelumnya.
- Retained hidden window tetap tercatat pada registry dan eligible untuk `reuse-per-route`; external
  matching route me-restore instance tersebut, sedangkan route berbeda membuat visible window baru.
- Restore retained window dari tray maupun navigation memakai satu canonical path: tentukan DPI aktif,
  measure/layout ulang, acquire/update native-peer lease, kirim `WM_SETFONT`/resolved colors, render
  complete frame generation aktif, baru `ShowWindow` dan activate. Tidak ada jalur restore kedua yang
  dapat menampilkan stale/blank intermediate frame.
- `WindowRenderContext` serta resource device-dependent window hidden dilepas untuk menekan idle working
  set, lalu dibuat ulang ketika restore tanpa membuang logical UI state.
- Jika `Shell_NotifyIcon` gagal saat route window masih visible, diagnostic ditampilkan dan close atas
  last reachable route window memulai `PrepareCloseAll`, bukan ditolak atau di-hide.
- Jika tray hilang/gagal dipasang kembali ketika hanya retained hidden route window yang tersedia,
  retained window itu segera di-restore. Invariant runtime: tray unavailable selalu berarti ada route
  window visible/reachable sampai user menutupnya untuk Exit.
- Aplikasi menangani registered `TaskbarCreated` message untuk memasang kembali tray icon setelah
  Windows Explorer restart melalui infrastructure top-level window.
- Explicit Exit dan tray-failure Exit memakai `PrepareCloseAll`: retained dirty window dipulihkan
  sementara agar confirmation Dialog dapat diakses, seluruh window disiapkan tanpa menghancurkan satu
  pun, Save boleh selesai, dan keputusan Discard baru diterapkan pada commit. Cancel membatalkan seluruh
  Exit serta mengembalikan visibility state; hanya setelah semua window mengizinkan close aplikasi
  menjalankan `CommitCloseAll`, melepas tray icon, cleanup, lalu mengakhiri proses.

### 11.2 Process-global Windows state signals

- Infrastructure window adalah satu-satunya penerima process-global signal seperti
  `WM_SYSCOLORCHANGE`, relevant `WM_SETTINGCHANGE`, High Contrast/app-theme change, display topology,
  dan `TaskbarCreated`.
- Infrastructure window hanya membaca/notifikasi platform state lalu meminta `RenderRuntime` atau
  application theme state memperbarui shared resolved-resource epoch satu kali. `RenderRuntime`
  meng-invalidasi seluruh active primary dan layered-popup render context/resource domain; setiap
  `WindowContainer` serta visible Combo popup menjadwalkan redraw/re-present. Infrastructure window
  tidak menentukan palette desain.
- `WM_DPICHANGED` bukan process-global signal. Setiap top-level window dan Combo popup menanganinya pada
  owner masing-masing karena effective DPI dapat berbeda per window/monitor.
- Sebelum first frame, `ThemePlatformAdapter` membaca High Contrast sinkron melalui
  `SystemParametersInfoW(SPI_GETHIGHCONTRAST)` serta snapshot app-theme sinkron dan bounded dari
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme`; missing/error
  app-theme memakai Light sebagai safe initial fallback. Jalur ini tidak memasang subscription atau
  menahan frame untuk WinRT activation; High Contrast yang terdeteksi selalu mengalahkan Dark/Light.
- Setelah first complete frame dan UI idle, application menginisialisasi WinRT `UISettings`, membaca
  authoritative foreground/app-theme state, merekonsiliasi snapshot bila berbeda, lalu memasang
  `ColorValuesChanged`. Kegagalan `RoInitialize`, activation, query, atau subscription mempertahankan
  snapshot/fallback dan hanya menghasilkan non-blocking diagnostic.
- `ColorValuesChanged` dapat tiba pada background thread; callback hanya mem-post dedicated theme signal
  ke infrastructure window. Infrastructure window me-requery/coalesce perubahan sehingga satu perubahan
  efektif menaikkan resource epoch sekali. `ImmersiveColorSet` tidak menjadi detection contract V1.

## 12. WindowContainer

Setiap top-level native window memiliki satu `WindowContainer`.

Tanggung jawab:

- top-level HWND, frame, background, primary `WindowRenderContext`, navigation, dan content bounds window
  itu;
- lazy assembly component tree route aktif dari resolved document dan cache screen per window;
- outer layout, resize, DPI, clipping, native child lifetime, invalidation, dan repaint;
- generic spatial hit testing, focus traversal, serta event dispatch ke component contract tanpa
  mengambil alih component-specific logic;
- satu logical focus coordinator dan combined Tab order untuk app-rendered component serta native peer;
  native focus event direkonsiliasi kembali ke logical focus dengan reentrancy guard;
- generic `ModalOverlayStack`, component-ancestry scope traversal, suppression refcount, UIA active-scope
  navigation, serta `SuspendNativePeer`/`ResumeNativePeer`; Dialog tetap memiliki scrim/panel/focus trap
  dan Input tetap memiliki snapshot/state preservation;
- mengagregasi generic component dirty participants menjadi window-level `IsDirty`, `PrepareClose`, dan
  `CommitClose` tanpa membaca draft payload atau menjalankan Save/Discard business operation;
- active route dan per-window UI state;
- meneruskan same-window navigation;
- meneruskan external/new-window navigation ke `ApplicationContainer`;
- meneruskan business action ke `UiApplicationBridge`.

`WindowContainer` tidak boleh membuat window lain secara langsung; request selalu naik ke
`ApplicationContainer`.

## 13. Multi-window yang dikunci

Model final:

> Single-process, multi-window native application.

```text
Terminal process
├── top-level Window A → WindowContainer → Terminal
└── top-level Window B → WindowContainer → Chrome Launcher
```

Behavior contoh wajib:

1. Terminal sudah terbuka di Window A.
2. User memilih taskbar/Jump List `Open Chrome Launcher`.
3. Second launch meneruskan route ke proses yang sudah hidup.
4. `ApplicationContainer` tidak mengganti Terminal di Window A.
5. Jika Chrome Launcher window belum ada, buat Window B.
6. Jika sudah ada, activate Window B tanpa membuat duplikat accidental.
7. Menutup Window B tidak menutup Window A.
8. Menutup seluruh visible window tetap meninggalkan aplikasi hidup di tray.

Ini bukan multi-instance dan bukan split view. Taskbar tetap melihat satu aplikasi/proses dengan
beberapa top-level window.

`reuse-per-route` dipilih untuk V1 karena memenuhi tujuan window terpisah tanpa memperbanyak instance
route yang sama, mempertahankan state window yang sudah ada, dan memakai resource lebih sedikit.
`newWindow` pada flow ini berarti membuat window ketika belum ada matching route, bukan selalu membuat
duplikat. `allowMultiple` bukan policy V1 dan tetap menjadi kemungkinan versi berikutnya setelah ada
use case nyata.

Same-window navigation memakai aturan deterministik berikut:

- bila target route sudah aktif pada window pemanggil, hasilnya no-op tanpa rebuild component tree;
- bila target route aktif pada visible window lain, activate window tersebut dan biarkan window
  pemanggil pada route semula;
- bila target route dimiliki retained-hidden window, jalankan canonical retained restore, activate
  instance itu, kosongkan retained slot, dan biarkan window pemanggil pada route semula;
- hanya bila tidak ada matching visible/retained instance, intent same-window boleh mengganti active
  route window pemanggil sesuai navigation binding; external/taskbar intent tetap membuat window baru.

## 14. Navigation event dan business event

Dua jalur event tidak boleh dicampur:

```text
window/navigation event
    → WindowContainer
    → ApplicationContainer bila menyangkut top-level window

business action
    → component/screen emits UiEvent
    → UiApplicationBridge
    → stub atau application/business handler
```

Navigation binding berasal dari JSON dan membawa route serta target intent. Business binding membawa
semantic action ID, bukan function pointer atau HWND.

## 15. UiApplicationBridge

Bridge adalah satu-satunya boundary UI dengan application/business layer.

### UI menuju application

UI mengirim typed semantic event, secara konseptual:

```text
UiEvent
- source UiAddress
- event type
- action ID
- ordinary typed payload/value
- config generation dan operation generation bila async
```

### Application menuju UI

Application mengirim typed view state atau patch, misalnya:

```text
UiPatch/ViewState
- target UiAddress
- property/state update
- value/items/status/error
- config generation dan operation generation bila async
```

`UiAddress` wajib, bukan optional:

```text
UiAddress
- windowInstanceId
- screenInstanceId
- componentInstanceId
- stable config componentId/path untuk reconciliation dan diagnostic
```

ID dari JSON adalah stable definition identity, bukan global runtime address. Setiap instance mendapat
runtime identity sehingga component ID yang sama pada dua window atau dua list item tidak dapat saling
menerima patch. Bridge menolak patch dengan window/component instance yang sudah hilang dan async
result dengan generation yang stale.

### Larangan bridge

- tidak melakukan validation business;
- tidak menyimpan settings;
- tidak membaca file business;
- tidak launch process;
- tidak scan Chrome/WSL;
- tidak menentukan workflow bisnis;
- tidak menyimpan HWND sebagai domain state.

Bridge hanya routing dan translation antara typed contracts.

## 16. State ownership

### Shared process/application state

Ketika business integration dimulai, data berikut secara konsep shared untuk seluruh window:

- UI theme/config generation;
- persisted application settings;
- terminal preferences dan recent folder history;
- provider/Base URL/API key/model data;
- bookmarks dan URL history;
- Chrome profile cache, visible profile order, dan preset;
- shared business service, cache, dan background operation state.

### Per-window/component state

- active route dan navigation/back state;
- window geometry;
- component focus, hover, pressed, selected, dan enabled presentation;
- scroll position;
- transient status/error;
- ordered `ModalOverlayStack` beserta top active scope dan prior-focus chain;
- input atau editor draft yang belum disimpan;
- saved/baseline identity serta derived `IsDirty` milik component yang mempunyai editable draft;
- preview/draft UI Editor;
- generation yang diperlukan untuk mengabaikan async result yang sudah stale bagi window tersebut.

Prinsip penentu: data yang harus konsisten di semua window menjadi shared application state; data
interaction/draft yang hanya masuk akal untuk satu window tetap dimiliki window/component tersebut.

Dirty/close ownership yang dikunci:

- component editable memiliki draft, baseline/saved identity, dan derived `IsDirty`; component contract
  hanya mengekspos generic dirty participant/prepare-close behavior, bukan isi atau aturan business-nya;
- `WindowContainer` mengagregasi participant berdasarkan stable component identity menjadi window-level
  `IsDirty`, `PrepareClose`, dan `CommitClose` tanpa switch component type;
- Save dikirim sebagai semantic `UiEvent` melalui `UiApplicationBridge` dan baru membersihkan dirty state
  setelah success patch yang sesuai generation/identity diterima; failure mempertahankan draft dan
  menggagalkan close;
- Discard mengembalikan component ke accepted baseline hanya pada commit; Cancel tidak mengubah draft,
  baseline, retained-window selection, atau visibility state akhir;
- hide ke tray bukan close/destruction sehingga boleh mempertahankan dirty draft. Destructive close,
  retained-window replacement, dan application Exit wajib memakai prepare/commit contract §11.1.

### Theme V1 yang dikunci

- V1 menyediakan `System`, `Dark`, dan `Light`; default pertama adalah `System`.
- JSON menyediakan token/style set Dark, Light, dan High Contrast. Gate me-resolve ketiganya pada satu
  config generation dan memvalidasi bahwa setiap set menghasilkan typed semantic contract lengkap;
  raw key tidak wajib identik bila inheritance/default valid, tetapi hasil resolved wajib setara.
- `System` memilih Dark atau Light berdasarkan Windows app theme. Windows High Contrast selalu
  mengaktifkan resolved High Contrast set.
- High Contrast document mempertahankan `SystemColorSlot` symbolic. Current RGB dibaca melalui platform
  system-color API hanya ketika theme-dependent render resource dibuat ulang; perubahan system color
  mengganti resource epoch/invalidation, bukan config generation.
- C++ hanya menerima process-global OS-state notification dari infrastructure window, menyediakan
  semantic system-color mechanism, dan memilih resolved set; C++ tidak menentukan palette desain.
- Perubahan theme berlaku untuk seluruh window dalam satu config generation dan menjadi shared
  application setting ketika persistence business sudah dipasang.
- Theme switch adalah pointer/state selection + theme-dependent resource invalidation, bukan reload,
  re-parse, atau config generation baru.
- Theme change harus memperbarui native control colors, non-client/client painting yang dimiliki app,
  focus visuals, dialog, dan tray menu yang dapat dikontrol tanpa restart aplikasi.
- Standard title bar memakai SDK `DWMWA_USE_IMMERSIVE_DARK_MODE` value 20 pada locked build-19045-or-newer
  baseline. Runtime tidak mencoba undocumented value 19. Attribute diterapkan pada setiap top-level
  window setelah create/recreate dan diterapkan ulang saat effective theme berubah; kegagalan selalu
  non-fatal dan membiarkan non-client area pada default OS.
- UI Editor preview boleh menjadi per-window draft, tetapi Apply mengubah shared theme/config state.

## 17. Stub-first boundary

UI harus dapat berjalan lengkap tanpa business logic.

`StubApplicationBridge` menyediakan deterministic placeholder seperti:

- sample terminal folder;
- sample provider/model;
- sample Chrome profiles/bookmarks;
- placeholder success/error/status;
- action log atau patch UI yang dapat diprediksi.

Pada stub phase, action hanya boleh membuktikan:

- event keluar dari component dengan ID/payload benar;
- bridge menerima dan merutekannya;
- patch/view state kembali ke target yang benar;
- visual state dan enabled/disabled state berubah sesuai contract.
- generic close flow dapat mengagregasi deterministic dirty participant tanpa persistence nyata;
- stub Save menerima draft sebagai baseline in-memory dan mengembalikan deterministic success patch,
  stub Discard kembali ke baseline in-memory hanya saat commit, dan Cancel mempertahankan seluruh state
  serta membatalkan close/Exit.

Pada stub phase dilarang:

- membuka PowerShell/WSL/Chrome;
- scan Chrome atau start/resolve WSL;
- membaca atau menulis settings business;
- menyentuh provider/API key nyata;
- melakukan network request;
- menjalankan updater dari component/business stub atau plugin discovery. Packaging/update harness dan
  updater infrastructure terpisah tetap boleh menguji instalasi, perpindahan build, serta menulis
  updater-owned schedule metadata §19; carve-out ini tidak mengizinkan business settings persistence;
- memakai business implementation nested repository.

## 18. Business integration setelah UI terbukti

Business logic dipasang kemudian melalui adapter dan `UiApplicationBridge`, feature-by-feature.

Yang harus tetap sama saat dipasang:

- hasil behavior;
- input dan parameter;
- urutan operasi;
- validation;
- persistence semantics;
- async/thread contract;
- cancellation/stale-result handling;
- success/error semantics.

Yang boleh berubah hanya wiring:

```text
legacy direct control handler
    ↓
semantic UiEvent
    ↓
UiApplicationBridge/adapter
    ↓
application/business operation
    ↓
typed ViewState/UiPatch
```

Business code canonical berada di `src/logic` dan tidak mempunyai dependency build/runtime pada
folder referensi. `Open-terminal-core` dipakai satu kali sebagai migration input; source yang dibangun,
dimodifikasi, dan dimiliki aplikasi adalah copy canonical di `src/logic`.

## 19. Packaging, installer, updater, dan release V1

Packaging bukan pekerjaan opsional atau post-V1. Aplikasi harus diuji sebagai installed application
sejak executable runnable pertama tersedia dan harus mendukung perpindahan dari installed build lama
ke build baru.

Kontrak yang dikunci:

- Product display name: `Terminal`; executable: `Terminal.exe`; solution:
  `Terminal.sln`; publisher: `Yuzha`; application/package ID: `Yuzha.Terminal`.
  Package version memakai SemVer `MAJOR.MINOR.PATCH`, Win32 file version memetakan ke
  `MAJOR.MINOR.PATCH.0`, dan release tag memakai `vMAJOR.MINOR.PATCH`.
- V1 memakai dynamic Velopack C++ SDK dan CLI 1.2.0 yang dipin serta hash-verified menurut §25.2 dan
  §25.7. `vpk`/required .NET SDK hanya dependency mesin build/package dan tidak menjadi managed runtime
  dependency aplikasi.
- `Setup.exe` memakai per-user install ke `%LOCALAPPDATA%\Yuzha.Terminal`; persistent data
  root terpisah di `%LOCALAPPDATA%\Yuzha\Terminal`. Program files/current version tidak pernah
  menjadi tempat config, log, updater state, cache, draft, atau credential.
- Installer adalah jalur utama user testing; menjalankan loose EXE hanya supplemental developer smoke.
- V1 menghasilkan installer untuk clean install serta update artifact/feed metadata untuk upgrade.
- `Setup.exe` adalah full offline installer: clean install dan first launch wajib lulus ketika jaringan
  dimatikan; update feed/network diuji sebagai jalur terpisah.
- Installed build mempunyai product identity, application ID, install scope, data directory, dan
  version identity yang stabil lintas update.
- Runtime UI process V1 selalu berjalan unelevated pada normal launch dari locked per-user install.
  Operasi business yang membutuhkan administrator memakai separately elevated child/helper
  melalui explicit action; installer elevation tidak boleh diwariskan menjadi elevated app runtime.
- Single-instance/second-launch IPC harus dibatasi ke same-user/session dan diverifikasi terhadap
  install/privilege model. V1 tidak melonggarkan UIPI message filter untuk menerima arbitrary
  lower-integrity window message. Manual elevated launch dengan linked/split normal token ditolak dengan
  diagnostic/relaunch unelevated; bila token tidak mempunyai linked counterpart (misalnya UAC disabled
  atau built-in Administrator), aplikasi boleh berjalan dengan diagnostic dan cross-integrity routing/
  helper feature dinonaktifkan, bukan hard-exit tanpa usable path.
- Update `N → N+1` mengganti program files secara aman tanpa menghapus atau menimpa user UI override,
  settings, cache, bookmark/history, draft yang memang persisted, atau credential storage.
- Rollback binary tidak mengubah atau mengganti override yang dibuat oleh build lebih baru. Binary lama
  menolak override incompatible, memakai embedded default, dan menampilkan diagnostic rollback; V1 tidak
  membuat persistence `last compatible snapshot` tambahan.
- File runtime milik versi berbeda tidak boleh bercampur. Update gagal atau dibatalkan harus
  meninggalkan versi lama tetap dapat dijalankan.
- Package/update metadata harus diverifikasi sebelum binary diganti. Exact Velopack integrity dan
  unsigned-V1 policy preview/public mengikuti §25.7.
- Update check, download, dan staging tidak boleh menahan first frame atau berjalan di UI thread.
- Updater coordination berada di application/deployment service dan hanya mengirim status/action
  semantic melalui `UiApplicationBridge`; component dan container tidak mengelola file update.
- Manual `Check for updates` selalu tersedia. Automatic check dilakukan maksimal sekali per 24 jam,
  hanya setelah first complete frame dan idle; check tidak mengunduh otomatis. Download serta
  restart/apply membutuhkan persetujuan eksplisit user dan apply wajib melewati `PrepareCloseAll`.
- Jadwal memakai updater-owned
  `%LOCALAPPDATA%\Yuzha\Terminal\updater\state.json` dengan `lastAttemptUtc` dan
  `lastSuccessfulCheckUtc`, ditulis atomik. Metadata ini deployment state, bukan business settings:
  Phase 5 boleh menulisnya melalui updater infrastructure/harness meskipun component/business stub
  dilarang melakukan persistence. Kegagalan membaca/menulis state tidak memblokir first frame, manual
  check, close, atau normal use dan hanya menghasilkan non-blocking diagnostic.
- V1 mempertahankan satu previous full package yang dapat dipilih untuk explicit downgrade serta satu
  staged file dari attempt terakhir. Staged file dibersihkan setelah apply sukses atau failure final
  dikonfirmasi. Automatic crash-loop rollback adalah post-V1; apply gagal wajib meninggalkan current
  installed version dapat dijalankan.
- Installer/update wajib mempertahankan single-process routing, taskbar/Jump List identity, tray icon,
  shortcut, dan multi-window behavior setelah update.
- Uninstall membersihkan program files dan integration milik aplikasi. Kebijakan mempertahankan atau
  menghapus user data harus menjadi pilihan eksplisit, bukan penghapusan diam-diam.
- Installer, package, update bundle, symbols, dan generated feed metadata adalah build/release artifact
  dan tidak dimasukkan ke source Git.
- Workflow release harus dapat membangun versioned Release x64, installer, update artifact, checksums/
  manifest, dan release notes yang sesuai version tersebut.
- Membangun serta menguji artifact berada dalam scope plan. Mempublikasikan release/update ke user
  tetap memerlukan permintaan eksplisit user pada turn pelaksanaan.

Exact local/production feed, channel, preview/public integrity, artifact command, unsigned V1 policy,
dan uninstall-user-data UX dikunci di §25.7-§25.9. Tidak ada secret/certificate blocker untuk V1.

## 20. Target source ownership dan nama final

Konvensi nama dikunci: file memakai `snake_case`, class/type memakai `PascalCase`, dan setiap class
boleh mempunyai header contract serta tepat satu `.cpp` implementation utama. Nama inti final:

| Tanggung jawab | File | Class/type |
| --- | --- | --- |
| process-level UI lifecycle | `src/application/application_container.{h,cpp}` | `ApplicationContainer` |
| process-global hidden message receiver | `src/application/application_infrastructure_window.{h,cpp}` | `ApplicationInfrastructureWindow` |
| UI/business boundary | `src/application/ui_application_bridge.{h,cpp}` | `UiApplicationBridge` |
| deterministic test application | `src/application/stub_application_bridge.{h,cpp}` | `StubApplicationBridge` |
| satu-satunya JSON gate | `src/ui/config/ui_config_gate.{h,cpp}` | `UiConfigGate` |
| immutable typed UI config | `src/ui/config/resolved_ui_document.{h,cpp}` | `ResolvedUiDocument` |
| process GDI cache/context registry owner | `src/ui/rendering/render_runtime.{h,cpp}` | `RenderRuntime` |
| process cache untuk physical native-peer GDI resource | `src/ui/rendering/native_peer_gdi_resource_cache.{h,cpp}` | `NativePeerGdiResourceCache` |
| per-window/surface render context | `src/ui/rendering/window_render_context.{h,cpp}` | `WindowRenderContext` |
| Combo layered-popup render/present path | `src/ui/rendering/layered_popup_render_context.{h,cpp}` | `LayeredPopupRenderContext` |
| one top-level window owner | `src/ui/container/window_container.{h,cpp}` | `WindowContainer` |
| component creation/registration | `src/ui/registry/component_registry.{h,cpp}` | `ComponentRegistry` |

Nama component final mengikuti pola `<name>_component.{h,cpp}` dan `<Name>Component`: `WindowComponent`,
`ScreenComponent`, `ContainerComponent`, `TextComponent`, `ButtonComponent`, `InputComponent`,
`ComboComponent`, `CheckboxComponent`, `ToggleComponent`, `CardComponent`, `ListComponent`,
`ScrollbarComponent`, dan `DialogComponent`.

Struktur target:

```text
C:\VSCODE\Teminal\
├── Assets\
│   └── ui\                         # core.json + screens\<routeId>.json (build-time merged)
├── packaging\                     # Velopack manifests and release inputs
├── src\
│   ├── main.cpp                    # minimal process bootstrap
│   ├── application\
│   │   ├── application_container.*
│   │   ├── application_infrastructure_window.*
│   │   ├── ui_application_bridge.*
│   │   └── stub_application_bridge.*
│   └── ui\
│       ├── config\
│       │   ├── ui_config_gate.*
│       │   └── resolved_ui_document.*
│       ├── rendering\
│       │   ├── render_runtime.*
│       │   ├── native_peer_gdi_resource_cache.*
│       │   ├── window_render_context.*
│       │   └── layered_popup_render_context.*
│       ├── container\
│       │   └── window_container.*
│       ├── registry\
│       │   └── component_registry.*
│       ├── primitives\
│       │   └── <technical primitive by responsibility>.*
│       └── components\
│           ├── window\window_component.*
│           ├── screen\screen_component.*
│           ├── container\container_component.*
│           ├── text\text_component.*
│           ├── button\button_component.*
│           ├── input\input_component.*
│           ├── combo\combo_component.*
│           ├── checkbox\checkbox_component.*
│           ├── toggle\toggle_component.*
│           ├── card\card_component.*
│           ├── list\list_component.*
│           ├── scrollbar\scrollbar_component.*
│           └── dialog\dialog_component.*
└── tests\
```

Ini menetapkan ownership dan penamaan final untuk V1. Nama interface/helper teknis tambahan ditentukan
hanya ketika kontraknya nyata. Jangan membuat directory kosong sebelum phase yang benar-benar
memerlukannya, dan jangan membuat framework abstraksi spekulatif.

## 21. Urutan implementasi

### Phase 0 — Scaffold dan runnable shell

#### Phase 0A — Scaffold langsung

1. Verifikasi root Git dan dirty worktree.
2. Baca plan ini; root tidak memerlukan `AGENTS.md` atau implementation-plan kedua.
3. Scaffold solution/project/scripts §25.2 dengan pola MSBuild `/MT /W4` dari
   `Open-terminal-native`; jangan mengimpor project/source lama.
4. Terapkan identity/version/data-root/config resource §19/§25.4 dan manifest `PerMonitorV2`.
5. Implement GDI object graph final §9.4/§25.3, test runner §25.2, instrumentation §25.5, dan IPC
   `WM_COPYDATA` §25.6.
6. Build Debug/Release dan perbaiki error yang ditemukan. Toolchain mismatch memberi diagnostic tetapi
   tidak melarang source/scaffold dibuat.

Exit criteria Phase 0A: solution membangun executable shell yang dapat membuat primary GDI surface,
memuat embedded config, menerima second launch, dan keluar orderly. Release workflow bukan blocker source.

#### Phase 0B — Packaging integration

1. Integrasikan exact Velopack C++ 1.2.0 per-user contract §19/§25.7, termasuk startup hook sebelum
   mutex/config/renderer dan `SetAutoApplyOnStartup(false)`.
2. Implement exact local preview feed/channel, version flags, integrity gate, artifact command, dan
   installed-update automation §25.7-§25.8.
3. Implement production GitHub Release unsigned-V1 contract §25.7-§25.8 dan SHA-256 release summary.
4. Implement uninstall-data UX §25.9 serta locked manual-elevation behavior §19.

Exit criteria Phase 0B: packaging route dan script `N → N+1` tersedia. Kegagalan preview dicatat dan
diperbaiki tanpa membuka kembali pemilihan packaging stack.

### Phase 1 — Schema dan gate

1. Implement seluruh schema component/default §25.10; vertical slice hanya menentukan urutan coding,
   bukan menambah atau memilih field baru.
2. Buat new embedded default document dan optional new override identity/path.
3. Implement parse, validation, reference checking, token resolution, merge, diagnostic, dan typed
   `ResolvedUiDocument`.
4. Implement typed `ResolvedColor` (`LiteralRgba | SystemColorSlot`), constraint statis surface/alpha,
   complete Dark/Light/High Contrast resolution, dan runtime theme selection tanpa re-parse.
5. Implement whole-override rejection, last-known-good reload, bootstrap failure, manual reload, dan
   rollback-incompatible override fallback tanpa membuat compatible snapshot tambahan.
6. Bekukan dan uji `ThemePlatformAdapter`: bounded synchronous initial snapshot, post-first-frame
   `UISettings` reconciliation/subscription, background-callback-to-infrastructure-window dispatch,
   semantic High Contrast slot materialization, fallback, dan coalesced resource-epoch behavior tanpa
   menjadikan current system RGB bagian config generation.
7. Tambahkan tests untuk valid, invalid, duplicate/unknown field, missing/cyclic reference, merge/
   replacement semantics, version, override rejection, reload, dan no-legacy-import.

Exit criteria: schema dapat di-resolve tanpa HWND dan tidak pernah membaca legacy `ui.json`.

### Phase 2 — Native primitives dan vertical component slice

1. Implement final GDI primary DIB/BitBlt path §25.3; tidak ada renderer probe atau backend fallback.
2. Lengkapi `RenderRuntime`, `NativePeerGdiResourceCache`, `WindowRenderContext`, cache/lease counter,
   cached AA corner tile, software source-over, GDI text/measurement, clipping, invalidation, resize, dan
   allocation-failure path. Seluruh measure primitive/component dapat diuji tanpa render context.
3. Buat component contract, registry/factory, dan minimal vertical-slice `WindowContainer` host yang
   memiliki logical focus coordinator, generic overlay plane, native-peer traversal, dan one-surface
   dispatch contract; routing/multi-window lifecycle lengkap tetap Phase 4.
4. Implement `Window`, `Container`, `Text`, HWND-less `Button`, dan native-Edit-backed `Input` sebagai
   vertical slice pertama pada satu primary surface.
5. Gunakan shared `HFONT` descriptor dan GDI measurement/rendering untuk app text/native Edit pada semua
   DPI. Visual mismatch diperbaiki pada metrics/padding/font descriptor, bukan dengan memilih backend.
6. Implement dan uji combined logical/native focus coordinator, two-way Edit focus sync, activation
   restore, Input `nativePeerContentRect` containment/non-overlap assertion dengan synthetic reserved
   app-painted regions, IME commit/candidate-close sebelum suspend, serta native-peer suspend/resume hook
   lengkap dengan suspended text snapshot dan restoration state. Synthetic overlay dapat memanggil hook
   sebelum DialogComponent tersedia.
7. Implement Input-owned GDI resource lease, cached `WM_CTLCOLOREDIT`/`WM_CTLCOLORSTATIC` path, atomic
   font/brush replacement pada theme/DPI change, serta destroy-order yang tidak melepaskan resource saat
   masih dipakai Edit HWND. Implement zero-lease cache eviction dan restore order DPI/layout → lease →
   `WM_SETFONT`/colors → complete frame → `ShowWindow`.
8. Tambahkan generic editable-component dirty participant contract; Input vertical slice membuktikan
   baseline, derived `IsDirty`, Save-success/failure patch, staged Discard, dan Cancel tanpa memasukkan
   persistence/business logic ke component atau container.
9. Pastikan first complete frame dirender ke presentation buffer sebelum top-level window ditampilkan;
   window class tidak memakai default white client brush dan handled `WM_ERASEBKGND` tidak menghapus
   complete app-owned frame.
10. Jalankan executable placeholder dari JSON untuk memeriksa create/layout/paint/alpha/event/patch,
   no-blank-frame, focus synchronization, suppression hook, dan resource recreation.
11. Jalankan measurement harness sebagai regression report. Hasil di atas target menjadi backlog/fix
   terukur dan tidak menahan vertical slice berikutnya kecuali aplikasi crash atau tidak usable.
12. Selesaikan Phase 0B, buat installer preview pertama, dan jalankan smoke dari installed path, bukan
   hanya build tree.

Exit criteria: installed test app tampil dari JSON, tidak memiliki hidden visual constant atau
business side effect, dan dapat di-uninstall tanpa merusak user data di luar scope aplikasi.

### Phase 3A — Component dasar, Scrollbar, dan basic UIA

1. Tambahkan HWND-less Checkbox, Toggle, Card, Screen, dan Scrollbar.
2. Integrasikan Scrollbar sebagai owned component contract pada scrollable Container dan multiline
   Input; native visible scrollbar tidak digunakan untuk app-owned scroll surface. Integrasi virtualized
   List diselesaikan pada Phase 3B. Verifikasi native Edit rect mengecualikan Scrollbar track dan seluruh
   app-painted decoration pada minimum/normal size serta seluruh accepted DPI.
3. Implement UI Automation provider untuk Button, Checkbox, Toggle, dan Scrollbar. Untuk Input,
   implement `IRawElementProviderHwndOverride`, Input provider yang berada pada fragment navigation
   order, serta native Edit host-provider delegation tanpa duplicate logical element.
4. Lengkapi token seluruh Button variant per theme. Jalankan gerbang objektif text 4.5:1 dan visual
   boundary/focus 3:1 terhadap adjacent resolved color, lalu screenshot enam variant × normal/hover/
   pressed/focused/disabled pada Dark dan Light. Kandidat yang gagal direvisi agent sebelum phase lulus.
5. Uji Inspect Raw/Control/Content view, previous/next visual order, Name/label, AutomationId,
   Text/Value/selection, read-only/password, SetFocus, peer recreate, config reconciliation, dan Narrator
   duplicate-announcement khusus Input.
6. Uji visual state, drag/wheel/keyboard scroll, UIA/Narrator, High Contrast, DPI, resize, repaint, dan
   cleanup sesuai capability masing-masing.

Exit criteria Phase 3A: component dasar dapat diregistrasikan lokal, Scrollbar tidak bergantung pada
native themed control, dan basic UIA/keyboard smoke lulus tanpa central widget dispatcher.

### Phase 3B — Combo popup, Dialog overlay, List virtualization, dan advanced UIA

1. Implement Combo dengan specialized `LayeredPopupRenderContext`, premultiplied-alpha DIB/HDC
   presentation melalui `UpdateLayeredWindow`, non-activating/no-taskbar popup, owner-side keyboard
   routing, pointer/outside-click handling, seluruh dismissal trigger, dan theme/system-color epoch
   redraw/re-present; primitive/component contract memakai GDI DIB §25.3.
2. Implement in-surface Dialog overlay memakai generic `ModalOverlayStack`; verifikasi nested
   push/pop, component-ancestry scope, suppression refcount, Input dalam/luar Dialog, Combo popup dalam
   active Dialog, scrim, rounded panel, shadow, focus trap, dismissal, IME completion,
   native-peer snapshot, dan exact restoration.
3. Implement virtualized List memakai ScrollbarComponent, visible-row realization, selection, scroll,
   dan cleanup tanpa `HWND` per row.
4. Implement UIA `ItemContainer`, `VirtualizedItem`, Selection, Scroll, dan ScrollItem contract untuk
   List; Combo `ExpandCollapse`, popup-hosted/reparented List `Selection`, item `SelectionItem`; serta
   Dialog Window/IsDialog/IsModal, active-scope navigation, disabled background, structure/focus event,
   dan suspended-provider identity contract.
5. Ukur full-surface update/copy cost layered popup pada deterministic maximum V1 item/size terhadap
   input-to-paint budget; jangan menyebutnya murah tanpa measurement.
6. Implement reusable Save/Discard/Cancel confirmation Dialog dan generic result contract yang akan
   dipakai Phase 4 close coordinator. Phase 4 tidak boleh mengganti confirmation ini dengan native
   message box atau component-specific close UI.

Exit criteria Phase 3B: Combo/Dialog/List telah terintegrasi dan smoke advanced UIA/visual dijalankan.
Temuan diperbaiki iteratif; ia tidak membuka renderer atau component contract baru.

### Phase 4 — WindowContainer, ApplicationContainer, multi-window, dan tray

1. Lazy-assemble hanya route aktif, lalu cache screen per window; inactive route tidak dilayout/dipaint.
2. Implement per-window route/state, combined focus coordinator, `ModalOverlayStack`, logical
   component-scope/native-peer traversal, reload normalization, serta window-level dirty aggregation di
   `WindowContainer`.
3. Implement `ApplicationInfrastructureWindow`, process window registry, process-global OS-state
   signal fan-out, tray callback, `TaskbarCreated`, dan second-launch routing di `ApplicationContainer`.
4. Implement per-window/popup `WM_DPICHANGED`; jangan fan-out DPI sebagai process-global state.
5. Implement `reuse-per-route` untuk external/taskbar command dengan registry assertion bahwa route ID
   tidak pernah mempunyai visible/hidden duplicate; implement same-window no-op, activate visible match,
   serta canonical restore retained match yang mengosongkan retained slot tanpa mengganti route pemanggil.
6. Implement `PrepareClose`/`CommitClose`, `PrepareCloseAll`/`CommitCloseAll`, destructive
   close-one-window, non-destructive hide dirty window, newest-window retained replacement, maksimal satu
   retained hidden route window, release/recreate hidden render resources, hidden-window route reuse,
   tray-failure restore/fallback Exit, dan explicit Exit. Confirmation wajib memakai Dialog Phase 3B.

Exit criteria: Terminal dapat tetap terbuka ketika Chrome Launcher muncul di top-level window kedua;
tidak ada accidental duplicate untuk external route; newest retained/Cancel/Exit behavior lulus tanpa
data loss atau window yang tidak dapat dijangkau.

### Phase 5 — Stub application dan installed-update validation gate

1. **DONE 2026-08-15:** lengkapi semua placeholder screen dan deterministic data.
2. **DONE 2026-08-15:** validasi setiap navigation binding, UiEvent, bridge route, dan
   UiPatch/ViewState.
3. **DONE 2026-08-15 (scope Phase 5: automated/current-machine smoke sebagai validation
   gate):** `tools\Smoke-Runtime.ps1` 48 check PASS pada Windows 10 19045 / 100% DPI,
   termasuk check theme/list/dialog yang benar-benar melakukan assertion: tujuh route
   no-blank diukur pada client area (PrintWindow, kebal overlay aplikasi lain) + clean
   exit; multi-window + second-launch IPC + no-duplicate-route; retained hidden window,
   restore via matching route, infrastructure window; USER/GDI counter steady dan
   idle-stable; resize repaint no-blank; Tab focus, Combo popup buka F4/tutup Escape
   (deteksi popup via EnumWindows karena FindWindow/FindWindowEx terbukti tidak reliably
   di mesin ini), popup tetap visible dan dirender ulang saat theme switch ketika terbuka,
   popup open/close cycle tanpa USER/GDI growth; List keyboard scroll dibandingkan
   setelah focus sudah berada pada List dan wajib mengubah lebih dari 500 sampled pixel
   client; Dialog buka (overlay client berubah besar) dan tutup Escape (overlay
   hilang) diverifikasi via diff client area; Dark/Light live switch default-run
   (luminance client 243.3 vs 40.4) dengan restore theme terjamin try/finally (value
   registry dihapus bila tadinya tidak ada). Harness memakai BringToFront
   (AttachThreadInput) karena proses lain dapat memegang foreground. Headless contract
   tests menambah DPI transition (window/popup), UIA patterns (Dialog/List/modal scope),
   nested modal suppression, focus traversal, virtualization, reload reconciliation,
   close transaction, dan ThemePlatformAdapter. Matrix manual/lingkungan berikut
   **deferred to Phase 7** (bukan PASS): DPI 125/150/200% (butuh perubahan system
   scale/sign-out atau monitor kedua), Windows 11 release-primary matrix (mesin saat ini
   hanya Windows 10), Narrator audio (interaktif), IME composition interaktif termasuk
   reload saat composition aktif (butuh IME live), klik tray icon sintetis (butuh
   interaksi shell nyata), dan High Contrast live re-run berulang (toggle HC mematikan
   console agent di mesin ini; jalur HC tetap tersedia via `-ThemeMatrix`).
4. **DONE 2026-08-15 (scope Phase 5):** package preview `0.1.0` dan `0.1.1` dihasilkan
   (`tools\Build-Package.ps1`, channel `win-preview`, delta + feed + SHA256SUMS lulus).
   Clean install `0.1.0` dan first launch lulus (window terlihat, exit 0). Update installed
   `0.1.0 → 0.1.1` lulus melalui local preview feed: `result.json`
   `artifacts\installed-update-results\0.1.0-to-0.1.1` `passed=true`. Sandbox Windows
   menolak run karena image 19041 < 19045 — penolakan itu sendiri membuktikan runtime
   gate build; run final memakai CurrentUser dengan cleanup uninstall. Verifikasi literal
   jaringan dimatikan (network-disabled clean install/first launch) **deferred to Phase 7**
   karena membutuhkan admin/physical disconnect di luar privilege sesi ini; bukti pendukung
   saat ini: Setup memakai embedded bundle penuh tanpa download (log Velopack) dan startup
   path tidak memanggil network API apa pun. Bukti lama tidak boleh dikutip ulang bila
   source final berubah tanpa re-run.
5. **DONE 2026-08-15:** file/product version berubah `0.1.0.0 → 0.1.1.0`, aplikasi
   launchable setelah update, sentinel data di `%LOCALAPPDATA%\Yuzha\Terminal` bertahan
   (`dataPreserved=true`), single instance dan shortcut diverifikasi, uninstall menghapus
   program files tetapi mempertahankan data root.
6. **DEFERRED 2026-08-15 (non-blocking, moved to Phase 6 item 7):** yang sudah tervalidasi
   dalam Phase 5: default uninstall preserve-data, shortcut/taskbar identity, update staging
   Velopack, dan `--update-now` CLI. Sisa item — scheduled check 24 jam setelah frame+idle,
   no-auto-download, consent download/restart UI, atomic `updater\state.json`, retained
   package/staged-file cleanup, `PrepareCloseAll` sebelum apply, rollback previous package,
   update-while-running end-to-end, dan tray setelah Explorer restart — bergantung pada updater
   application service dan Deployment UI yang baru diimplementasikan pada Phase 6 item 7;
   validasinya mengikuti Phase 6 item 7 lalu release validation Phase 7. Item ini tidak
   dihitung DONE pada Phase 5.
7. **DEFERRED 2026-08-15 (non-blocking, moved to Phase 7):** bukti pendahuluan
   (development measurement, installed 0.1.0, 10 sample): cold
   first visible frame p95 `64.3 ms` (target 250); layered popup full-surface p95
   `10.8 ms` (target 33); first route visible `44 ms`; route assembly pertama `117 ms`
   termasuk overhead process/IPC harness (target app-owned 100); idle private commit
   `5.2 MiB` (target 64), working set diagnostic `18.5 MiB`; 100 cycle
   close→retained→restore `0` failure dengan USER `89→89`, GDI `17→17`, satu top-level
   (`tools\Smoke-Runtime.ps1 -CycleOnly`). Pengukuran berprivilege — ETL/`wpr.exe` trace,
   input-to-paint live-app, resize frame timing, dan TraceLogging breakdown — **deferred
   to Phase 7** karena membutuhkan admin pada mesin ini; harness dan marker sudah
   tersedia. Release candidate tetap wajib 30 sample.
8. **DONE 2026-08-15:** manifest `asInvoker`; token process installed terverifikasi
   `elevated=False`; second-launch routing same-user lulus pada installed build;
   tidak ada `ChangeWindowMessageFilterEx` di source; elevated business helper baru
   relevan pada Phase 6 Terminal launcher.
9. **DONE 2026-08-15 (untuk snapshot Phase 5):** build Phase 5 adalah stub murni — tidak
   ada API process-launch/network/shell aktif di `src`, tidak ada business side effect,
   dan tidak ada dependency ke nested reference repository pada source/project file; tidak
   ada secret, binary, atau artifact ter-track; `.gitignore` mencakup build/package/log.
   Pekerjaan Phase 6 (misalnya TerminalLauncher) tidak dihitung dalam audit item ini sampai
   phase-nya sendiri. Temuan pre-existing: 42 file `Open-terminal-core` ter-track root
   sejak commit sebelum plan ini; tetap hanya referensi tanpa dependency build; cleanup
   menunggu otorisasi legacy.

Penutupan Phase 5: automated/current-machine smoke diterima sebagai validation gate Phase 5.
Verdict Phase 5: **PASS untuk scope Phase 5** — exit criteria runtime (build Debug/Release,
seluruh contract tests, visible runtime smoke current-machine, package preview, clean install,
dan installed update `N → N+1`) benar-benar lulus pada snapshot ini. Item 6 dan 7 yang belum
tersedia tanpa business integration/release environment berstatus DEFERRED non-blocking dan
dipindahkan secara eksplisit ke Phase 6 item 7 (updater application service + Deployment UI)
serta Phase 7 (pengukuran berprivilege dan release candidate 30 sample). Pemeriksaan yang
dipindahkan tidak dihitung lulus di sini dan tetap wajib pada phase tujuan.

Exit criteria: UI runtime dinyatakan PASS hanya jika build, contract tests, visible Windows smoke,
clean install, dan `N → N+1` installed update lulus. Jika visible/install/update smoke tidak tersedia,
verdict maksimal PARTIAL.

### Phase 6 — Business integration, terpisah

Phase ini dimulai hanya setelah stub UI disetujui user.

Urutan final mengikuti dependency dan implementasi yang sudah bekerja di dua repository referensi:

1. Terminal launcher: PowerShell Admin, PowerShell, dan Ubuntu/WSL dengan selected folder.
2. Settings/storage: theme, terminal preferences, recent folders, startup-to-tray, dan persistence.
3. Chrome Launcher: cached Windows/WSL profile metadata, bookmark/URL state, launch, lalu Profile Manager.
4. JSON INJECT: provider/config selection, Windows/WSL target, validation, backup, dan atomic write.
5. JSON Editor: open, edit, dirty state, validate, backup/restore, save, dan error handling.
6. UI Editor: edit draft, preview, apply/save, token preservation, reload, dan rollback diagnostic.
7. Deployment UI: manual/scheduled update state, download consent, apply/restart, dan uninstall choice.

Untuk setiap nomor: raw logic yang relevan berada di `src/logic/features`, facade typed berada di
`src/logic/application`, dan adapter plug-and-play berada di `src/application/adapters`. Setiap action
JSON tetap mempunyai fallback stub; adapter nyata memasangnya melalui `ReplaceAction` tanpa branch
feature di component atau `WindowContainer`. Aplikasi tidak membangun source dari folder referensi.

### Phase 7 — Release readiness, cutover, dan legacy retirement

1. Jalankan full Release x64 build, package, clean-install, upgrade dari accepted build sebelumnya,
   uninstall, checksum/integrity, version, shortcut/taskbar/tray, dan release-note validation.
2. Jadikan UI baru entrypoint canonical setelah seluruh integration yang disetujui lulus.
3. Pastikan release/update artifact dapat dipublikasikan melalui workflow yang sudah dipilih; actual
   publication tetap menunggu instruksi eksplisit user.
4. Penghapusan legacy code/reference hanya dengan otorisasi eksplisit dan cleanup plan tersendiri.
5. **Deferred dari penutupan Phase 5 (belum PASS; jalankan dan catat di sini):**
   - DPI 125%, 150%, 200% dan PerMonitorV2 pada perubahan scale nyata (butuh perubahan
     system scale/sign-out atau monitor kedua);
   - Windows 11 x64 supported-current release-primary matrix (mesin Phase 5 hanya Windows 10);
   - Narrator audio dan accessibility inspection interaktif;
   - IME composition interaktif, termasuk reload ketika composition aktif;
   - klik tray icon dan tray menu nyata, termasuk tray setelah Explorer restart;
   - High Contrast live switch berulang pada sesi interaktif;
   - ETL/`wpr.exe` trace berprivilege: input-to-paint live-app, resize frame timing, dan
     TraceLogging breakdown startup; release candidate 30 sample;
   - clean install dan first launch literal dengan jaringan dimatikan.
   Setiap check dicatat hasilnya (PASS/FAIL/blocked) tanpa mengklaim lulus sebelumnya.

## 22. Acceptance criteria global

### Repository

- Semua source aplikasi baru berada di root repository.
- Nested repositories tetap bersih dan tidak menjadi dependency.
- Tidak ada credential atau binary build di Git.
- Root `.gitignore` mengabaikan output native/IDE/packaging seperti `x64/`, `Debug/`, `Release/`,
  `*.obj`, `*.tlog`, `*.ipdb`, `*.iobj`, generated installer/update feed, serta symbol/output lain;
  source, manifest, dan script packaging tetap tracked.

### Config-driven UI

- Seluruh screen dapat dirakit dari JSON baru.
- Mengubah supported visual/layout field di override baru mengubah UI tanpa mengubah component C++.
- Tidak ada legacy UI schema/import.
- Tidak ada file/JSON access di paint hot path.
- Invalid config/reference menghasilkan diagnostic yang dapat ditindaklanjuti.
- Invalid override ditolak seluruhnya dan tidak mengganti last-known-good UI.
- Invalid embedded default gagal sebelum main UI dengan bootstrap diagnostic dan non-zero exit.
- Reload hanya terjadi atas tindakan eksplisit. Candidate generation tidak mengganti UI aktif sampai
  seluruh window menutup popup, menyelesaikan IME, drain modal stack, dan membuktikan suppression depth
  nol; reload failure mempertahankan generation/tree lama tanpa native peer tersembunyi atau stale UIA.
- Dark, Light, dan High Contrast selalu resolve menjadi typed semantic contract lengkap sebelum
  document dipublikasikan.
- High Contrast mempertahankan symbolic `SystemColorSlot`; current system RGB dimaterialize saat
  render-resource build dan tidak menjadi bagian config generation.
- Alpha hanya dipakai pada surface yang sah; kombinasi lintas native `HWND` yang background akhirnya
  tidak dapat ditentukan ditolak oleh gate berdasarkan constraint statis. DPI-dependent geometry akhir
  divalidasi component pada measure/layout runtime.
- Rollback-incompatible override tidak diubah atau dihapus, embedded default tetap dapat dipakai, dan
  diagnostic menjelaskan minimum compatible binary/config contract.

### Component ownership

- Satu `.cpp` utama per jenis component dengan blok lifecycle, logic, dan UI.
- Tidak ada component-specific logic di luar pemiliknya.
- Container hanya mengetahui contract child, bukan internal child.
- Shared primitive tidak bercabang berdasarkan component type.
- Penambahan component baru tidak membutuhkan modifikasi dispatcher widget global.
- Button, Checkbox, Toggle, Text, Card, Container, Screen, dan row List tidak membuat child `HWND`.
- List mem-virtualize row/item dan tidak membuat native window per item.
- Scrollbar adalah HWND-less component dengan internal visual/interaction ownership; Container/List
  atau multiline Input memiliki scroll model dan menghubungkannya melalui component contract; visible
  native Edit scrollbar tidak dipakai.
- Input, Combo popup, infrastructure window, dan top-level Window memiliki serta membersihkan native
  window/surface yang memang menjadi tanggung jawab component/container tersebut. Dialog tidak memiliki
  native window dan memiliki seluruh modal overlay visual/logic pada surface parent.
- Input memiliki lease resource GDI peer, sedangkan `NativePeerGdiResourceCache` memiliki physical
  `HFONT`/`HBRUSH`; tidak ada GDI object creation pada paint/control-color hot path atau release ketika
  modal hanya menyembunyikan peer.
- Editable component memiliki baseline/draft/derived `IsDirty` dan generic prepare/commit-close
  participant; WindowContainer hanya mengagregasi contract tanpa mengetahui payload atau component type.
- `ModalOverlayStack` dan suppression refcount dimiliki WindowContainer sebagai generic container UI
  lifecycle; Dialog/Input/Combo tetap memiliki state/visual/native-peer behavior khususnya sendiri.

### Runtime dan visual

- Seluruh app-owned component visual digambar ke primary GDI DIB; native Edit memakai GDI peer resources
  dari cache yang sama.
- Setiap top-level window hanya mempunyai satu primary app-owned surface; Dialog memakai surface itu,
  Combo popup mempunyai specialized layered surface saat terlihat, dan app-rendered component biasa
  tidak membuat render target sendiri.
- Manifest dan runtime inspection membuktikan `PerMonitorV2`; setiap top-level/popup merespons
  `WM_DPICHANGED` miliknya pada DPI 100%, 125%, 150%, dan 200%.
- Compatibility smoke lulus pada Windows 10 22H2 build 19045 dan visual/release-primary smoke lulus pada
  supported-current Windows 11 x64 build yang exact version-nya dicatat; budget yang dibekukan pada satu
  OS tidak otomatis dianggap lulus pada OS lain.
- First frame tidak menunggu scan, network, WSL, plugin, atau business file operation.
- Top-level window baru tidak ditampilkan sebelum complete frame tersedia pada presentation buffer;
  cold start/restore tidak pernah memperlihatkan default white/blank client frame dan handled
  `WM_ERASEBKGND` tidak menghapus app-owned frame.
- First frame hanya merakit route awal; screen lain dirakit lazy lalu di-cache per window.
- Focus, hover, pressed, selected, disabled, dan keyboard tetap bekerja melalui component owner.
- Resize/DPI tidak menimbulkan border ghosting, stale pixels, overlap, atau layout corruption.
- Native Edit child dan primary DIB tetap benar pada clipping/z-order/resize. Full repaint terjadi setelah
  resize, DIB recreation, config generation, dan theme epoch; hidden window tidak menjalankan render loop.
- Input mempunyai satu continuous outline di bounds-nya, focused outline solid-accent lebih tebal,
  single-line text vertically centered, multiline top-aligned, dan seluruh text left-aligned.
- Seluruh bounds Input menjadi pointer hit target; klik padding memfokuskan Edit dan menempatkan caret
  terdekat. Multiline Scrollbar sinkron dengan native Edit dalam unit line tanpa overlap app-painted area.
- Button mempunyai contrast-to-surrounding-surface `normal < hover < pressed` pada Light maupun Dark,
  text normal minimal 4.5:1, visual boundary/focus minimal 3:1, solid accent border saat hover/pressed,
  solid focus outline tanpa dashed/dotted ring, serta nilai hover/pressed eksplisit dan independen di
  JSON. Seluruh variant lulus gerbang objektif sebelum screenshot review.
- Alpha/tint Button dan component app-rendered ter-composite benar terhadap parent pada surface yang
  sama; native child tidak mencoba membaca pixel parent lintas `HWND`.
- App-painted text dan native Edit memakai resolved `HFONT` descriptor serta GDI metrics yang sama pada
  DPI 100%, 125%, 150%, dan 200%.
- Native Edit `nativePeerContentRect` selalu berada di dalam safe opaque inner Input geometry dan tidak
  overlap dengan Scrollbar track, icon, clear button, atau app-painted decoration pada seluruh accepted
  size/DPI; layout failure menghasilkan diagnostic, bukan overwrite atau crash.
- Logical/native focus tersinkron dua arah dan mempunyai satu canonical logical focused address; window
  deactivate/reactivate tidak menghasilkan focus ring ganda atau kehilangan focus target valid.
- Sebelum modal/reload menyembunyikan Input, active IME composition di-commit dan candidate UI ditutup;
  failure menunda/membatalkan transition tanpa membuang composition.
- Nested modal memakai stack dan component-ancestry scope: native peer di belakang top Dialog
  disuspend/refcounted, Input di active Dialog tetap terlihat di atas panel, Combo milik active Dialog
  tetap boleh membuka popup, dan pop inner Dialog tidak membangunkan background sebelum stack kosong.
- Input yang disuspend menggambar text snapshot di bawah scrim, mempertahankan
  caret/selection/scroll/draft, dan memulihkan state/focus tepat setelah scope mengizinkan resume.
- Combo popup tidak mengambil activation/taskbar/Alt-Tab; keyboard tetap melalui owner, pointer tetap
  milik Combo, dan selection/Escape/outside-click/deactivate/focus-leave/route/reload/modal transition
  menutupnya secara deterministik.
- Combo layered popup mempunyai rounded clip/shadow konsisten dan full-surface update/copy cost-nya
  memenuhi input-to-paint budget pada deterministic maximum V1 size/items.
- Combo popup selalu ter-clamp pada work area monitor anchor, memilih arah buka yang reachable, dan
  recompute geometry/resource ketika berpindah monitor atau menerima DPI baru saat terbuka.
- System/Dark/Light/High Contrast menghasilkan resolved set yang benar di seluruh top-level window
  tanpa restart atau re-parse config; system-color/app-theme change hanya meningkatkan shared
  resource epoch sekali lalu meng-invalidasi dan redraw/re-present seluruh active primary serta layered
  popup context.
- Initial snapshot tidak memperlihatkan flash theme yang salah pada baseline test; post-first-frame
  `UISettings` reconciliation dan background callback tidak menyentuh UI lintas thread. DWM dark title
  attribute 20 diterapkan ulang ketika perlu dan kegagalannya tidak menggagalkan window/startup.
- Paint tidak melakukan parse/config IO atau membuat ulang resource cacheable setiap frame.
- DIB allocation/resize failure tidak menghilangkan logical UI state. Hidden restore menghitung DPI/
  layout, mengganti GDI lease/font/colors, dan merender complete
  frame sebelum `ShowWindow`, sehingga tidak ada stale atau blank intermediate frame.
- Theme/DPI change mengganti native Edit font/brush tanpa use-after-free; Edit HWND dihancurkan sebelum
  final font lease dilepas dan read-only/disabled Input memakai jalur `WM_CTLCOLORSTATIC` yang benar.

### Accessibility dan High Contrast

- Semua component interaktif keyboard-reachable dan mempunyai visible focus yang tidak bergantung pada
  warna saja.
- Component app-rendered interaktif mengekspos accessible name, role, state, dan action melalui UIA;
  List virtualized tetap dapat dinavigasi secara accessibility tanpa `HWND` per row.
- Native Edit mempertahankan text/selection accessibility dan terhubung ke accessible label Input.
- Input muncul tepat sekali pada UIA tree melalui `IRawElementProviderHwndOverride`; native host provider
  mempertahankan Text/Value/selection pattern, visual/Tab/navigation order konsisten, dan peer recreation
  tidak meninggalkan duplicate atau stale provider.
- Combo hanya mengekspos required `ExpandCollapse` pada Combo provider; popup HWND memiliki hosted List
  fragment yang direparent melalui bidirectional navigation, List mengekspos `Selection`, item
  `SelectionItem`, dan tidak ada duplicate popup sebagai desktop child.
- Top Dialog provider melaporkan Window/IsDialog/IsModal semantics. Background provider tetap stable
  tetapi unreachable, disabled/non-focusable, dan tidak ditandai offscreen hanya karena scrim; suspended
  Input menolak focus lalu kembali dengan identity yang sama dan structure/focus event yang benar.
- Narrator serta accessibility inspection smoke benar-benar dijalankan sebelum klaim PASS.
- High Contrast memakai semantic Windows system colors melalui resolved config set dan tidak memakai
  alpha dekoratif yang mengurangi kontras.

### Performance dan resource budget

Semua angka diukur dari installed Release x64 dengan deterministic stub data dan trace marker yang
sama. Minimum 10 sample dipakai selama development dan 30 sample pada release candidate. Metric startup
canonical adalah cold process dengan OS/file cache hangat dan tanpa app warm-up; true cold-file-cache
run dicatat terpisah. Angka berikut adalah target regression V1, bukan blocker pemilihan stack:

- cold process-start sampai first complete non-blank frame terlihat: p95 maksimal 250 ms;
- laporan cold/warm start memisahkan process/bootstrap/config, first layout/render, BitBlt present, dan
  first visible frame;
- warm process-start sampai first complete non-blank frame terlihat: p95 maksimal 120 ms;
- first-time route assembly/navigation: p95 maksimal 100 ms;
- cached route navigation: p95 maksimal 50 ms;
- input event sampai visual state terlihat: p95 maksimal 33 ms;
- application-owned UI-thread work untuk satu input event: p95 maksimal 8 ms;
- resize/layout/paint frame selama deterministic resize: p95 maksimal 16.7 ms dan tidak ada single
  application-owned stall di atas 50 ms;
- idle private commit setelah 10 detik settle pada satu Terminal stub window: target maksimal 64 MiB;
  total working set tetap direkam sebagai diagnostic dan bukan hard PASS/FAIL metric;
- process baseline mempunyai tepat satu hidden infrastructure top-level `HWND`; visible route window
  menambah satu top-level `HWND` per window, instantiated native Edit menambah child `HWND`, dan active
  Combo menambah satu layered popup. Dialog, Button, Checkbox, Toggle, Scrollbar, dan List row menambah
  nol `HWND`;
- 100 cycle route/theme/hide-restore tidak menghasilkan pertumbuhan bersih `HWND`, USER/GDI handle,
  private commit, render context, GDI lease, atau cache entry pada resource domain mana pun setelah
  settle;
- `RenderRuntime` diagnostic API melaporkan jumlah active render context serta cache entry
  termasuk cached `HFONT`, cached `HBRUSH`, dan active native-peer lease sehingga no-growth dapat diuji.
  Solid brush key hanya final opaque `COLORREF`; zero-lease entry wajib hilang setelah settle.

Hasil target yang gagal dicatat pada report dan diperbaiki secara iteratif. Ia tidak menghentikan phase
selama build, behavior utama, dan data safety tetap berfungsi; target tidak dihapus diam-diam.

### Multi-window dan tray

- Satu proses dapat memiliki minimal dua independent top-level window.
- External Chrome Launcher route tidak mengganti Terminal window yang sudah terbuka.
- External route mengaktifkan matching window yang sudah ada atau membuat satu bila belum ada.
- Same-window navigation ke route yang sama adalah no-op; visible match lain diaktifkan, sedangkan
  retained match dipulihkan lewat canonical DPI/layout/resource/frame-before-show path dan mengosongkan
  retained slot tanpa mengganti route window pemanggil.
- Registry assertion membuktikan satu route ID tidak pernah mempunyai visible dan retained-hidden
  instance bersamaan.
- Menutup satu window tidak menutup window lain.
- Maksimal satu retained hidden route window boleh hidup di luar infrastructure window. Jika belum ada
  retained window dan tray tersedia, menutup route window terakhir meng-hide instance yang sama,
  mempertahankan route/draft/state, dan melepas render resource beratnya.
- Jika retained hidden window lama sudah ada, close atas last visible route window menjadikan window
  terbaru sebagai retained setelah old-retained `PrepareClose` berhasil. Cancel mempertahankan old
  retained dan membiarkan newest window visible; tidak ada state yang dihancurkan sebagian.
- Klik kiri tray memulihkan retained hidden route window atau membuat Terminal window bila tidak ada.
- Klik kanan tray menyediakan route dan Exit; Explorer restart memasang kembali icon.
- Satu hidden infrastructure top-level window menerima tray callback, `TaskbarCreated`, second-launch,
  dan process-global OS-state signal sepanjang umur proses.
- Kegagalan tray tidak pernah meninggalkan proses hidup tanpa visible/reachable window.
- Saat tray unavailable, retained hidden window dipulihkan; close atas last reachable route window
  menjalankan `PrepareCloseAll` lalu orderly Exit hanya setelah seluruh dirty participant mengizinkan.
- Explicit Exit memulihkan retained dirty window untuk confirmation, tidak menghancurkan window sebelum
  seluruh prepare selesai, dan Cancel membatalkan Exit sepenuhnya.

### Stub/business boundary

- Stub menyediakan deterministic data dan tidak memiliki business side effect.
- Semua action/patch melewati typed bridge.
- Setiap `UiEvent` dan `UiPatch/ViewState` membawa `UiAddress` dengan mandatory window, screen, serta
  component runtime identity; stale/missing target ditolak.
- Component/container tidak menjalankan business operation.
- Business integration kelak tidak mengenal JSON/HWND/paint/layout.
- Stub Save hanya menerima draft sebagai in-memory baseline dan mengirim deterministic success/failure
  patch; Discard baru diterapkan saat commit dan Cancel tidak mengubah draft/retention/visibility.

### Installer, updater, dan release artifact

- Preview pertama dan setiap accepted build setelahnya dapat dihasilkan sebagai installer.
- Per-user Velopack `Setup.exe` dapat melakukan clean install dan first launch tanpa jaringan.
- Clean install menjalankan binary dari installed location dengan product/version identity yang benar.
- Installed build `N` dapat diperbarui ke `N+1` dan benar-benar menjalankan `N+1`.
- Update mempertahankan user data dan tidak mencampur program files antarversi.
- Update invalid/gagal tidak merusak versi terpasang yang masih valid.
- Rollback tidak menghapus/menimpa override baru; binary lama memakai embedded default serta diagnostic
  bila override membutuhkan contract lebih baru.
- Update check/download tidak menahan first frame atau UI thread.
- Manual check, scheduled 24-hour check setelah frame+idle, no-auto-download, explicit consent,
  updater-owned atomic timestamp state, `PrepareCloseAll`, one-previous-full retention, one-staged-attempt
  cleanup, dan failed-apply current-version survival semuanya lulus installed smoke.
- Uninstall membersihkan integration/program files sesuai contract tanpa menghapus user data diam-diam.
- Generated installer/update/release artifacts tidak tracked di source Git.
- Normal installed UI process berjalan unelevated; elevated business action terpisah, dan second-launch
  routing same-user/session lulus pada install/privilege model yang dipilih tanpa unsafe UIPI exception.

## 23. Validation minimum

Project scaffold wajib menyediakan command canonical berikut; implementation agent tidak memilih
generator, runner, konfigurasi, atau package command baru:

```powershell
.\tools\Test-Toolchain.ps1
.\tools\Restore-Dependencies.ps1
.\tools\Build.ps1 -Configuration Debug -Platform x64
.\tools\Build.ps1 -Configuration Release -Platform x64
.\tools\Test.ps1 -Configuration Debug -Platform x64
.\tools\Test.ps1 -Configuration Release -Platform x64
.\tools\Measure-Performance.ps1 -Configuration Release -Samples 30
.\tools\Build-Package.ps1 -Version 0.1.0 -Channel win-preview
.\tools\Test-InstalledUpdate.ps1 -FromVersion 0.1.0 -ToVersion 0.1.1 -Channel win-preview
```

Script tersebut memakai exact MSBuild/vpk invocation §25.2/§25.8 dan gagal bila pin tidak cocok.
Minimum setiap implementation phase:

- `git diff --check`;
- build Debug x64;
- build Release x64;
- test schema/parser/resolution;
- test Dark/Light/High Contrast contract completeness, alpha/surface rejection, override minimum-version,
  symbolic system-color materialization/resource-epoch change, serta rollback fallback;
- test component/event/patch contracts yang terkena;
- headless/hidden-window measure test yang membuktikan tidak ada dependency ke active window DC;
- reload normalization test untuk popup, nested modal stack, suppression refcount, removed component,
  active IME success/failure, rollback ke generation lama, dan focus restoration;
- Windows smoke untuk behavior runtime/visual yang berubah, termasuk GDI DIB allocation/resize failure,
  dirty/full repaint, native Edit text consistency, Combo layered popup, in-surface Dialog, lazy route,
  Scrollbar, dan virtualized List;
- modal-overlay smoke untuk nested stack, component-ancestry scope, native-peer suppression refcount,
  active-Dialog Input/Combo, IME completion, suspended Input snapshot, restoration
  caret/selection/scroll/draft, popup close, dismissal, focus trap, dan reload drain;
- two-way logical/native focus smoke termasuk click/Tab, direct Edit focus, window deactivate/reactivate,
  component removal, dan config-generation reconciliation;
- keyboard, UIA inspection Raw/Control/Content tree, Narrator duplicate-announcement check, Input HWND
  override/host-provider order dan peer-recreation check, Combo popup logical reparenting/pattern split,
  Dialog IsDialog/IsModal active-scope/background behavior, High Contrast, serta List
  ItemContainer/VirtualizedItem smoke;
- manifest/runtime verification bahwa `PerMonitorV2` aktif dan per-window/popup `WM_DPICHANGED` behavior
  benar pada DPI 100%, 125%, 150%, dan 200%;
- Windows 10 build 19045 compatibility matrix serta supported-current Windows 11 visual/release-primary
  matrix dengan exact OS build dan hasil performance revalidation tercatat;
- no-blank-frame visual/capture check pada cold start, new-window route, hidden-window restore, dan stale
  DPI/theme hidden restore setelah lease/font/color update;
- theme-platform smoke untuk initial snapshot, post-frame `UISettings` reconciliation,
  `ColorValuesChanged` background dispatch/coalescing, fallback failure, dan DWM attribute reapply;
- Combo popup smoke untuk non-activation, owner keyboard routing, pointer selection, seluruh dismissal
  trigger, theme/High-Contrast live redraw, taskbar/Alt-Tab absence, dan per-popup DPI;
- close-coordination smoke untuk dirty single close, non-destructive dirty hide, newest retained
  replacement, Save success/failure, staged Discard, Cancel rollback, explicit Exit, tray-failure Exit,
  dan no-duplicate-route registry assertion;
- performance harness untuk first-complete-visible-frame, route, input-to-paint, layered-popup
  full-surface update/copy, resize, private commit, diagnostic working set, `HWND`, USER/GDI handle,
  render-context/cache-entry/native-peer-GDI-lease counter, 100-cycle no-growth check, serta all-route
  open/close scenario. Startup report memisahkan bootstrap/config, layout/render, BitBlt, dan visible;
- clean-install smoke dari artifact, bukan build directory;
- offline `Setup.exe` clean-install/first-launch smoke dengan jaringan dimatikan;
- installed update smoke dari version `N` ke version `N+1`;
- preservation check untuk config/settings/cache/user data yang relevan;
- failed/invalid update dan uninstall smoke;
- package version, manifest/checksum/signature, shortcut, taskbar, tray, dan installed-path checks sesuai
  locked contract §25.7-§25.9;
- second-launch routing same-user/session pada locked per-user privilege/install scope, termasuk explicit
  behavior ketika executable diluncurkan manual dengan elevated token;
- pemeriksaan bahwa nested repositories tidak berubah;
- pemeriksaan bahwa artefak binary tidak staged/tracked dan native build/package patterns sudah
  tercakup `.gitignore`.

Jangan mengklaim visual PASS hanya dari build atau unit test. Visual PASS membutuhkan aplikasi Windows
yang benar-benar terlihat pada state dan viewport/DPI yang relevan.

## 24. Di luar scope UI/stub phase

- migration atau compatibility dengan UI lama;
- import/link/dependency terhadap nested repository;
- layout mirroring RTL dan `WS_EX_LAYOUTRTL`; V1 menjamin layout LTR. Unicode bidi text pada app-painted
  Text memakai Windows Uniscribe `ScriptStringAnalyse`/`ScriptStringOut`; native Edit memakai behavior OS;
- terminal/Chrome/WSL launch nyata;
- scan profile nyata;
- settings/provider/API key persistence nyata;
- network request business yang tidak diperlukan installer/updater;
- plugin discovery atau marketplace;
- penghapusan implementasi lama;
- business rule baru atau perubahan behavior business lama.

Packaging, installer, installed-update validation, dan release-artifact generation adalah pengecualian
yang sengaja masuk scope V1. Public publication tetap membutuhkan instruksi user tersendiri.

## 25. Final execution contract

Bagian ini adalah keputusan implementasi, bukan menu pilihan. Implementasi langsung memakai contract
berikut. Bila build/test/runtime gagal, perbaiki implementasi atau pin yang rusak dan lanjutkan; jangan
menahan pekerjaan untuk membuat plan pembuktian baru. User hanya perlu diminta lagi bila perubahan
menyentuh product scope, business semantics, privilege, user-data deletion, atau publication external.

### 25.1 Status freeze

| Area | Status sebelum source | Closure berikutnya |
| --- | --- | --- |
| arsitektur dan product behavior | locked | hanya berubah melalui keputusan user |
| Phase 0A technical contract | locked di §25.2-§25.6 | implementation verification, bukan stack selection |
| Phase 0B production contract | locked di §25.7-§25.9 | implement unsigned V1 workflow yang sudah ditentukan |
| primary presentation | locked GDI DIB + BitBlt | implement §25.3; tidak ada backend probe |
| performance/resource target | locked regression target §22 | ukur, catat, lalu optimalkan bila gagal |
| field/flags per component | locked §25.10 | implement sesuai schema/default table |
| business integration order | locked Phase 6 | Terminal → Settings → Chrome → Inject → JSON Editor → UI Editor → Deployment |

Plan tidak membutuhkan dokumen `AGENTS.md` atau implementation-plan kedua. `Termial-plan.md` adalah
satu-satunya spesifikasi delivery root. Phase 0 dimulai dengan membuat scaffold, bukan audit tambahan.

### 25.2 Exact toolchain, project, dependency, dan test contract

Build system V1 adalah native Visual Studio/MSBuild solution, bukan CMake, Meson, Bazel, atau generator
yang dipilih kemudian:

- solution `Terminal.sln`; application project `src\Terminal.vcxproj`; contract test
  project `tests\TerminalTests.vcxproj`; performance harness project
  `tests\performance\TerminalPerformance.vcxproj`;
- Visual Studio Build Tools 2022 `17.14.36` (`17.14.37502.11`), MSBuild `17.14.51.32402`, platform
  toolset `v143`, `VCToolsVersion=14.44.35207`, dan `cl.exe 19.44.35228` x64;
- Windows SDK `10.0.26100.0`; `_WIN32_WINNT=0x0A00`, target x64 only, dan runtime gate tetap Windows 10
  build 19045. Pemakaian SDK baru tidak mengizinkan unguarded API yang absen pada 19045;
- C++20, `/std:c++20 /permissive- /Zc:__cplusplus /utf-8 /EHsc /W4 /WX`; Release memakai `/O2 /MT`,
  Debug `/Od /RTC1 /MTd`; linker memakai `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf` dan
  `CETCOMPAT` bila linker exact tersebut menerimanya;
- system link set exact:
  `comctl32.lib;shell32.lib;shlwapi.lib;ole32.lib;oleaut32.lib;uuid.lib;dwmapi.lib;uxtheme.lib;advapi32.lib;crypt32.lib;user32.lib;gdi32.lib;propsys.lib;uiautomationcore.lib;usp10.lib;psapi.lib;windowsapp.lib`;
- COM ownership memakai `Microsoft::WRL::ComPtr`; C++/WinRT projection yang dibutuhkan `UISettings`
  berasal dari Windows SDK 10.0.26100.0, bukan NuGet runtime tambahan;
- `global.json` mengunci .NET SDK `9.0.304` dengan roll-forward disabled hanya untuk local `vpk`; file
  `.config\dotnet-tools.json` mengunci `vpk` `1.2.0` dan `rollForward: false`;
- tracked `dependencies.lock.json` menyimpan URL, version, SHA-256, selected files, dan license. Script
  `tools\Restore-Dependencies.ps1` mengunduh ke ignored `build\deps`, memverifikasi hash sebelum extract,
  dan tidak pernah commit header hasil download, import library, DLL, package, atau cache;
- JSON parser adalah single-header `nlohmann/json` `v3.12.0`, tag object
  `65ee68451d8eb2b5f3a30b410476ab83deb3289b`. `json.hpp` berasal dari official tag URL dengan SHA-256
  `AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63`; license MIT ikut dipulihkan;
- parser memakai `parser_callback_t` dengan per-object key set pada `object_start/key/object_end` untuk
  menolak duplicate key sebelum DOM dipublikasikan; `allow_exceptions=true`, comments disabled, dan
  seluruh size/depth/numeric limit §7.2 tetap diterapkan sebelum typed resolution;
- Velopack memakai official `velopack_libc_1.2.0.zip`, SHA-256
  `547262ED7A1AB1FF62F580AA53851EDE2F1A451AC61B8974EB7BC01117488835`. Build x64 memakai
  `include\Velopack.hpp`, `lib\velopack_libc_win_x64_msvc.dll.lib`, dan meng-copy
  `lib\velopack_libc_win_x64_msvc.dll` ke publish output. Binary dependency hanya masuk build/package
  output, tidak source Git;
- tidak ada Catch2, GoogleTest, atau framework test lain. `TerminalTests.exe` adalah runner
  kecil repository-owned. CLI exact:
  `TerminalTests.exe [--filter <glob>] [--report-json <path>] [--report-junit <path>]`.
  Tanpa filter menjalankan semua test dalam urutan nama ordinal. Default report adalah
  `artifacts\test-results\<Configuration>\results.json` dan `results.junit.xml`. JSON membawa
  `{ schemaVersion, configuration, startedUtc, durationMs, totals, tests[] }`; setiap test membawa
  `{ name, status, durationMs, message, file, line }`. JUnit memakai satu `testsuite`. Exit `0` seluruh
  selected test PASS, `1` ada failure, `2` argumen/discovery/empty-selection invalid; crash tetap
  mempertahankan non-zero OS exception code dan `tools\Test.ps1` memperlakukannya FAIL;
- `tools\Test-Toolchain.ps1` memeriksa seluruh exact version di atas. Version mismatch adalah blocker
  build dengan install/repair guidance; script tidak memilih toolset/SDK terbaru otomatis. Source dan
  scaffold tetap boleh dibuat sebelum toolchain mesin diperbaiki;
- CI dan release memakai GitHub-hosted `windows-2022`, `actions/setup-dotnet` exact `9.0.304`,
  `vswhere` untuk VS 2022, project-pinned `VCToolsVersion=14.44.35207` dan SDK `10.0.26100.0`. Jika image
  tidak menyediakan pin, job gagal dan pin/workflow diperbaiki; tidak berpindah ke self-hosted runner
  atau generator build lain saat eksekusi.

Canonical MSBuild di balik `tools\Build.ps1` adalah:

```powershell
& $msbuild .\Terminal.sln /m /t:Build `
  /p:Configuration=$Configuration /p:Platform=x64 `
  /p:PlatformToolset=v143 /p:VCToolsVersion=14.44.35207 `
  /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:PreferredToolArchitecture=x64
```

Scaffold membuat dependency lock dan toolchain check terlebih dahulu, lalu source boleh ditulis pada
turn implementasi yang sama. Availability failure tidak membatalkan scaffold.

### 25.3 Exact GDI renderer object graph

`RenderRuntime` hidup pada UI thread dan memiliki `NativePeerGdiResourceCache`, generic `HFONT`/brush/
pen cache, antialiased-corner-tile cache maksimum 96 entry, temporary text-mask pool, serta registry
active context. Tidak ada D2D/DWrite/D3D/DXGI object.

Setiap `WindowRenderContext` memiliki satu persistent memory DC dan satu selected top-down 32-bit BGRA
`CreateDIBSection` sebesar client area. DIB memakai stride 4-byte aligned, origin top-left, dan alpha
hasil akhir `255`. Resize membuat DC/DIB candidate, memilih bitmap, merender complete frame, lalu swap;
old bitmap dipilih kembali sebelum delete. Allocation failure mempertahankan old complete frame bila
masih ada dan menghasilkan diagnostic tanpa loop.

`WM_PAINT` memakai `BeginPaint`, memastikan buffer, merender invalid union ke DIB, melakukan satu
`BitBlt(..., SRCCOPY)` untuk `PAINTSTRUCT::rcPaint`, lalu `EndPaint`. Full repaint dilakukan pada first
frame, resize, DPI/theme/config epoch, retained-window restore, dan cache reset. `WM_ERASEBKGND`
mengembalikan handled setelah buffer valid. Window baru baru di-`ShowWindow` setelah complete DIB siap.

Rounded shape memakai cached opaque analytic-coverage corner tile yang diadaptasi dari
`Open-terminal-native\src\ui\paint.cpp`; straight region memakai `FillRect`, corner memakai empat
`BitBlt`. Software source-over mengomposisi alpha/tint/scrim ke primary opaque background. Primary text
memakai cached `HFONT` dan GDI. Native Edit memakai lease font/brush yang sama.

`LayeredPopupRenderContext` memiliki top-down premultiplied BGRA DIB dan memory DC sendiri. Shape/shadow
dikomposisi software; glyph dirender ke reusable mask lalu dikomposisi dengan premultiplied alpha.
Present selalu `UpdateLayeredWindow(ULW_ALPHA, AC_SRC_ALPHA)`. Resize transactional dan selected bitmap
dikembalikan sebelum handle dihapus. Ini satu-satunya alternate present path V1 dan bukan backend
pilihan.

### 25.4 Exact config identity, metadata, resource, path, dan diagnostic

- schema identity exact: `yuzha.terminal.ui`; schema `version: 1`;
- embedded source authoring: `Assets\ui\core.json` + `Assets\ui\screens\<routeId>.json` (satu file per
  screen, nama file adalah route ID, tanpa manifest). Build-time merge oleh
  `tools\Merge-UiConfig.ps1` menghasilkan `build\generated\ui\terminal.ui.default.v1.json` yang di-embed
  dengan resource type `RT_RCDATA`, symbolic ID `IDR_UI_DEFAULT_JSON`, numeric ID `201`. File hasil
  merge adalah build artifact yang tidak tracked Git. Merge menggagalkan build bila JSON invalid,
  `routeId` tidak sama dengan nama file, `core.json` memuat `screens`, nama file bukan lower-kebab-case,
  atau hasil gabungan melewati batas 4 MiB/64 level §7.2;
- optional override exact:
  `%LOCALAPPDATA%\Yuzha\Terminal\ui\override.v1.json`;
- legacy filename/path tidak dicoba. Runtime tidak mencari `ui.json`, sibling executable config, nested
  repo assets, atau `%LOCALAPPDATA%\OpenTerminal`;
- default memakai `documentKind: default`; override memakai `documentKind: override`. Keduanya membawa
  `schema`, `version`, `minimumReaderContract`, dan `writtenBy { appVersion, configContract }` sebelum
  `tokens/styles/windows/screens`;
- binary V1 mengompilasi `readerContract=1` dan `writerContract=1`. `minimumReaderContract` adalah integer
  positif; override diterapkan hanya bila nilainya `<= readerContract`. `writtenBy.appVersion` hanya
  diagnostic SemVer dan tidak menentukan compatibility. UI Editor mengisi minimum contract tertinggi
  dari field yang ditulis;
- binary lama yang melihat minimum reader lebih tinggi menolak seluruh override, mempertahankan bytes,
  memakai embedded default, dan menampilkan rollback-incompatible diagnostic;
- persistent config banner berada di bagian atas Settings/UI Editor dengan severity, error code,
  source/path, JSON pointer/line-column bila ada, dan pesan `Override UI tidak diterapkan`. Banner tidak
  hilang oleh navigation/close Settings dan baru clear setelah load/reload sukses; action yang tersedia
  hanya `Buka UI Editor` dan `Coba reload`, bukan silent reset/delete;
- root data tetap `%LOCALAPPDATA%\Yuzha\Terminal`; config log
  `logs\ui-config.log`; updater state `updater\state.json`; measurement artifact tidak pernah ditulis ke
  data root normal dan berada di ignored repository `artifacts\measurements` atau test temp directory.

### 25.5 Exact measurement contract

Instrumentation memakai `QueryPerformanceCounter` dan TraceLogging provider
`Yuzha.Terminal.Performance`, GUID `{926b237e-f049-4ec4-8026-5db2e27a8239}`. Event minimal:
`ProcessEntry`, `VelopackHooksComplete`, `ConfigResolved`, `RenderBufferReady`, `FirstLayoutComplete`,
`FirstPresentComplete`, `FirstFrameVisible`, `InputReceived`, `InputVisualPresented`,
`NavigationRequested`, `NavigationPresented`, `ResizeFramePresented`, `ScenarioSettled`, dan
`ResourceSnapshot`. Correlation ID wajib menghubungkan input/navigation/resize start ke present yang
benar; timestamp dari message receipt sampai event setelah successful present.

`FirstFrameVisible` dicatat setelah complete buffer dipresentasikan, top-level window di-show, dan satu
`DwmFlush` selesai. Startup end-to-end dimulai dari timestamp harness tepat sebelum `CreateProcessW`,
bukan dari sesudah `wWinMain`; report tetap menyimpan internal breakdown event. Harness adalah
`TerminalPerformance.exe`, memakai system-wide QPC yang sama, dan tidak mengukur dengan PowerShell
`Measure-Command`.

`tools\Measure-Performance.ps1` mengorkestrasi installed Release x64, `wpr.exe` file-mode profile, dan
`tracerpt.exe` export; tool tersebut sudah tersedia pada Windows baseline. Report canonical adalah
`artifacts\measurements\<UTC>-<git-sha>\report.json` plus raw `.etl`/CSV, berisi toolchain, Git SHA,
package version, installed path, renderer `gdi-dib-bitblt`, OS build/UBR, DPI/display, CPU/GPU/
driver/RAM/storage, scenario parameters, seluruh sample, min/median/p95/max, target, serta PASS/FAIL.

Baseline awal exact untuk numeric budget adalah mesin yang diaudit 2026-08-14:

- Windows 10 Pro 22H2 x64 `19045.7663`, primary display `2560×1440` pada 96 DPI/100%;
- Intel Core i5-14400F, 10 core/16 logical processor, RAM 32 GiB;
- NVIDIA GeForce RTX 3050 driver `32.0.15.6094`; source/install pada WD Blue SN5000 1 TB NVMe.

Windows 11 release-primary matrix memakai GA Windows 11 25H2 build family `26200` dengan exact UBR
dicatat pada run; compatibility matrix tetap Windows 10 build 19045. Functional DPI matrix adalah
100/125/150/200%; numeric budget canonical diambil pada primary 100% baseline di atas.

Canonical startup `cold-process/warm-file-cache` berarti tidak ada app process/tray tersisa, settle dua
detik, harness membaca seluruh installed binary/package input sekali untuk menghangatkan file cache
tanpa meluncurkan app, lalu mencatat timestamp sebelum process creation. Tidak ada app/device warm-up.
Warm startup menjalankan accepted build sekali, keluar orderly, menunggu dua detik, lalu sample berikutnya.
True cold-file-cache/reboot run hanya diagnostic dan tidak mengganti canonical PASS/FAIL.

Deterministic data adalah Combo 200 item dengan maksimum popup 480×480 DIP dan List 10,000 row dengan
32 DIP row height serta viewport 40 row. Selain startup, satu warm-up scenario tidak dihitung lalu
minimum 30 sample dicatat. Resource snapshot memakai `GetProcessMemoryInfo` `PrivateUsage` sebagai
private commit dan `WorkingSetSize` diagnostic, `GetGuiResources` untuk USER/GDI, enumeration HWND by
PID, serta diagnostic API `RenderRuntime` untuk render context/cache/lease. Settle adalah 10 detik idle
setelah pending UI work, popup, animation, worker, dan deferred zero-lease eviction selesai.

Target §22 langsung berlaku sebagai regression target. Report tidak membuang outlier. Kegagalan target
menghasilkan pekerjaan optimasi dan tidak mengganti renderer atau menghentikan feature implementation.

### 25.6 Exact single-instance dan second-launch IPC contract

V1 mengadopsi pola yang sudah bekerja di `Open-terminal-native`: named mutex + hidden infrastructure
window + `WM_COPYDATA`. Tidak ada named pipe, listener thread, SDDL, atau IPC library.

Primary membuat mutex `Local\Yuzha.Terminal.Instance.v1.<userSidSha256-32hex>` memakai default
current-token security dan message-only infrastructure window class
`Yuzha.Terminal.Infrastructure.v1`. Suffix adalah 16 byte pertama SHA-256 textual TokenUser
SID; `Local` membatasi session. Secondary menemukan receiver memakai
`FindWindowExW(HWND_MESSAGE, nullptr, className, nullptr)`.

`COPYDATASTRUCT.dwData` exact `0x594F544E00000001`, `cbData` maksimum 64 KiB, dan `lpData` membawa strict
UTF-8 JSON tanpa NUL wajib. Request menolak duplicate/unknown field dan nesting di atas 16. Bentuk exact:

```json
{
  "protocol": "yuzha.terminal.ipc",
  "version": 1,
  "requestId": "lowercase-uuid",
  "command": "open-route",
  "arguments": { "routeId": "terminal" }
}
```

Command/arguments exact:

- `activate-default`: `{}`;
- `open-route`: `{ "routeId": <terminal|json-inject|json-editor|chrome-launcher|chrome-profile-manager|settings|ui-editor> }`;
- `request-exit`: `{}`.

Receiver menyalin payload selama handler, parse/validasi, lalu memasukkan semantic event ke bounded
`ApplicationContainer` queue maksimum 64. `LRESULT` memakai `MAKELONG(status,error)` dengan status
`1=accepted`, `2=rejected`, `3=busy`; error `0=none`, `1=invalid-payload`, `2=unsupported-version`,
`3=unsupported-command`, `4=invalid-route`, `5=queue-full`, `6=shutdown-in-progress`. Accepted berarti
queued, bukan selesai paint.

Secondary mencari window pada elapsed `0, 50, 150, 350, 750 ms`, lalu memakai `SendMessageTimeoutW`
dengan `SMTO_ABORTIFHUNG | SMTO_BLOCK` dan timeout `1000 ms`. Timeout boleh satu retry setelah `250 ms`
dengan requestId sama; primary menyimpan 128 requestId terakhir selama dua menit untuk idempotency.
Total maksimum dua detik. Exit secondary: `0` accepted, `20` receiver tidak ditemukan, `21` rejected,
`22` busy, `23` timeout. Runtime normal selalu unelevated sesuai §19; tidak ada
`ChangeWindowMessageFilterEx`. Manual elevated launch direlaunch unelevated sebelum mutex bila linked
token tersedia.

### 25.7 Feed, channel, update API, integrity, dan unsigned V1 contract

Evidence reference adalah read-only `Open-terminal`: pinned Velopack 1.2.0, local file source,
anonymous GitHub Release source, successful `v0.3.0 → v0.3.1`, full/delta packages, `RELEASES`, dan
`releases.win.json`. Greenfield tidak mengimpor source lama, tetapi mempertahankan deployment pattern
yang sudah terbukti.

- preview channel exact `win-preview`; local feed exact ignored directory
  `artifacts\releases\win-preview`; metadata `releases.win-preview.json`;
- production stable channel exact `win`; source/host
  `Velopack::GithubSource("https://github.com/yuzhayo/Terminal", "", false)`; metadata
  `releases.win.json`; public app tidak membawa PAT/token;
- package channel di-embed oleh `vpk pack --channel`; runtime normal memakai
  `UpdateOptions { AllowVersionDowngrade=false, ExplicitChannel=nullopt,
  MaximumDeltasBeforeFallback=1 }`. Channel tidak diganti lewat settings biasa;
- test local source hanya diterima oleh `win-preview` package dari absolute directory environment
  `TERMINAL_UPDATE_SOURCE`; stable `win` package mengabaikan override tersebut;
- `VelopackApp::Build().SetAutoApplyOnStartup(false)` serta install/update/uninstall hooks dipanggil paling
  awal di `wWinMain`, sebelum mutex, config, renderer, atau window. Download hanya setelah consent;
  apply lebih dahulu menjalankan `PrepareCloseAll`; setelah seluruh participant mengizinkan, deployment
  service memanggil `WaitExitThenApplyUpdates` lalu melakukan orderly process exit;
- V1 preview dan stable mengikuti reference yang sudah bekerja: artifact unsigned. Integrity memakai
  Velopack package/feed hash validation, GitHub HTTPS transport, generated `SHA256SUMS`, corruption test,
  dan SHA-256 installer pada release summary. Windows dapat menampilkan Unknown Publisher; ini diterima
  untuk V1 dan tidak menahan implementasi/release;
- Authenticode/certificate adalah enhancement setelah V1. Ketika ditambahkan, gunakan `--signParams`
  tanpa mengganti package ID, channel, feed, update API, atau release transport.

### 25.8 Exact packaging, installed-update, dan production-release command

`tools\Build-Package.ps1` lebih dahulu menjalankan toolchain/dependency check, Release build, contract
tests, dan publish assembly ke `artifacts\publish\Release\x64`; folder tersebut hanya berisi runtime
yang diperlukan. Exact pack invocation baseline:

```powershell
dotnet tool restore
dotnet vpk pack `
  --packId Yuzha.Terminal `
  --packVersion $Version `
  --packDir .\artifacts\publish\Release\x64 `
  --mainExe Terminal.exe `
  --packTitle "Terminal" `
  --packAuthors "Yuzha" `
  --icon .\Assets\terminal.ico `
  --outputDir ".\artifacts\releases\$Channel" `
  --runtime win-x64 `
  --channel $Channel `
  --shortcuts Desktop,StartMenuRoot `
  --delta BestSpeed
```

Tidak ada `--framework` karena application CRT static dan Velopack native runtime ikut package. V1 tidak
menambahkan `--signParams`. Script memverifikasi Setup, portable ZIP, full package, channel feed,
version, SHA256SUMS, dan—bila previous release tersedia—delta package. Output Git-ignored.

`tools\Test-InstalledUpdate.ps1` membuat dua preview package berurutan, clean-install version N melalui
offline Setup, meluncurkan installed path, mengarahkan source ke local `win-preview` feed dengan test
source environment, menjalankan check/download/consent/apply, menunggu restart, lalu memeriksa file/product version
N+1, preserved data, no mixed version files, single-instance routing, tray/shortcut, failed/corrupt
package survival, explicit downgrade test, uninstall-preserve, dan uninstall-delete UX.

Isolation exact: pada GitHub Actions script berjalan langsung pada disposable `windows-2022` runner dan
lebih dahulu memastikan package ID belum terpasang. Pada mesin developer default memakai generated
Windows Sandbox `.wsb` dengan mapped read-only package/feed dan mapped result directory. Eksekusi pada
current user hanya diizinkan dengan `-Environment CurrentUser -AllowInstallMutation`; script menolak
mode tersebut bila instalasi production sudah ada. Uninstall/cleanup selalu dijalankan pada `finally`.

Production release host adalah GitHub Releases repository `yuzhayo/Terminal`. Workflow hanya manual
`workflow_dispatch`, branch `main`, concurrency `terminal-release`, current-commit CI harus
PASS, runner `windows-2022`, version source `version.props`, tag `vMAJOR.MINOR.PATCH`, dan tag/asset tidak
pernah dioverwrite. Workflow mengadaptasi `.github\workflows\release.yml` dari `Open-terminal`: setup
.NET 9.0.304, restore tool/package, download previous Velopack release untuk delta, build/test/package/
verify, lalu:

```powershell
dotnet vpk upload github `
  --repoUrl "https://github.com/yuzhayo/Terminal" `
  --outputDir ".\artifacts\releases\win" `
  --channel win `
  --token $env:GITHUB_TOKEN `
  --publish `
  --releaseName "Terminal $Version" `
  --tag "v$Version" `
  --targetCommitish $CommitSha
```

`GITHUB_TOKEN` hanya repository-scoped `contents: write`. Release dispatch/publication selalu memerlukan
instruksi eksplisit user; plan ini tidak mengotorisasi push, tag, workflow dispatch, atau publish.

### 25.9 Exact uninstall-user-data UX dan deletion safety

Default uninstall dari Windows Settings/Velopack selalu mempertahankan
`%LOCALAPPDATA%\Yuzha\Terminal`. Ia menghapus versioned program files, shortcuts, taskbar/Jump
List/Explorer integration milik aplikasi, serta installer registration; tidak menghapus config,
settings, cache, history/bookmark, drafts, logs, updater diagnostic, atau credential tanpa pilihan user.

Settings menyediakan action `Uninstall Terminal…`. In-surface confirmation menampilkan dua
radio option:

1. `Pertahankan data saya (direkomendasikan)` — default/focused;
2. `Hapus seluruh data Terminal dari PC ini` — menampilkan exact root path dan memerlukan
   checkbox kedua `Saya memahami data ini tidak dapat dipulihkan` sebelum tombol Uninstall enabled.

Flow pilihan kedua lebih dahulu harus lulus `PrepareCloseAll`; Cancel tidak menulis marker atau
meluncurkan updater. Setelah prepare lulus, app menulis atomic one-time marker ke
`updater\uninstall-intent.json` berisi random 128-bit nonce, package ID, installed version, exact
canonical data root, dan UTC expiry 10 menit, lalu menjalankan `Update.exe uninstall --silent` dengan
nonce yang sama hanya pada inherited environment
`TERMINAL_UNINSTALL_NONCE`. `OnBeforeUninstall` menghapus data hanya bila environment nonce
dan marker cocok, marker valid/unexpired, serta package identity/version/root cocok. Marker invalid,
stale, system-initiated uninstall, atau hook error kembali ke preserve-data default dan menghasilkan
diagnostic, bukan aggressive cleanup.

Sebelum delete, path dibentuk ulang dari `FOLDERID_LocalAppData` dan harus sama ordinal-ignore-case
dengan exact canonical root, bukan hanya prefix. Root yang merupakan unexpected reparse point ditolak;
enumeration tidak mengikuti reparse point dan hanya menghapus link entry. Deletion scope tidak pernah
naik ke `%LOCALAPPDATA%`, `%LOCALAPPDATA%\Yuzha`, repository, nested repo, atau arbitrary user path.
Windows Credential Manager entry hanya dihapus untuk exact target prefix
`Yuzha.Terminal/`; credential provider lain tidak disentuh. Failure menampilkan path yang
tersisa dan tidak mengklaim full deletion.

Setelah normal preserve-data uninstall, Apps entry tidak menawarkan cleanup kedua. Dokumentasi final
menunjukkan exact retained root untuk manual cleanup atau meminta reinstall lalu memakai in-app flow;
uninstaller tidak menghapus data diam-diam agar UI tampak sederhana.

### 25.10 Exact component schema, native flags, dan default visual

Semua component menerima common field berikut dan menolak field lain: `id` required lower-kebab-case;
`type` required enum §9.1; `visible` Boolean default `true`; `enabled` Boolean default `true`; `style`
required explicit `{ "$ref": "styles.<id>" }`; `layout` object default `{}`; `automation` object default
`{}`; `events` object default `{}`. Layout exact: `width|height` adalah `auto|fill|0..8192`, default
`auto`; `minWidth|minHeight` `0..8192`, default `0`; `maxWidth|maxHeight` `0..8192`, default `8192`;
`margin` adalah `{left,top,right,bottom}` `0..256`, seluruh default `0`. String/Boolean/value dinamis
memakai typed `{ "$bind": "viewState.<path>" }`; C++ tidak menerima string binding shorthand.
Automation exact: `name` adalah string/binding atau `"auto"` default; `helpText=""`; `live="off"`
(`off|polite|assertive`). `auto` mengambil label/title/text dan invalid untuk interactive component yang
tidak mempunyai sumber nama. Role/pattern ditentukan type component dan tidak dapat dioverride JSON.

Component-specific field final:

| Type | Field exact dan default |
| --- | --- |
| `Window` | `title` required string/binding; `initialWidth=760`, `initialHeight=520`, `minWidth=620`, `minHeight=420`, `resizable=true`, `children=[]` |
| `Screen` | `routeId` required enum inventory §10; `children=[]` |
| `Container` | `direction=column` (`row|column|grid|flow`), `gap=8` (`0..128`), `padding={0,0,0,0}`, `align=stretch`, `justify=start`, `wrap=false`, `overflow=visible` (`visible|clip|scroll`), `children=[]` |
| `Text` | exactly one `text` or `textBinding`; `variant=body` (`body|title|caption|monospace`), `wrap=true`, `selectable=false`, `align=start` |
| `Button` | `label` required string/binding; `variant=default` (`default|primary|subtle|danger|navigation|bookmark`), `selected=false`/binding, `tabStop=true`; event allowlist `click` |
| `Input` | `valueBinding` required; `mode=singleLine` (`singleLine|multiline`), `placeholder=""`, `readOnly=false`, `password=false`, `maxLength=4096` (`0` berarti unlimited hanya multiline), `horizontalAlign=start`, `scrollbar=auto` (`auto|never`), `tabStop=true`; events `changed|commit|focus|blur` |
| `Combo` | `itemsBinding` dan `selectedValueBinding` required; `placeholder=""`, `maxVisibleItems=10` (`1..50`), `popupMaxHeight=480` (`64..1024`), `allowEmpty=true`, `tabStop=true`; events `changed|opened|closed` |
| `Checkbox` | `label` required, `checkedBinding` required, `triState=false`, `tabStop=true`; event `changed` |
| `Toggle` | `label` required, `checkedBinding` required, `variant=default`, `tabStop=true`; event `changed` |
| `Card` | `interactive=false`, `selected=false`/binding, `children=[]`, `tabStop` default mengikuti `interactive`; event `activate` hanya bila interactive |
| `List` | `itemsBinding` dan `itemTemplate` required; `selectedIdBinding` optional, `rowHeight=32` (`20..256`), `overscanRows=2` (`0..20`), `selection=single` (`none|single`), `emptyText="Tidak ada data"`, `scrollbar=auto`, `tabStop=true`; events `selectionChanged|activate` |
| `Scrollbar` | `orientation=vertical`, `thickness=12` (`8..32`), `minThumbLength=24` (`12..128`), `lineStep=1` (`1..1000`), `pageStep=auto`; value/range dimiliki parent melalui typed internal contract, bukan JSON binding independen |
| `Dialog` | `title` required, `modality=modal` satu-satunya nilai V1, `width=480` (`240..1200`), `maxHeight=720` (`160..1200`), `dismissPolicy={escape:true,outsideClick:false,explicitAction:true}`, `children=[]`; events `accept|cancel|dismiss` |

Composition `children` berisi inline component object dan hanya valid pada Window/Screen/Container/Card/
Dialog. Event value exact `{ "action": "lower-kebab-case", "payload": {} }`; payload hanya literal JSON
atau binding yang schema action tersebut izinkan.

Native flags final:

- top-level route: `WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN`, extended `WS_EX_APPWINDOW`;
- Input base: `WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP`; visible menambahkan `WS_VISIBLE`;
- single-line Input: `ES_LEFT | ES_AUTOHSCROLL`; multiline:
  `ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN`; password menambah `ES_PASSWORD` dan hanya
  valid single-line; read-only diterapkan dengan `EM_SETREADONLY`;
- Input tidak memakai `WS_BORDER`, `WS_EX_CLIENTEDGE`, `WS_VSCROLL`, atau `WS_HSCROLL`;
- Combo popup: `WS_POPUP`, extended
  `WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, owned oleh route top-level dan tidak topmost.

Default metric dari native reference: Segoe UI 9 pt, title 12 pt, Cascadia Mono 10 pt dengan Consolas
fallback; field/button height 32 DIP; normal gap 8 DIP; section gap 16 DIP. Radius exact: default/
primary/subtle/danger Button `6`, navigation/bookmark `8`, Input/Combo `6`, Checkbox `3`, Card `10`,
Dialog `12` DIP.

Default token Light/Dark mengadopsi `Open-terminal-native\Assets\ui.default.json`:

| token | Dark | Light |
| --- | --- | --- |
| window / surface / surfaceAlt | `#14161B` / `#1C1F26` / `#232730` | `#F5F6F9` / `#FFFFFF` / `#F0F2F6` |
| border / borderStrong | `#303540` / `#444B5A` | `#D8DCE3` / `#BAC0CB` |
| text / textDim / textMuted | `#E9EDF4` / `#A8B2C2` / `#788294` | `#181C24` / `#565F6E` / `#7E8796` |
| accent / accentHover / accentPressed / accentText | `#60A5FA` / `#93C5FD` / `#BFDBFE` / `#0F172A` | `#2563EB` / `#1D4ED8` / `#1E40AF` / `#FFFFFF` |
| danger / success / input / selection / scrim | `#EF6060` / `#4AC48C` / `#181B21` / `#263E64` / `#00000099` | `#CB3737` / `#1C9460` / `#FFFFFF` / `#CDE0FC` / `#00000066` |

Button state mapping exact `normal; hover; pressed; selected; disabled; focus`:

- default/bookmark: `surface/text/border; surfaceAlt/borderStrong; selection/accent;
  selection/accent; surfaceAlt/textMuted; accent`;
- primary: `accent/accentText/accent; accentHover/accentText/accentHover;
  accentPressed/accentText/accentPressed; accentPressed/accentText/accentPressed;
  surfaceAlt/textMuted/border; accentHover`;
- subtle/navigation: `transparent/textDim/transparent; surfaceAlt/text/transparent;
  selection/text/accent; selection/text/accent; transparent/textMuted/transparent; accent`;
- danger: `danger/accentText/danger; danger/accentText/borderStrong; selection/text/accent;
  selection/text/accent; surfaceAlt/textMuted/border; danger`.

High Contrast mapping exact: window/surface/input → `COLOR_WINDOW`; text → `COLOR_WINDOWTEXT`; muted/
disabled → `COLOR_GRAYTEXT`; accent/selection/focus → `COLOR_HIGHLIGHT`; accentText →
`COLOR_HIGHLIGHTTEXT`; border → `COLOR_WINDOWTEXT`. Decorative alpha/tint/shadow dimatikan.

`allowMultiple`, RTL mirroring, custom JSON text editor, dan automatic crash-loop rollback tetap post-V1.

### 25.11 Evidence, dokumentasi, dan Git

- Auto-update evidence dibaca dari `Open-terminal\TerminalChooser.csproj`, `.config\dotnet-tools.json`,
  `Features\Updates\AppUpdateService.cs`, `Program.cs`, `tools\Build-Release.ps1`, dan
  `.github\workflows\release.yml`; GitHub release `v0.3.0` dan `v0.3.1` serta successful release run
  membuktikan pattern local/GitHub feed dan N→N+1. Nested repo tetap tidak berubah.
- Evidence snapshot 2026-08-14: release workflow run `30764401190` berstatus success; `v0.3.1`
  mempublikasikan Setup, portable ZIP, full/delta package, `RELEASES`, serta `releases.win.json`.
  Setup SHA-256 adalah `3FBA5EB2A944C0CA33E362DA1816465F34627730B9EAB30B1B7541C3721C486E`, dan install lokal
  mempunyai `current`, `packages`, launcher, serta `Update.exe`. Snapshot ini membuktikan reference
  deployment, bukan acceptance greenfield.
- `Open-terminal-native` memberi pola raw-Win32/MSBuild `/MT /W4`, lazy route/tray, embedded config,
  native Edit, GDI `BufferedPaint`, cached antialiased-corner tile, pixel/smoke scripts, dan measurement
  baseline. V1 mengadopsi pattern tersebut ke ownership/schema baru tanpa dependency ke nested repo.
- Measurement native mencatat GDI cached-AA sekitar `0.0304 ms` per owner-draw control dan menolak
  per-control hardware `DCRenderTarget`; keputusan final V1 adalah GDI DIB/BitBlt §25.3. Tidak ada probe
  untuk memilih backend lain.
- Root sengaja tidak memakai `AGENTS.md` atau implementation-plan kedua; dokumen ini authoritative.
- Root `.gitignore` sudah mencakup native MSVC intermediate, dependency/build/artifact cache, package,
  installer/feed, binary, log, ETL/CSV, dan test temp. Tracked scripts/manifests/lock files tetap tidak
  di-ignore.
- Perubahan plan belum otomatis menjadi Git history. Commit/push/release hanya atas instruksi user.

### 25.12 Post-completion developer playbook dan skill

1. `Playbook.md` menjadi panduan operasional untuk menambah screen, component, navigation binding,
   action handler, business service, tests, serta smoke tanpa memilih ulang arsitektur.
2. Selama Phase 4 dan Phase 5, perubahan schema/event/bridge/route/build contract wajib memperbarui
   playbook pada perubahan yang sama agar contoh tidak drift dari runtime aktual.
3. Hanya setelah exit criteria Phase 5 benar-benar PASS, turunkan playbook yang telah terbukti dipakai
   menjadi repo-specific skill untuk menghasilkan skeleton screen JSON, navigation button, feature
   action registration, dan test checklist. Skill harus membaca contract aktual, tidak mengedit nested
   reference repository, tidak melakukan commit/push, dan tidak menjadi architecture/implementation
   plan kedua.
4. Skill dinyatakan siap hanya jika output sample-nya lolos config resolution, Debug/Release contract
   tests, `git diff --check`, dan runtime route smoke. Pembuatan skill adalah tooling pasca-completion,
   bukan blocker exit criteria V1.
