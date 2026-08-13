# Open Terminal Greenfield Native — Locked Architecture and Delivery Plan

Status: **keputusan arsitektur dikunci; implementasi belum dimulai**

Repository canonical: `C:\VSCODE\Teminal`

Tanggal konsolidasi: 2026-08-14

Dokumen ini menggabungkan seluruh keputusan yang sudah dikunci dalam pembahasan. Jika detail belum
pernah diputuskan, dokumen ini menandainya sebagai open decision agar agent tidak mengarang atau
memperluas scope secara diam-diam.

## 1. Tujuan

Membangun aplikasi Open Terminal native baru sebagai greenfield C++20 raw Win32 yang:

- ringan dan menampilkan first frame dengan cepat;
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
  inventory component, dan kontrak business yang perlu dipertahankan kelak.
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
- UI: native HWND/GDI/Win32, tanpa browser engine.
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
- DPI scaling, clipping, invalidation, buffered paint, GDI resource lifecycle, dan cleanup;
- algoritma teknis seperti vertical centering dan union antara old/new dirty rectangle;
- safety checks dan kegagalan yang eksplisit.

Rumus atau algoritma boleh berada di C++; input visual dan hasil desainnya berasal dari JSON.

## 7. Schema JSON baru

### 7.1 Kontrak yang sudah dikunci

- Schema mempunyai identity baru dan `version: 1`.
- Schema minimal mengikuti kebutuhan nyata V1 dan harus dapat ditambah tanpa merusak component lama.
- Dokumen default lengkap di-embed ke executable.
- Optional user override baru diterapkan di atas default baru hanya setelah seluruh override lolos
  parse, schema validation, dan reference validation. Override tidak pernah diterapkan sebagian.
- Gate menghasilkan typed resolved document; tidak ada string token lookup atau JSON parsing di paint.
- Invalid schema/reference menghasilkan diagnostic jelas dan tidak diam-diam memakai fallback visual
  hardcoded.
- File UI lama tidak dibaca, diubah, atau dimigrasikan.

Bentuk tingkat atas yang dimaksud:

```json
{
  "schema": "open-terminal-greenfield-ui",
  "version": 1,
  "tokens": {},
  "styles": {},
  "windows": {},
  "screens": {}
}
```

Contoh tersebut menetapkan boundary, bukan mengunci semua nama field turunannya. Schema rinci tumbuh
di Phase 1-3 berdasarkan kebutuhan component dan placeholder screen nyata, bukan sebagai form engine
generik. Contract setiap component harus dibekukan dan diuji sebelum implementation component itu
dimulai; tidak perlu mengarang seluruh field 13 component sebelum vertical slice pertama.

### 7.2 Grammar dan aturan evolusi yang dikunci

- Dokumen menggunakan JSON UTF-8.
- Nama field memakai `lowerCamelCase`. Stable config ID dan route ID memakai `lower-kebab-case`.
- Nilai ukuran/layout numerik memakai logical pixel pada baseline 96 DPI. C++ mengubahnya ke physical
  pixel saat runtime; JSON tidak menyimpan physical pixel hasil scaling.
- Warna literal memakai `#RRGGBB` atau `#RRGGBBAA`. Preferensi utama tetap token reference.
- Reference memakai object eksplisit `{ "$ref": "tokens.<path>" }`; string biasa tidak ditafsirkan
  diam-diam sebagai token.
- Unknown field, duplicate key, unknown component type, missing reference, reference cycle, salah
  type, dan nilai di luar range ditolak sebagai validation error; tidak diabaikan diam-diam.
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
    → tidak membaca file
    → tidak parse JSON
    → tidak resolve string token
```

## 8. UiConfigGate

`UiConfigGate` adalah satu-satunya pintu JSON menuju UI runtime.

Tanggung jawab:

- membaca embedded default dan override baru;
- parse dan validate schema/version;
- memvalidasi component, window, screen, style, token, action, dan navigation references;
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
- Error override harus terlihat sekali secara langsung dan tetap tercatat sebagai config diagnostic
  aktif pada Settings/UI Editor sampai reload berikutnya berhasil. Jangan menerapkan sebagian override
  dan jangan menyamarkan kegagalan sebagai keberhasilan.
- Diagnostic minimal membawa file/source identity, schema path atau JSON location bila tersedia,
  error code/category, dan pesan yang dapat ditindaklanjuti tanpa membocorkan secret.

Exact bentuk visual diagnostic persisten dan lokasi log diagnostic masih belum pasti; keduanya dicatat
di bagian keputusan terbuka dan tidak boleh ditebak oleh implementation agent.

### 8.2 Reload V1

- Reload V1 hanya manual dan eksplisit; tidak ada file watcher.
- UI Editor menggunakan entry point reload `UiConfigGate` yang sama, bukan jalur parse khusus.
- Reload valid mempublikasikan generation baru secara atomik.
- Reconciliation mempertahankan focus, scroll, dan unsaved draft berdasarkan stable window/screen/
  component identity yang masih ada. State untuk identity yang hilang dibuang secara deterministik.
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
- `Dialog`.

Jenis/variant baru harus bisa didaftarkan melalui registry/factory tanpa membuat central widget
dispatcher membesar.

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

- Input memiliki seluruh focus/blur, native edit behavior, vertical alignment, selection, frame,
  resize invalidation, dan paint state Input.
- Button memiliki seluruh hover, pressed, selected, disabled, keyboard focus, capture, dan paint
  state Button.
- Combo memiliki dropdown/selection/arrow/hover/focus dan paint miliknya sendiri.
- Checkbox, Toggle, Card, List, Dialog, Text, Screen, Window, dan Container memiliki logic/UI khususnya
  sendiri.
- Tidak boleh ada logic khusus Input di container, logic Button di screen, atau dispatcher besar yang
  mengetahui internal seluruh component.

### 9.3 Container adalah component

Container boleh dan harus memiliki logic UI yang memang miliknya:

- background dan border container;
- padding dan gap;
- row/column/grid/flow child layout;
- clipping, scrolling, z-order, resize, dan invalidation;
- membuat/menghancurkan child melalui registry/factory;
- meneruskan event tanpa mengambil alih internal logic child.

Container tidak menjalankan terminal, scan Chrome, persistence, atau business validation.

### 9.4 Shared native primitives

Shared primitives hanya menyediakan mekanisme generik:

- GDI resource ownership;
- buffered painting;
- rounded drawing;
- DPI scaling;
- font/text measurement;
- rectangle math;
- clipping;
- invalidation union;
- native helper yang tidak mengetahui jenis component.

Shared primitive tidak boleh berisi cabang `if Input`, `if Button`, atau aturan visual role tertentu.

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

#### Button

- pressed memakai accent fill yang lebih kuat daripada hover;
- nilai hover dan pressed ditulis eksplisit serta independen pada JSON untuk setiap Button variant;
- C++ tidak menghitung hover sebagai persentase pressed atau sebaliknya;
- hover dan pressed memakai solid accent border;
- keyboard focus memakai solid focus outline, bukan dashed/dotted Win32 focus rectangle;
- nilai warna, campuran accent, ketebalan, dan radius berasal dari token/style JSON, bukan constant di
  Button C++.

Angka lama `35%/25% accent` bukan tabel canonical yang dapat direkonstruksi dan tidak boleh disebut
sebagai mapping referensi. Exact default tiap variant adalah keputusan desain baru di Phase 1. Yang
sudah dikunci hanya hubungan visual `normal < hover < pressed` dan seluruh nilainya harus tampak
eksplisit di JSON agar tidak ada visual policy tersembunyi dalam C++.

### 9.6 Model HWND V1 yang dikunci

V1 memakai model hybrid, bukan `HWND` untuk setiap node dan bukan satu canvas owner-drawn untuk semua
component:

- setiap runtime component adalah object dengan stable runtime identity dan boleh memiliki nol, satu,
  atau beberapa `HWND` sesuai native capability yang dibutuhkan;
- top-level `HWND` dimiliki `WindowContainer`; `WindowComponent` adalah definisi/composition window,
  bukan pemilik top-level window kedua;
- `DialogComponent` memiliki owned top-level/popup `HWND` ketika dialog ditampilkan;
- `InputComponent` memiliki host/frame miliknya serta native Edit child agar text input, selection,
  clipboard, IME, dan accessibility tetap native;
- `ComboComponent` memiliki host dan native dropdown child yang diperlukan;
- Button, Checkbox, Toggle, dan List yang interaktif memiliki component-owned child window atau
  registered custom child window sehingga focus, keyboard, accessibility, capture, paint, dan cleanup
  tetap dimiliki component tersebut;
- Text, Card, Screen, dan Container tidak diwajibkan memiliki child `HWND`; mereka dapat di-compose dan
  digambar pada host terdekat selama clipping, hit testing, invalidation, dan accessibility contract
  tetap terpenuhi;
- shared window-procedure trampoline hanya meneruskan message ke instance owner dan tidak bercabang
  berdasarkan component type.

Component yang memiliki beberapa native window tetap satu component dan seluruh creation, subclassing,
message handling, layout internal, paint, serta destruction-nya berada di `.cpp` component tersebut.
Khusus Input, frame/outline hanya digambar sekali oleh host `InputComponent`, bukan kembali digambar
oleh Container atau native Edit child.

Exact Win32 class/style flags dan pilihan native-versus-registered child untuk Button, Checkbox,
Toggle, dan List divalidasi pada vertical slice. Detail itu boleh disesuaikan tanpa mengubah model
ownership hybrid di atas.

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
- draft/status/scroll/focus milik instance screen/window;
- screen tidak membaca storage business dan tidak menjalankan business service pada stub phase;
- penambahan screen baru dilakukan melalui config + component/action registration yang eksplisit,
  bukan menambah logic ke dispatcher global.

## 11. ApplicationContainer

`ApplicationContainer` adalah container tingkat proses, satu instance per proses.

Tanggung jawab:

- bootstrap UI shell setelah config berhasil di-resolve;
- menerima initial route dan command dari second launch, Jump List, atau taskbar;
- memakai Terminal sebagai default route ketika startup tidak membawa route valid;
- memiliki registry seluruh top-level window;
- create, find, activate, reuse, dan close window;
- membuat satu `WindowContainer` per top-level window;
- menerapkan V1 `reuse-per-route` untuk external/taskbar route: aktifkan matching window yang sudah
  ada atau buat satu bila belum ada;
- mengelola tray lifetime;
- tetap hidup di tray setelah window terakhir ditutup;
- keluar penuh hanya melalui explicit Exit action.

Larangan:

- tidak menjalankan terminal atau Chrome;
- tidak scan profile;
- tidak membaca/menulis business settings;
- tidak berisi component-specific logic;
- tidak menjadi service locator untuk business logic.

### 11.1 Tray lifecycle V1

- Klik kiri tray icon mengaktifkan dan mengembalikan window yang terakhir aktif.
- Bila tidak ada window tersisa, klik kiri membuat window baru dengan default route Terminal.
- Klik kanan membuka native context menu yang minimal berisi daftar route yang boleh dibuka serta
  explicit Exit.
- Menutup visible window terakhir hanya menyembunyikan/menutup window tersebut setelah tray icon
  dipastikan tersedia.
- Jika `Shell_NotifyIcon` gagal, aplikasi tidak boleh membuat dirinya tidak dapat dijangkau: visible
  window terakhir tetap terbuka dan diagnostic ditampilkan.
- Aplikasi menangani registered `TaskbarCreated` message untuk memasang kembali tray icon setelah
  Windows Explorer restart.
- Explicit Exit menutup seluruh window, melepas tray icon, menyelesaikan cleanup, lalu mengakhiri
  proses.

## 12. WindowContainer

Setiap top-level native window memiliki satu `WindowContainer`.

Tanggung jawab:

- HWND, frame, background, navigation, dan content bounds window itu;
- assembly screen/component tree dari resolved document;
- outer layout, resize, DPI, clipping, child lifetime, dan repaint;
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
Open Terminal process
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
- dialog yang sedang terbuka;
- input atau editor draft yang belum disimpan;
- preview/draft UI Editor;
- generation yang diperlukan untuk mengabaikan async result yang sudah stale bagi window tersebut.

Prinsip penentu: data yang harus konsisten di semua window menjadi shared application state; data
interaction/draft yang hanya masuk akal untuk satu window tetap dimiliki window/component tersebut.

### Theme V1 yang dikunci

- V1 menyediakan `System`, `Dark`, dan `Light`; default pertama adalah `System`.
- JSON menyediakan token/style set Dark dan Light. `System` memilih salah satunya berdasarkan Windows
  app theme; C++ hanya membaca perubahan OS dan memilih resolved set, bukan menentukan warna.
- Perubahan theme berlaku untuk seluruh window dalam satu config generation dan menjadi shared
  application setting ketika persistence business sudah dipasang.
- Theme change harus memperbarui native control colors, non-client/client painting yang dimiliki app,
  focus visuals, dialog, dan tray menu yang dapat dikontrol tanpa restart aplikasi.
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

Pada stub phase dilarang:

- membuka PowerShell/WSL/Chrome;
- scan Chrome atau start/resolve WSL;
- membaca atau menulis settings business;
- menyentuh provider/API key nyata;
- melakukan network request;
- menjalankan updater dari component/business stub atau plugin discovery; packaging/update harness yang
  terpisah tetap boleh menguji instalasi serta perpindahan build;
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

Business code baru di root tidak boleh mempunyai dependency build/runtime pada nested repository.
Nested code hanya boleh menjadi referensi untuk port/reimplementation yang dilakukan secara eksplisit
pada business phase.

## 19. Packaging, installer, updater, dan release V1

Packaging bukan pekerjaan opsional atau post-V1. Aplikasi harus diuji sebagai installed application
sejak executable runnable pertama tersedia dan harus mendukung perpindahan dari installed build lama
ke build baru.

Kontrak yang dikunci:

- Installer adalah jalur utama user testing; menjalankan loose EXE hanya supplemental developer smoke.
- V1 menghasilkan installer untuk clean install serta update artifact/feed metadata untuk upgrade.
- Installed build mempunyai product identity, application ID, install scope, data directory, dan
  version identity yang stabil lintas update.
- Update `N → N+1` mengganti program files secara aman tanpa menghapus atau menimpa user UI override,
  settings, cache, bookmark/history, draft yang memang persisted, atau credential storage.
- File runtime milik versi berbeda tidak boleh bercampur. Update gagal atau dibatalkan harus
  meninggalkan versi lama tetap dapat dijalankan.
- Package/update metadata harus diverifikasi sebelum binary diganti. Exact signing/integrity mechanism
  mengikuti updater stack yang nanti dipilih.
- Update check, download, dan staging tidak boleh menahan first frame atau berjalan di UI thread.
- Updater coordination berada di application/deployment service dan hanya mengirim status/action
  semantic melalui `UiApplicationBridge`; component dan container tidak mengelola file update.
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

Installer/updater stack, install scope, update transport/feed, channel, signing certificate, release
host, rollback policy, dan detail uninstall user-data belum pasti. Semuanya harus diputuskan sebelum
packaging implementation; agent tidak boleh memilih atau memasang dependency sendiri.

## 20. Target source ownership dan nama final

Konvensi nama dikunci: file memakai `snake_case`, class/type memakai `PascalCase`, dan setiap class
boleh mempunyai header contract serta tepat satu `.cpp` implementation utama. Nama inti final:

| Tanggung jawab | File | Class/type |
| --- | --- | --- |
| process-level UI lifecycle | `src/application/application_container.{h,cpp}` | `ApplicationContainer` |
| UI/business boundary | `src/application/ui_application_bridge.{h,cpp}` | `UiApplicationBridge` |
| deterministic test application | `src/application/stub_application_bridge.{h,cpp}` | `StubApplicationBridge` |
| satu-satunya JSON gate | `src/ui/config/ui_config_gate.{h,cpp}` | `UiConfigGate` |
| immutable typed UI config | `src/ui/config/resolved_ui_document.{h,cpp}` | `ResolvedUiDocument` |
| one top-level window owner | `src/ui/container/window_container.{h,cpp}` | `WindowContainer` |
| component creation/registration | `src/ui/registry/component_registry.{h,cpp}` | `ComponentRegistry` |

Nama component final mengikuti pola `<name>_component.{h,cpp}` dan `<Name>Component`: `WindowComponent`,
`ScreenComponent`, `ContainerComponent`, `TextComponent`, `ButtonComponent`, `InputComponent`,
`ComboComponent`, `CheckboxComponent`, `ToggleComponent`, `CardComponent`, `ListComponent`, dan
`DialogComponent`.

Struktur target:

```text
C:\VSCODE\Teminal\
├── Assets\
│   └── ui\                         # new schema/default documents only
├── packaging\                     # manifests/scripts after packaging stack is approved
├── src\
│   ├── main.cpp                    # minimal process bootstrap
│   ├── application\
│   │   ├── application_container.*
│   │   ├── ui_application_bridge.*
│   │   └── stub_application_bridge.*
│   └── ui\
│       ├── config\
│       │   ├── ui_config_gate.*
│       │   └── resolved_ui_document.*
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
│           └── dialog\dialog_component.*
└── tests\
```

Ini menetapkan ownership dan penamaan final untuk V1. Nama interface/helper teknis tambahan ditentukan
hanya ketika kontraknya nyata. Jangan membuat directory kosong sebelum phase yang benar-benar
memerlukannya, dan jangan membuat framework abstraksi spekulatif.

## 21. Urutan implementasi

### Phase 0 — Preflight dan contract freeze

1. Verifikasi root Git dan dirty worktree.
2. Baca `AGENTS.md` dan plan ini.
3. Inventarisasi behavior reference secara read-only dan hanya sejauh kebutuhan phase.
4. Tetapkan nama product/executable/solution dan path config baru.
5. Tetapkan build system/toolchain, JSON parser, dan test framework tanpa mengimpor project lama.
6. Tetapkan installer/updater stack, version identity, install scope, test feed/channel, integrity/
   signing policy, dan artifact commands.
7. Buat acceptance checklist serta exact validation command phase sebelum menulis source.

Exit criteria: semua keputusan pada bucket "wajib sebelum coding" sudah diputuskan; scope, files,
dependency, packaging route, dan validation command diketahui; nested repositories tidak berubah.

### Phase 1 — Schema dan gate

1. Definisikan schema V1 minimal untuk vertical slice berdasarkan grammar yang sudah dikunci, lalu
   tambahkan contract component secara bertahap sebelum component tersebut diimplementasikan.
2. Buat new embedded default document dan optional new override identity/path.
3. Implement parse, validation, reference checking, token resolution, merge, diagnostic, dan typed
   `ResolvedUiDocument`.
4. Implement whole-override rejection, last-known-good reload, bootstrap failure, serta manual reload.
5. Tambahkan tests untuk valid, invalid, duplicate/unknown field, missing/cyclic reference, merge/
   replacement semantics, version, override rejection, reload, dan no-legacy-import.

Exit criteria: schema dapat di-resolve tanpa HWND dan tidak pernah membaca legacy `ui.json`.

### Phase 2 — Native primitives dan vertical component slice

1. Buat primitive minimum untuk DPI, font measurement, color/drawing, clipping, buffered paint, dan
   invalidation.
2. Buat component contract serta registry/factory.
3. Implement `Window`, `Container`, `Text`, `Button`, dan `Input` sebagai vertical slice pertama.
4. Jalankan executable placeholder dari JSON untuk membuktikan create/layout/paint/event/patch.
5. Buat installer preview pertama dan jalankan smoke dari installed path, bukan hanya build tree.

Exit criteria: installed test app tampil dari JSON, tidak memiliki hidden visual constant atau
business side effect, dan dapat di-uninstall tanpa merusak user data di luar scope aplikasi.

### Phase 3 — Component V1 lengkap

1. Tambahkan Combo, Checkbox, Toggle, Card, List, Dialog, dan Screen.
2. Pastikan setiap component memiliki seluruh logic/UI internal miliknya.
3. Uji normal/hover/pressed/focused/selected/checked/disabled, keyboard, DPI, resize, repaint, dan
   cleanup sesuai capability masing-masing.

Exit criteria: tidak ada central widget dispatcher dan component baru dapat diregistrasikan secara
lokal.

### Phase 4 — WindowContainer, ApplicationContainer, multi-window, dan tray

1. Assemble named placeholder screens dari JSON.
2. Implement per-window route/state di `WindowContainer`.
3. Implement process window registry dan external route handling di `ApplicationContainer`.
4. Implement `reuse-per-route` untuk external/taskbar command.
5. Implement close-one-window, last-window-to-tray, activate existing route, dan explicit Exit.

Exit criteria: Terminal dapat tetap terbuka ketika Chrome Launcher muncul di top-level window kedua;
tidak ada accidental duplicate untuk external route.

### Phase 5 — Stub application dan installed-update validation gate

1. Lengkapi semua placeholder screen dan deterministic data.
2. Validasi setiap navigation binding, UiEvent, bridge route, dan UiPatch/ViewState.
3. Jalankan Windows visual/runtime smoke untuk System/Dark/Light, DPI, resize, focus, keyboard,
   repaint/ghosting, multi-window, taskbar route, serta tray lifecycle.
4. Hasilkan dua versioned preview build `N` dan `N+1`, clean-install `N`, lalu update installed app ke
   `N+1` menggunakan jalur update yang direncanakan.
5. Verifikasi executable/version benar-benar berubah, aplikasi tetap launchable, dan config/user data
   yang disiapkan pada `N` tetap tersedia pada `N+1`.
6. Uji update gagal/ditolak, uninstall, Explorer restart, dan update ketika aplikasi masih berjalan
   sesuai contract updater yang dipilih.
7. Audit bahwa tidak ada business side effect dan tidak ada dependency ke nested repository.

Exit criteria: UI runtime dinyatakan PASS hanya jika build, contract tests, visible Windows smoke,
clean install, dan `N → N+1` installed update lulus. Jika visible/install/update smoke tidak tersedia,
verdict maksimal PARTIAL.

### Phase 6 — Business integration, terpisah

Phase ini dimulai hanya setelah stub UI disetujui user.

1. Petakan action/view-state contract satu feature.
2. Buat adapter business di root tanpa UI/HWND dependency.
3. Ganti stub handler feature tersebut.
4. Verifikasi behavior parity dan regression.
5. Hasilkan serta uji installer/update artifact untuk accepted build.
6. Lanjutkan feature berikutnya hanya setelah feature aktif lulus.

Urutan feature integration belum dikunci dan harus ditetapkan berdasarkan dependency live sebelum
Phase 6 dimulai.

### Phase 7 — Release readiness, cutover, dan legacy retirement

1. Jalankan full Release x64 build, package, clean-install, upgrade dari accepted build sebelumnya,
   uninstall, checksum/integrity, version, shortcut/taskbar/tray, dan release-note validation.
2. Jadikan UI baru entrypoint canonical setelah seluruh integration yang disetujui lulus.
3. Pastikan release/update artifact dapat dipublikasikan melalui workflow yang sudah dipilih; actual
   publication tetap menunggu instruksi eksplisit user.
4. Penghapusan legacy code/reference hanya dengan otorisasi eksplisit dan cleanup plan tersendiri.

## 22. Acceptance criteria global

### Repository

- Semua source aplikasi baru berada di root repository.
- Nested repositories tetap bersih dan tidak menjadi dependency.
- Tidak ada credential atau binary build di Git.

### Config-driven UI

- Seluruh screen dapat dirakit dari JSON baru.
- Mengubah supported visual/layout field di override baru mengubah UI tanpa mengubah component C++.
- Tidak ada legacy UI schema/import.
- Tidak ada file/JSON access di paint hot path.
- Invalid config/reference menghasilkan diagnostic yang dapat ditindaklanjuti.
- Invalid override ditolak seluruhnya dan tidak mengganti last-known-good UI.
- Invalid embedded default gagal sebelum main UI dengan bootstrap diagnostic dan non-zero exit.
- Reload hanya terjadi atas tindakan eksplisit dan melakukan atomic generation swap.

### Component ownership

- Satu `.cpp` utama per jenis component dengan blok lifecycle, logic, dan UI.
- Tidak ada component-specific logic di luar pemiliknya.
- Container hanya mengetahui contract child, bukan internal child.
- Shared primitive tidak bercabang berdasarkan component type.
- Penambahan component baru tidak membutuhkan modifikasi dispatcher widget global.
- Setiap native child/window dimiliki dan dibersihkan component yang membuatnya sesuai hybrid HWND
  ownership contract.

### Runtime dan visual

- First frame tidak menunggu scan, network, WSL, plugin, atau business file operation.
- Focus, hover, pressed, selected, disabled, keyboard, dan accessibility tetap bekerja.
- Resize/DPI tidak menimbulkan border ghosting, stale pixels, overlap, atau layout corruption.
- Input mempunyai satu continuous outline di bounds-nya, focused outline solid-accent lebih tebal,
  single-line text vertically centered, multiline top-aligned, dan seluruh text left-aligned.
- Button mempunyai hubungan normal < hover < pressed yang konsisten, solid accent border saat
  hover/pressed, solid focus outline tanpa dashed/dotted ring, serta nilai hover/pressed eksplisit dan
  independen di JSON.
- System/Dark/Light menghasilkan token set yang benar di seluruh top-level window tanpa restart.
- Paint tidak melakukan parse/config IO.

### Multi-window dan tray

- Satu proses dapat memiliki minimal dua independent top-level window.
- External Chrome Launcher route tidak mengganti Terminal window yang sudah terbuka.
- External route mengaktifkan matching window yang sudah ada atau membuat satu bila belum ada.
- Menutup satu window tidak menutup window lain.
- Menutup window terakhir membuat aplikasi tetap hidup di tray.
- Klik kiri tray memulihkan last-active window atau membuat Terminal window bila tidak ada.
- Klik kanan tray menyediakan route dan Exit; Explorer restart memasang kembali icon.
- Kegagalan tray tidak pernah meninggalkan proses hidup tanpa visible/reachable window.
- Exit eksplisit menutup proses dengan cleanup yang benar.

### Stub/business boundary

- Stub menyediakan deterministic data dan tidak memiliki business side effect.
- Semua action/patch melewati typed bridge.
- Setiap `UiEvent` dan `UiPatch/ViewState` membawa `UiAddress` dengan mandatory window, screen, serta
  component runtime identity; stale/missing target ditolak.
- Component/container tidak menjalankan business operation.
- Business integration kelak tidak mengenal JSON/HWND/paint/layout.

### Installer, updater, dan release artifact

- Preview pertama dan setiap accepted build setelahnya dapat dihasilkan sebagai installer.
- Clean install menjalankan binary dari installed location dengan product/version identity yang benar.
- Installed build `N` dapat diperbarui ke `N+1` dan benar-benar menjalankan `N+1`.
- Update mempertahankan user data dan tidak mencampur program files antarversi.
- Update invalid/gagal tidak merusak versi terpasang yang masih valid.
- Update check/download tidak menahan first frame atau UI thread.
- Uninstall membersihkan integration/program files sesuai contract tanpa menghapus user data diam-diam.
- Generated installer/update/release artifacts tidak tracked di source Git.

## 23. Validation minimum

Command final ditemukan dari project file baru setelah scaffold. Minimum setiap implementation phase:

- `git diff --check`;
- build Debug x64;
- build Release x64;
- test schema/parser/resolution;
- test component/event/patch contracts yang terkena;
- Windows smoke untuk behavior runtime/visual yang berubah;
- clean-install smoke dari artifact, bukan build directory;
- installed update smoke dari version `N` ke version `N+1`;
- preservation check untuk config/settings/cache/user data yang relevan;
- failed/invalid update dan uninstall smoke;
- package version, manifest/checksum/signature, shortcut, taskbar, tray, dan installed-path checks sesuai
  stack yang disetujui;
- pemeriksaan bahwa nested repositories tidak berubah;
- pemeriksaan bahwa artefak binary tidak staged/tracked.

Jangan mengklaim visual PASS hanya dari build atau unit test. Visual PASS membutuhkan aplikasi Windows
yang benar-benar terlihat pada state dan viewport/DPI yang relevan.

## 24. Di luar scope UI/stub phase

- migration atau compatibility dengan UI lama;
- import/link/dependency terhadap nested repository;
- terminal/Chrome/WSL launch nyata;
- scan profile nyata;
- settings/provider/API key persistence nyata;
- network request business yang tidak diperlukan installer/updater;
- plugin discovery atau marketplace;
- penghapusan implementasi lama;
- business rule baru atau perubahan behavior business lama.

Packaging, installer, installed-update validation, dan release-artifact generation adalah pengecualian
yang sengaja masuk scope V1. Public publication tetap membutuhkan instruksi user tersendiri.

## 25. Keputusan yang masih belum pasti

Bagian ini adalah note uncertainty, bukan izin bagi agent untuk memilih diam-diam.

### 25.1 Wajib diputuskan pada Phase 0 sebelum implementation source dimulai

- nama final product, executable, solution, application ID, publisher identity, dan version convention;
- build system/generator, exact MSVC/Windows SDK baseline, dependency policy, dan project layout final di
  luar ownership tree konseptual;
- JSON parser/library beserta cara dependency di-pin dan didistribusikan;
- test framework/runner serta command Debug/Release/test canonical;
- nama/resource ID embedded default JSON dan exact user override/config/data path baru;
- installer/updater technology, install scope (`per-user` atau `per-machine`), privilege model, dan
  supported Windows baseline;
- update feed/transport untuk local-preview dan release, channel model, version comparison, integrity/
  signing mechanism, certificate availability, serta release host;
- exact config diagnostic persistence: lokasi log dan bentuk indikator tetap pada Settings/UI Editor.

Semua item di bucket ini memengaruhi dependency, persistence, installed identity, atau validation
command. Agent harus melakukan discovery/proposal lalu memperoleh keputusan user; tidak boleh mulai
scaffold dengan pilihan default pribadi.

### 25.2 Diputuskan sebelum phase/component terkait

- exact nama/type/range/default field schema untuk setiap component; grammar global sudah dikunci,
  tetapi field tumbuh per vertical slice;
- exact token/color/fill/border values hover dan pressed untuk setiap Button variant. Ini desain baru,
  bukan pencarian mapping legacy `35%/25%`;
- exact native class/style flags dan native-versus-registered child choice untuk Button, Checkbox,
  Toggle, dan List, tanpa mengubah hybrid HWND ownership;
- Windows High Contrast behavior dan batas native non-client area yang dapat ditemakan aplikasi;
- exact same-window navigation behavior ketika target route sudah aktif pada window lain;
- update UX: manual check atau automatic check, frequency, download consent, restart/apply prompt,
  foreground-process coordination, rollback depth, dan offline-installer requirement;
- uninstall user-data option dan retention period untuk staged update/rollback files;
- urutan feature business integration pada Phase 6;
- kebijakan `allowMultiple` untuk route yang sama setelah V1.

Item phase-specific harus diputuskan sebelum contract atau acceptance test phase tersebut ditulis.
Keputusan yang sudah dikunci di bagian lain tidak boleh dibuka kembali tanpa instruksi user.

### 25.3 Note dokumentasi dan Git

- `Termial-plan.md` adalah sumber keputusan arsitektur/delivery paling lengkap, tetapi file tetap harus
  dibaca bersama root `AGENTS.md` sampai user mengotorisasi peringkasan `AGENTS.md`.
- Perubahan plan ini belum otomatis menjadi Git history. Commit hanya dilakukan atas instruksi user.
