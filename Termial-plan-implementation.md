# Open Terminal Native — Implementation Plan

**Sumber:** `Termial-plan.md` — canonical architecture & delivery plan, dikunci per 2026-08-14
**Status dokumen ini:** turunan implementasi (derived), breakdown actionable dari §21 canonical plan,
disilangkan dengan acceptance criteria (§22), validation minimum (§23), dan open contracts (§25).
**Dokumen ini tidak mengubah satu pun keputusan arsitektur.** Kalau ada yang kelihatan bertentangan,
`Termial-plan.md` yang menang — dokumen ini cuma reorganisasi jadi checklist yang bisa dieksekusi.

## Cara pakai

- Setiap phase punya: tugas (checklist), open contract yang harus dibekukan (kalau ada), validation
  minimum yang relevan, dan exit criteria.
- Ikuti dependency gate pada roadmap, bukan urutan nomor secara buta. Phase 0A wajib selesai sebelum
  source pertama; Phase 1 boleh mulai setelah 0A. Track contract Phase 0B boleh berjalan paralel setelah
  0A, sedangkan integrasi source/installer-nya dilakukan setelah entrypoint runnable dan wajib selesai
  paling lambat pada Phase 2.12. Phase lain tidak boleh melewati blocker yang tercantum tanpa evidence
  nyata (build log, screenshot, test result).
- Nomor tugas (mis. `2.7`) mengikuti urutan langkah asli di §21 canonical plan, supaya gampang
  di-cross-reference kalau butuh detail lengkap.
- Bagian "Open contracts" bukan pertanyaan buat dilempar ke user. Sesuai §25: implementer/agent bikin
  rekomendasi konkret sendiri, catat evidence, lalu bekukan — kecuali kalau perubahan menyentuh product
  scope, business semantics, dependency class, privilege, persistence user, atau publication/signing.

---

## Ringkasan roadmap

| Phase | Fokus | Blocker untuk |
|---|---|---|
| 0A | Freeze contract sebelum source pertama | Semua phase |
| 0B | Freeze packaging/update contract; integrasikan setelah entrypoint tersedia | Phase 2 step 12 |
| 1 | Schema JSON + `UiConfigGate` | Phase 2 |
| 2 | Native primitives + vertical slice (Window/Container/Text/Button/Input) | Phase 3A |
| 3A | Component dasar + Scrollbar + basic UIA | Phase 3B, Phase 4 |
| 3B | Combo popup + Dialog overlay + List virtualization + advanced UIA | Phase 4 |
| 4 | WindowContainer, ApplicationContainer, multi-window, tray | Phase 5 |
| 5 | Stub app lengkap + installed-update validation gate | Phase 6 |
| 6 | Business integration per-feature | Phase 7 |
| 7 | Release readiness, cutover, legacy retirement | — |

## Referensi struktur source (§20)

Konvensi: file `snake_case`, class/type `PascalCase`, satu `.cpp` utama per class.

| Class | File |
|---|---|
| `ApplicationContainer` | `src/application/application_container.{h,cpp}` |
| `ApplicationInfrastructureWindow` | `src/application/application_infrastructure_window.{h,cpp}` |
| `UiApplicationBridge` | `src/application/ui_application_bridge.{h,cpp}` |
| `StubApplicationBridge` | `src/application/stub_application_bridge.{h,cpp}` |
| `UiConfigGate` | `src/ui/config/ui_config_gate.{h,cpp}` |
| `ResolvedUiDocument` | `src/ui/config/resolved_ui_document.{h,cpp}` |
| `RenderRuntime` | `src/ui/rendering/render_runtime.{h,cpp}` |
| `NativePeerGdiResourceCache` | `src/ui/rendering/native_peer_gdi_resource_cache.{h,cpp}` |
| `WindowRenderContext` | `src/ui/rendering/window_render_context.{h,cpp}` |
| `LayeredPopupRenderContext` | `src/ui/rendering/layered_popup_render_context.{h,cpp}` |
| `WindowContainer` | `src/ui/container/window_container.{h,cpp}` |
| `ComponentRegistry` | `src/ui/registry/component_registry.{h,cpp}` |
| 13 component (Window, Screen, Container, Text, Button, Input, Combo, Checkbox, Toggle, Card, List, Scrollbar, Dialog) | `src/ui/components/<nama>/<nama>_component.{h,cpp}` |

Full directory tree ada di §20 canonical plan.

---

## Phase 0A — Preflight, blocker sebelum source pertama

Referensi: §21 Phase 0A, §25.1, §25.4

### Tugas

- [ ] **0A.1** Verifikasi root Git `C:\VSCODE\Teminal` dan dirty worktree.
- [ ] **0A.2** Baca ulang `AGENTS.md` dan seluruh canonical plan.
- [ ] **0A.3** Inventarisasi behavior reference dari nested repo (`Open-terminal`, `Open-terminal-native`)
  secara read-only, **hanya sejauh kebutuhan phase yang sedang dikerjakan** — jangan borong semua sekaligus.
- [ ] **0A.4** Sinkronkan `AGENTS.md` dengan keputusan: Direct2D/DirectWrite, primary-surface/HWND
  ownership, in-surface Dialog, layered Combo popup, infrastructure window, accessibility, High Contrast,
  lazy route, private-commit performance contract. Cakupan wajib menurut §25.4: Dialog overlay, layered
  Combo exception, infrastructure window, Scrollbar, `PerMonitorV2`, modal stack/component scope, UIA
  HWND/popup hosting, native-peer GDI lease/cache, newest retained/dirty close transaction, tray-failure
  Exit, private-commit metric.
- [ ] **0A.5** Terapkan identity/version/data-root yang sudah dikunci §19 (nama produk, exe, publisher,
  package ID); tetapkan nama/resource path config baru yang belum ditentukan.
- [ ] **0A.6** Tetapkan build system/toolchain + exact MSVC/SDK untuk baseline Windows 10 22H2 build 19045
  minimum dan Windows 11 release-primary gate; exact Direct2D backend/object/resource-domain model; JSON
  parser; test framework — **tanpa mengimpor project lama**. Manifest wajib membuktikan `PerMonitorV2`.
- [ ] **0A.7** Tetapkan embedded default resource identity, override metadata/version comparison, config
  diagnostic location (`%LOCALAPPDATA%\Yuzha\OpenTerminalNative\logs\ui-config.log`), data paths.
- [ ] **0A.8** Tetapkan measurement-harness contract: trace source/counter, deterministic scenario,
  installed Release test procedure, baseline machine/Windows/DPI, cara ambil first-complete-frame,
  input-to-paint, navigation, resize, private commit, diagnostic working set, HWND, USER/GDI handle,
  render context, dan cache-entry counter per resource domain. Implementasi dan eksekusi harness terjadi
  pada Phase 2.2/2.11/2.12. *(Prasyarat measurement untuk freeze budget Phase 2.12.)*
- [ ] **0A.9** Tetapkan single-instance/second-launch IPC contract untuk normal unelevated same-user/session
  runtime. **Jangan** lock contract ini kalau masih mengasumsikan privilege yang belum konsisten dengan
  model packaging Phase 0B — koordinasikan dulu.
- [ ] **0A.10** Buat acceptance checklist dan exact validation command phase sebelum menulis source.

### Open contracts wajib dibekukan di sini (§25.1)

- Build system/generator + exact MSVC/SDK version + dependency policy + project layout final.
- Exact Direct2D interface/device object graph, hardware/WARP creation parameters, bitblt tier detail,
  resource-domain implementation, layered-popup DIB/HDC path (§9.4).
- JSON parser/library exact pin, test framework/runner, canonical Debug/Release/test commands.
- Embedded default resource ID, override filename/subdirectory, exact config-contract version comparison,
  bentuk diagnostic indicator persistent di Settings/UI Editor.
- Measurement tooling, deterministic maximum Combo/List data, exact baseline machine/Windows/DPI,
  cold-process/warm-file-cache procedure, counters, canonical performance-report format.
- Named-pipe name/security descriptor/message envelope + exact bounded retry timing.
- Sinkronisasi `AGENTS.md` + `.gitignore` terhadap seluruh keputusan canonical plan.

### Exit criteria

Semua blocker source sudah diputuskan; scope, files, dependencies, renderer, measurement route, dan
validation command diketahui; nested repositories tidak berubah. *(Production certificate/release host
bukan blocker Phase 0A.)*

---

## Phase 0B — Blocker sebelum installer preview pertama

Referensi: §21 Phase 0B, §25.2, §19

Track ini boleh berjalan paralel dengan Phase 1 setelah Phase 0A selesai. Contract/dependency pin
dibekukan lebih dahulu; langkah yang membutuhkan `main.cpp`, IPC, atau executable runnable baru
diimplementasikan ketika source tersebut tersedia. Seluruh task 0B wajib selesai sebelum Phase 2.12.

### Tugas

- [ ] **0B.1** Bekukan lalu integrasikan Velopack C++ SDK/CLI **1.2.0** (locked):
  - contract per-user install ke `%LOCALAPPDATA%\Yuzha.OpenTerminalNative`, data root terpisah
    `%LOCALAPPDATA%\Yuzha\OpenTerminalNative`, full offline `Setup.exe`, privilege/manual-elevation
    policy, serta build-only dependency pin dapat diselesaikan sebelum executable tersedia;
  - setelah `main.cpp` tersedia, pasang startup hook `VelopackApp::Build().Run()` tepat sekali sebagai
    operasi pertama di `wWinMain`, sebelum `RoInitialize`/COM, mutex, pipe, config gate, infrastructure
    window, atau UI lain; fast-exit hook tidak boleh menjalankan startup normal;
  - packaging dan installed smoke baru dinyatakan selesai pada Phase 2.12.
- [ ] **0B.2** Tetapkan local test feed/channel, version comparison, integrity/signing policy preview,
  update staging/apply/rollback contract, artifact commands.
- [ ] **0B.3** Tetapkan production feed/release-host/signing requirement sebagai **contract**
  (sertifikat asli baru wajib sebelum public signed release, bukan sebelum source pertama).
- [ ] **0B.4** Implement dan buktikan locked manual-elevation behavior (§19): manual elevated launch
  dengan linked/split token → ditolak + diagnostic/relaunch unelevated; token tanpa linked counterpart
  (UAC disabled / built-in Administrator) → boleh jalan dengan diagnostic, cross-integrity routing/helper
  dinonaktifkan. Second-launch routing tanpa pelonggaran UIPI yang tidak aman.

### Open contracts wajib dibekukan di sini (§25.2)

- Exact local test feed transport/path, channel names, version comparison flags, integrity/signing policy
  preview, artifact commands, installed-test automation.
- Production feed/release host + production signing requirement sebagai contract (secret asli belum
  wajib, jangan disimpan di repo).
- Explicit uninstall-user-data UX — default aman mempertahankan data; hapus data harus pilihan sadar +
  scope path tervalidasi.

### Exit criteria

Contract, dependency pin, source integration, packaging route, dan installed-test automation siap;
Phase 2.12 dapat menghasilkan serta smoke-test preview installed pertama. Uji `N → N+1` penuh tetap
Phase 5 setelah dua versioned build tersedia.

---

## Phase 1 — Schema dan gate

Referensi: §21 Phase 1, §7, §8

### Tugas

- [ ] **1.1** Definisikan schema V1 minimal untuk vertical slice pertama (Window, Container, Text, Button,
  Input) sesuai grammar §7.2. **Jangan** desain seluruh 13 component schema di depan — tambah contract
  per component sebelum component itu diimplementasikan. Minimum universal shape: stable `id`, registered
  `type`, layout/style references, initial visibility/enabled state, serta typed event/navigation binding.
  Button wajib memiliki explicit `variant` dan per-state style references; field tambahan harus mempunyai
  exact name/type/range/default sebelum owner component ditulis.
- [ ] **1.2** Buat embedded default document baru + optional new override identity/path.
- [ ] **1.3** Implement parse, validation, reference checking, token resolution (`$ref`), merge (recursive
  object / scalar replace / array replace), diagnostic, typed `ResolvedUiDocument`. Embedded document
  dan override masing-masing dibatasi 4 MiB serta nesting 64 level; parser menolak literal non-standard
  `NaN`/`Infinity` dan seluruh hasil konversi numerik typed wajib lulus `std::isfinite`.
- [ ] **1.4** Implement typed `ResolvedColor` (`LiteralRgba | SystemColorSlot`), constraint statis
  surface/alpha, complete Dark/Light/High Contrast resolution, runtime theme selection tanpa re-parse.
- [ ] **1.5** Implement whole-override rejection, manual reload dengan last-known-good preservation,
  bootstrap failure
  (`MessageBoxW` + non-zero exit untuk embedded default invalid), rollback-incompatible override fallback
  tanpa compatible snapshot tambahan (§8.1). Tambahkan config persistence/diagnostic contract:
  - override writer memakai temporary sibling pada volume sama → flush → atomic replace/rename; destination
    lama tetap utuh saat gagal, temporary dibersihkan best effort, dan candidate melewati `UiConfigGate`
    sebelum publish;
  - diagnostic log memakai `%LOCALAPPDATA%\Yuzha\OpenTerminalNative\logs\ui-config.log`; kegagalan
    directory/open/write/rotation tidak menahan first frame, tidak menggagalkan UI valid, dan tidak
    menghasilkan dialog berulang;
  - Settings/UI Editor mempertahankan satu diagnostic aktif sampai reload valid berikutnya.
- [ ] **1.6** Bekukan dan uji `ThemePlatformAdapter`: bounded synchronous initial snapshot
  (`SPI_GETHIGHCONTRAST` + `AppsUseLightTheme` registry sebelum first frame), post-first-frame WinRT
  `UISettings` reconciliation/subscription, background-callback dispatch melalui injectable/test sink,
  semantic High Contrast slot materialization + fallback (Light bila app-theme gagal dibaca), coalesced
  resource-epoch behavior — current system RGB **tidak** menjadi bagian config generation. Phase 1 tidak
  bergantung pada infrastructure window nyata; wiring `PostMessage` ke window tersebut dilakukan di 4.3.
- [ ] **1.7** Test: valid, invalid, duplicate/unknown field, missing/cyclic reference, merge/replacement
  semantics, version rejection, override rejection, reload, no-legacy-import, oversized document,
  nesting limit, non-finite number, atomic-write failure di setiap tahap, stale temporary cleanup,
  log-path unwritable, serta bukti bahwa kegagalan log tetap non-blocking.

### Validation minimum (§23)

`git diff --check` • build Debug x64 • build Release x64 • test schema/parser/resolution • test
Dark/Light/High Contrast contract completeness, alpha/surface rejection, override minimum-version,
symbolic system-color materialization/resource-epoch change, rollback fallback • test size/depth/finite
limits, atomic override replacement, last-known-good preservation, dan non-blocking diagnostic failure.

### Exit criteria

Schema dapat di-resolve tanpa HWND dan tidak pernah membaca legacy `ui.json`.

---

## Phase 2 — Native primitives dan vertical component slice

Referensi: §21 Phase 2, §9.4, §9.5, §9.6

### Tugas

- [ ] **2.1** Renderer probe: flip-model parent + synthetic native Edit child, dirty tracking per-buffer,
  full repaint pada resize/device/config/theme epoch, occlusion standby, DPI/hide-restore stress. Kalau
  ada artifact reproducible → turun ke bitblt `SEQUENTIAL` (device graph sama). **Gate keras:** jangan
  lanjut ke component slice di backend yang belum lulus; kebutuhan `ID2D1HwndRenderTarget` = revisi §9.4,
  stop dulu.
- [ ] **2.2** Lengkapi `RenderRuntime`, `NativePeerGdiResourceCache`, minimal `WindowRenderContext`,
  resource-domain + GDI lease/cache counter, primitive Direct2D/DirectWrite backend-agnostic (DPI, font
  measurement, typed color/compositing, geometry, clipping, invalidation, caching, hardware/WARP creation,
  device-lost hard-failure path). Semua measure primitive harus testable tanpa device/swapchain/render
  context.
- [ ] **2.3** Buat component contract, registry/factory, minimal vertical-slice `WindowContainer` host
  (logical focus coordinator, generic overlay plane, native-peer traversal, one-surface dispatch).
  Routing/multi-window lifecycle lengkap tetap Phase 4.
- [ ] **2.4** Implement `Window`, `Container`, `Text`, HWND-less `Button`, native-Edit-backed `Input` —
  vertical slice pertama, satu primary surface. Acceptance slice yang tidak boleh tersirat:
  - Input unfocused mempunyai satu continuous thin outline; focused mempunyai satu solid accent outline
    lebih tebal (target 1→2 logical px), tanpa repeated segment, sibling leak, atau stale border;
  - single-line text vertically centered, multiline top-aligned, seluruh text left-aligned;
  - Button memakai explicit JSON state values, solid hover/pressed border, dan solid focus outline tanpa
    dashed/dotted Win32 ring;
  - Window menerapkan SDK `DWMWA_USE_IMMERSIVE_DARK_MODE` value 20 setelah create/recreate; failure
    non-fatal dan tidak mencoba undocumented value 19.
- [ ] **2.5** Verifikasi GDI-compatible layout (`DWRITE_MEASURING_MODE_GDI_CLASSIC`, ClearType,
  `DWRITE_RENDERING_MODE_GDI_CLASSIC`) terhadap native Edit pada DPI 100/125/150/200%, Dark+Light.
  Screenshot berpasangan, cek baseline/advance/spacing/density.
- [ ] **2.6** Implement + uji: combined logical/native focus coordinator, two-way Edit focus sync,
  activation restore, `nativePeerContentRect` containment/non-overlap assertion, IME commit/candidate-close
  sebelum suspend, native-peer suspend/resume hook lengkap (suspended text snapshot + restoration).
  Pelanggaran containment menghasilkan actionable diagnostic + safe non-interactive layout-error
  presentation, bukan crash atau silent overlap.
  Seluruh bounds Input adalah pointer hit target: klik padding memfokuskan Edit dan menempatkan caret
  pada posisi teks terdekat. IME completion wajib memakai `CPS_COMPLETE`, menunggu composition-end sebelum
  snapshot/hide; bila gagal, modal/reload ditunda atau dibatalkan dengan diagnostic, bukan silent cancel.
  Multiline native scroll contract memakai unit line/baris, bukan per-pixel.
- [ ] **2.7** Implement Input-owned GDI resource lease, cached `WM_CTLCOLOREDIT`/`WM_CTLCOLORSTATIC`,
  atomic font/brush replacement saat theme/DPI change, destroy-order benar (Edit HWND destroyed sebelum
  final font lease dilepas). Zero-lease cache eviction. Restore order: DPI/layout → lease →
  `WM_SETFONT`/colors → complete frame → `ShowWindow`.
- [ ] **2.8** Generic editable-component dirty participant contract; Input membuktikan baseline, derived
  `IsDirty`, Save-success/failure patch, staged Discard, Cancel — tanpa persistence/business logic masuk
  ke component/container.
- [ ] **2.9** First complete frame dirender ke presentation buffer **sebelum** top-level window
  ditampilkan. Window class tidak pakai default white client brush; `WM_ERASEBKGND` handled dan tidak
  menghapus complete app-owned frame.
- [ ] **2.10** Jalankan executable placeholder dari JSON: buktikan create/layout/paint/alpha/event/patch,
  no-blank-frame, focus sync, suppression hook, resource recreation.
- [ ] **2.11** Implementasikan measurement harness sesuai contract 0A.8 dan lakukan dry-run pada loose
  Release untuk memvalidasi counter/scenario. Hasil ini hanya diagnostic; jangan membekukan budget atau
  mengklaim PASS dari build tree.
- [ ] **2.12** Selesaikan Phase 0B, buat installer preview pertama, lalu smoke-test executable dari
  installed path. Setelah install tersedia, jalankan ulang harness pada installed Release dan baru
  **bekukan** numeric cold/warm first-frame + idle private-commit budget sebelum Phase 3A.

### Validation minimum (§23)

Headless/device-lost/hidden-window measure test (no dependency ke render context/device-dependent
resource) • Windows smoke: Direct2D device-lost/recreate, hardware-to-WARP fallback, flip/bitblt renderer
probe + native Edit child, per-buffer dirty/full-repaint/occlusion, device-lost hard-failure diagnostic,
GDI-classic text consistency • visual Input outline/alignment/hit-target/caret-nearest smoke • multiline
line-scroll synchronization • two-way logical/native focus smoke • DWM value-20 create/recreate dan
non-fatal-failure smoke • no-blank-frame visual/capture check • manifest/runtime verification
`PerMonitorV2` + `WM_DPICHANGED` di 100/125/150/200%.

### Exit criteria

Installed test app tampil dari JSON, tidak ada hidden visual constant atau business side effect, dapat
di-uninstall tanpa merusak user data di luar scope aplikasi.

---

## Phase 3A — Component dasar, Scrollbar, dan basic UIA

Referensi: §21 Phase 3A, §9.7

### Tugas

- [ ] **3A.1** Sebelum source masing-masing ditulis, bekukan exact schema name/type/range/default untuk
  `Checkbox`, `Toggle`, `Card`, `Screen`, dan `Scrollbar`; `Screen` wajib membawa stable `routeId` serta
  validated route/navigation binding. Setelah contract test tersedia, tambahkan seluruh component ini
  sebagai HWND-less implementation.
- [ ] **3A.2** Integrasikan `Scrollbar` sebagai owned component contract pada scrollable `Container` dan
  multiline `Input`; native visible scrollbar tidak dipakai. (Integrasi virtualized `List` selesai di
  Phase 3B.) Verifikasi native Edit rect exclude Scrollbar track + app-painted decoration di
  minimum/normal size dan seluruh accepted DPI; sinkronisasi multiline Edit memakai unit line/baris.
- [ ] **3A.3** Implement UIA provider untuk `Button`, `Checkbox`, `Toggle`, `Scrollbar`. Untuk `Input`:
  `IRawElementProviderHwndOverride`, Input provider di fragment navigation order, native Edit
  host-provider delegation tanpa duplicate logical element.
- [ ] **3A.4** Lengkapi token seluruh Button variant per theme. Jalankan gerbang objektif: text contrast
  ≥4.5:1, visual boundary/focus ≥3:1 terhadap adjacent resolved color. Screenshot 6 variant ×
  normal/hover/pressed/focused/disabled, Dark+Light. Kandidat gagal direvisi sebelum phase lulus.
- [ ] **3A.5** Uji Inspect Raw/Control/Content view, previous/next visual order, Name/label, AutomationId,
  Text/Value/selection, read-only/password, SetFocus, peer recreate, config reconciliation, Narrator
  duplicate-announcement check (khusus Input).
- [ ] **3A.6** Uji visual state, drag/wheel/keyboard scroll, UIA/Narrator, High Contrast, DPI, resize,
  repaint, cleanup sesuai capability masing-masing component.

### Validation minimum (§23)

Test component/event/patch contract untuk `Checkbox`/`Toggle`/`Card`/`Screen`/`Scrollbar` yang terkena •
keyboard smoke, UIA inspection Raw/Control/Content tree, Narrator duplicate-announcement check (khusus
Input), Input HWND override/host-provider order dan peer-recreation check, serta High Contrast smoke untuk
seluruh component baru • Button contrast gate (text ≥4.5:1, visual boundary/focus ≥3:1) lulus sebelum
screenshot review enam variant × normal/hover/pressed/focused/disabled pada Dark+Light.

### Exit criteria

Component dasar dapat diregistrasikan lokal, Scrollbar tidak bergantung native themed control, basic
UIA/keyboard smoke lulus tanpa central widget dispatcher.

> **Phase 4 tidak boleh dimulai sebelum Phase 3A dan 3B keduanya lulus.**

---

## Phase 3B — Combo popup, Dialog overlay, List virtualization, advanced UIA

Referensi: §21 Phase 3B, §9.6, §9.7

### Tugas

- [ ] **3B.1** Bekukan exact Combo schema/flags/backing-format contract, lalu implement `Combo` dengan
  `LayeredPopupRenderContext` khusus, premultiplied-alpha DIB/HDC via `UpdateLayeredWindow`, grayscale
  text antialiasing, non-activating/no-taskbar popup
  (`WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`), owner-side keyboard routing, pointer/outside-click
  handling, seluruh dismissal trigger, theme/system-color epoch redraw/re-present. Popup memilih monitor
  dari anchor/owner, memilih arah buka yang masih muat, clamp ke reachable work area, dan recompute
  geometry/resource saat pindah monitor atau menerima `WM_DPICHANGED` ketika terbuka.
- [ ] **3B.2** Bekukan Dialog schema dengan explicit `modality` dan `dismissPolicy` (Escape,
  outside-click, explicit action), lalu implement in-surface `Dialog` overlay via generic
  `ModalOverlayStack`. Verifikasi nested
  push/pop, component-ancestry scope, suppression refcount, Input dalam/luar Dialog, Combo milik active
  Dialog tetap boleh buka popup, scrim, rounded panel, shadow, focus trap, dismissal, IME completion,
  native-peer snapshot + exact restoration.
- [ ] **3B.3** Bekukan exact List schema dan virtualization/selection/scroll contract, lalu implement
  virtualized `List` via `ScrollbarComponent`, visible-row realization, selection, scroll, cleanup —
  tanpa HWND per row.
- [ ] **3B.4** Implement UIA: `List` → `ItemContainer`, `VirtualizedItem`, Selection, Scroll, ScrollItem.
  `Combo` → `ExpandCollapse`; popup-hosted List → `Selection`; item → `SelectionItem`. `Dialog` →
  Window/IsDialog/IsModal, active-scope navigation, disabled background, structure/focus event,
  suspended-provider identity.
- [ ] **3B.5** Ukur full-surface update/copy cost layered popup pada deterministic maximum V1 item/size
  vs input-to-paint budget. **Jangan klaim murah tanpa measurement.**
- [ ] **3B.6** Implement reusable Save/Discard/Cancel confirmation `Dialog` + generic result contract
  untuk dipakai close coordinator Phase 4. **Constraint:** Phase 4 tidak boleh mengganti ini dengan native
  message box atau component-specific close UI.

### Validation minimum (§23)

Combo popup smoke: non-activation, owner-side keyboard routing, pointer selection, seluruh dismissal
trigger, theme/High-Contrast live redraw, grayscale layered text, taskbar/Alt-Tab absence, per-popup DPI,
work-area clamping, alternate opening direction, dan live cross-monitor/DPI reposition • advanced UIA: Combo
popup logical reparenting/pattern split, Dialog IsDialog/IsModal active-scope/background behavior, List
`ItemContainer`/`VirtualizedItem` smoke • layered-popup full-surface update/copy cost terhadap
input-to-paint budget pada deterministic maximum V1 item/size — jangan lulus tanpa measurement nyata.

### Exit criteria

Combo rounded/shadow konsisten, Dialog tidak ditembus native child, virtualized List serta advanced UIA
lulus, layered presentation dalam performance budget.

---

## Phase 4 — WindowContainer, ApplicationContainer, multi-window, dan tray

Referensi: §21 Phase 4, §11, §11.1, §11.2, §12, §13

### Tugas

- [ ] **4.1** Lazy-assemble hanya route aktif, cache screen per window; inactive route tidak
  dilayout/dipaint.
- [ ] **4.2** Implement di `WindowContainer`: per-window route/state, combined focus coordinator,
  `ModalOverlayStack`, logical component-scope/native-peer traversal, reload normalization, window-level
  dirty aggregation.
- [ ] **4.3** Implement `ApplicationInfrastructureWindow` (satu hidden top-level HWND seumur proses, bukan
  `HWND_MESSAGE` — wajib terima broadcast `TaskbarCreated`), process window registry, process-global
  OS-state signal fan-out (`WM_SYSCOLORCHANGE`, `WM_SETTINGCHANGE`, High Contrast/app-theme, display
  topology), tray callback, `TaskbarCreated`, second-launch routing di `ApplicationContainer`. Selesaikan
  runtime startup/IPC contract:
  - setelah Velopack hook, first instance memperoleh same-user/session mutex, membuat named-pipe listener
    sampai status ready, baru membuat infrastructure/route window;
  - second instance memakai bounded backoff maksimal dua detik; kegagalan menghasilkan diagnostic dan
    orderly non-zero exit, tidak pernah diam-diam menjadi instance kedua;
  - pipe worker hanya melakukan IO/validation lalu `PostMessage` ke infrastructure window; worker tidak
    mengakses HWND registry atau UI state secara langsung;
  - hubungkan `ThemePlatformAdapter` callback melalui dedicated `PostMessage`, requery/coalesce di UI
    thread, redraw/re-present seluruh active context, dan reapply DWM attribute 20 pada effective theme
    change; subscription baru dipasang setelah first complete frame + idle dan seluruh failure non-fatal.
- [ ] **4.4** Implement per-window/popup `WM_DPICHANGED` — **bukan** process-global signal; setiap
  top-level + Combo popup handle sendiri.
- [ ] **4.5** Implement `reuse-per-route` untuk external/taskbar command: registry assertion satu route ID
  tidak pernah punya visible+hidden duplicate; same-window no-op; activate visible match; canonical
  restore retained match (kosongkan retained slot tanpa ganti route pemanggil). Jika same-window intent
  tidak menemukan visible/retained match, ganti active route window pemanggil sesuai binding; external/
  taskbar intent tetap membuat window baru ketika belum ada match. Untuk V1, `newWindow` berarti create
  hanya saat belum ada matching route; `allowMultiple` bukan policy yang diimplementasikan.
- [ ] **4.6** Implement full close/tray lifecycle:
  - `PrepareClose`/`CommitClose` per window; `PrepareCloseAll`/`CommitCloseAll` untuk Exit.
  - Destructive close-one-window.
  - Non-destructive hide-dirty-window ke tray — invariant keras: **maksimal satu** retained hidden route
    window.
  - Newest-window retained replacement (old retained → `PrepareClose`; Cancel → newest tetap visible,
    old retained kembali hidden, tidak ada state yang dihancurkan sebagian).
  - Release/recreate hidden render resources (tekan idle working set).
  - Hidden-window route reuse memakai satu canonical restore path untuk tray maupun navigation:
    tentukan DPI → measure/layout → acquire/update native-peer lease → `WM_SETFONT`/resolved colors →
    complete frame generation aktif → `ShowWindow` + activate.
  - Klik kiri tray memulihkan retained window; bila slot kosong, buat Terminal route window. Klik kanan
    menampilkan native route menu + explicit Exit. Seluruh tray callback masuk melalui infrastructure
    window, bukan route window.
  - `TaskbarCreated` memasang ulang tray icon setelah Explorer restart.
  - Tray-failure restore/fallback Exit (`Shell_NotifyIcon` gagal saat window visible → diagnostic +
    `PrepareCloseAll`). Bila tray hilang saat hanya retained window tersedia, restore retained segera;
    tray unavailable selalu menyisakan route window visible/reachable.
  - Explicit Exit. Confirmation dialog **wajib** pakai Dialog dari Phase 3B.6, jangan bikin ulang.

### Validation minimum (§23)

Modal-overlay smoke (nested stack, component-ancestry scope, native-peer suppression refcount,
active-Dialog Input/Combo, IME completion, suspended Input snapshot, restoration, popup close, dismissal,
focus trap, reload drain) • close-coordination smoke (dirty single close, non-destructive dirty hide,
newest retained replacement, Save success/failure, staged Discard, Cancel rollback, explicit Exit,
tray-failure Exit, no-duplicate-route registry assertion) • startup/IPC race smoke (mutex, listener-ready,
bounded second-instance retry, no duplicate fallback, worker-to-infrastructure `PostMessage`) • tray
left/right action, `TaskbarCreated` reinstall, tray-loss retained restore, canonical frame-before-show
route restore • same-window no-op/visible-match/retained-match/no-match routing matrix • live
`ColorValuesChanged` background dispatch/coalescing dan DWM reapply smoke.

### Exit criteria

Terminal tetap terbuka ketika Chrome Launcher muncul di top-level window kedua; tidak ada accidental
duplicate untuk external route; newest retained/Cancel/Exit behavior lulus tanpa data loss atau window
yang tidak dapat dijangkau.

---

## Phase 5 — Stub application dan installed-update validation gate

Referensi: §21 Phase 5, §23

### Tugas

- [ ] **5.1** Lengkapi semua placeholder screen + deterministic data (7 screen: Terminal, JSON INJECT,
  JSON Editor, Chrome Launcher, Chrome Profile Manager, Settings, UI Editor). UI Editor harus memakai
  atomic override writer Phase 1 dan menampilkan diagnostic aktif persisten sampai reload valid; tidak
  boleh membuat jalur parse/write khusus yang melewati `UiConfigGate`.
- [ ] **5.2** Validasi setiap navigation binding, `UiEvent`, bridge route, `UiPatch`/`ViewState`.
- [ ] **5.3** Jalankan Windows visual/runtime smoke lengkap: System/Dark/Light/High Contrast, UIA/Narrator,
  `PerMonitorV2`, resize, two-way focus, modal native-peer suppression/restoration, keyboard, alpha,
  layered popup, no-blank-frame, repaint/ghosting, lazy route, virtualization, multi-window, taskbar
  route, infrastructure window, tray lifecycle — termasuk initial theme snapshot → post-frame `UISettings`
  reconciliation, background `ColorValuesChanged` coalescing, DWM dark-mode reapply/non-fatal failure,
  reload saat nested Dialog/IME aktif, theme switch saat Combo terbuka, all-route retained replacement,
  dirty Cancel, tray-failure Exit, hidden-window stale-resource restore sebelum `ShowWindow`.
- [ ] **5.4** Hasilkan **dua** versioned preview build `N` dan `N+1`; clean-install `N`, update ke `N+1`
  via jalur update terencana. Clean-install `Setup.exe` + first launch **wajib diuji dengan jaringan
  mati**.
- [ ] **5.5** Verifikasi executable/version berubah, app tetap launchable, config/user data yang disiapkan
  di `N` tetap tersedia di `N+1`.
- [ ] **5.6** Uji update gagal/ditolak, uninstall, Explorer restart, update saat app masih jalan. Verifikasi
  manual check, scheduled check 24 jam setelah frame+idle, no-auto-download, consent download/restart,
  atomic updater metadata (`%LOCALAPPDATA%\Yuzha\OpenTerminalNative\updater\state.json`) dengan
  `lastAttemptUtc` + `lastSuccessfulCheckUtc`, dan `PrepareCloseAll` sebelum apply. Retention tepat satu
  previous full package + satu staged file dari attempt terakhir; staged file dibersihkan setelah apply
  sukses atau failure final, sementara failed apply menyisakan current installed version tetap launchable.
  Scheduled-check test memakai injectable clock/test seam untuk memajukan waktu secara deterministik;
  test tidak menunggu 24 jam nyata dan tidak mengubah system clock. Read/write metadata failure hanya
  menghasilkan non-blocking diagnostic dan tidak memblokir first frame, manual check, close, atau use.
- [ ] **5.7** Jalankan deterministic performance/resource scenario, buktikan seluruh budget (lihat tabel
  performance di bawah) + no-growth 100-cycle.
- [ ] **5.8** Verifikasi second-launch routing same-user/session pada locked per-user privilege/install
  scope; pastikan normal UI runtime tidak elevated.
- [ ] **5.9** Audit: tidak ada business side effect, tidak ada dependency ke nested repository.

### Validation minimum (§23)

Clean-install smoke dari artifact (bukan build directory) • offline `Setup.exe` clean-install/first-launch
smoke dengan jaringan mati • installed update `N→N+1` smoke + preservation check config/settings/cache/user
data • failed/invalid update dan uninstall smoke • package version, manifest/checksum/signature, shortcut,
taskbar, tray, dan installed-path checks • second-launch routing same-user/session • Windows 10 22H2 build
19045 compatibility matrix serta supported-current Windows 11 visual/release-primary matrix dengan exact OS
build dan hasil performance revalidation tercatat • theme-platform smoke (initial snapshot → post-frame
`UISettings` reconciliation, `ColorValuesChanged` coalescing, DWM attribute reapply, fallback failure) •
performance harness penuh sesuai tabel budget di bawah + no-growth 100-cycle check.

### Exit criteria

UI runtime dinyatakan **PASS** hanya jika build, contract tests, visible Windows smoke, clean install, dan
`N → N+1` installed update lulus semua. Kalau visible/install/update smoke tidak tersedia → verdict
maksimal **PARTIAL**, bukan PASS.

---

## Phase 6 — Business integration (terpisah, per-feature)

Referensi: §21 Phase 6, §18

**Prasyarat: hanya mulai setelah stub UI disetujui user.** Urutan feature belum dikunci — tetapkan dari
dependency live sebelum phase ini dimulai (§25.3).

### Template per-feature (ulangi untuk setiap feature)

- [ ] **6.x.1** Petakan action/view-state contract satu feature.
- [ ] **6.x.2** Buat adapter business di root, tanpa UI/HWND dependency.
- [ ] **6.x.3** Ganti stub handler feature tersebut dengan adapter nyata.
- [ ] **6.x.4** Verifikasi behavior parity + regression. Yang **wajib tetap sama**: hasil behavior, input
  dan parameter, urutan operasi, validation, persistence semantics, async/thread contract,
  cancellation/stale-result handling, success/error semantics. **Hanya wiring yang boleh berubah**
  (legacy handler → `UiEvent` → bridge/adapter → business operation → `UiPatch`/`ViewState`).
- [ ] **6.x.5** Hasilkan + uji installer/update artifact untuk accepted build.
- [ ] **6.x.6** Lanjut feature berikutnya hanya setelah feature aktif lulus.

Nested repository lama boleh jadi referensi port/reimplementation eksplisit di phase ini saja — tetap
tidak boleh jadi build/runtime dependency.

---

## Phase 7 — Release readiness, cutover, dan legacy retirement

Referensi: §21 Phase 7

### Tugas

- [ ] **7.1** Full Release x64 build, package, clean-install, upgrade dari accepted build sebelumnya,
  uninstall, checksum/integrity, version, shortcut/taskbar/tray, release-note validation.
- [ ] **7.2** Jadikan UI baru entrypoint canonical setelah seluruh integration yang disetujui lulus.
- [ ] **7.3** Pastikan release/update artifact dapat dipublikasikan lewat workflow yang dipilih.
  **Actual publication tetap menunggu instruksi eksplisit user.**
- [ ] **7.4** Penghapusan legacy code/reference — **hanya dengan otorisasi eksplisit** + cleanup plan
  tersendiri.

---

## Performance budget (cross-phase — harness divalidasi di 2.11, dibekukan di 2.12, diverifikasi di 5.7)

Diukur dari installed Release x64, baseline machine/Windows/DPI Phase 0A, deterministic stub data,
minimum 30 sample per scenario.

| Metric | Target |
|---|---|
| Cold process-start → first complete non-blank frame | p95 ≤ 250 ms |
| Warm process-start → first complete non-blank frame | p95 ≤ 120 ms |
| First-time route assembly/navigation | p95 ≤ 100 ms |
| Cached route navigation | p95 ≤ 50 ms |
| Input event → visual state terlihat | p95 ≤ 33 ms |
| App-owned UI-thread work per input event | p95 ≤ 8 ms |
| Resize/layout/paint frame (deterministic resize) | p95 ≤ 16.7 ms, no stall > 50 ms |
| Idle private commit (10s settle, 1 Terminal stub window) | ≤ 64 MiB provisional |
| HWND baseline | 1 infra + 1/visible window + Edit child + active Combo popup; Dialog/Button/Checkbox/Toggle/Scrollbar/List row = 0 |
| 100-cycle route/theme/hide-restore | zero net growth: HWND, USER/GDI handle, private commit, render context, GDI lease, cache entry |

Catatan: startup canonical = cold process dengan OS/file cache hangat, tanpa app warm-up. True
cold-file-cache run dicatat terpisah sebagai diagnostic saja. Scenario selain startup melakukan satu
warm-up sebelum sample dicatat. Breakdown wajib pisahkan bootstrap/config, device creation/WARP warm-up,
first layout/render, first present — tapi **tidak boleh** exclude device-init dari PASS/FAIL end-to-end.

---

## Global acceptance criteria — non-exhaustive quick summary Phase 5/7

Daftar berikut hanya indeks cepat. Seluruh butir §22 dan §23 canonical tetap mandatory walaupun tidak
ditulis ulang di sini; checklist ini tidak boleh dipakai untuk menurunkan acceptance atau validation.

- **Repository:** semua source baru di root; nested repo bersih/bukan dependency; tidak ada
  credential/binary di Git; `.gitignore` lengkap untuk native/IDE/packaging output.
- **Config-driven UI:** semua screen dirakit dari JSON baru; ubah override baru = ubah UI tanpa ubah C++;
  tidak ada file/JSON access di paint hot path; invalid config → diagnostic actionable, bukan silent
  fallback; Dark/Light/High Contrast selalu resolve lengkap sebelum document dipublikasikan.
- **Component ownership:** satu `.cpp` utama per component type; tidak ada component-specific logic di
  luar pemiliknya; Button/Checkbox/Toggle/Text/Card/Container/Screen/row-List = zero child HWND; List
  virtualized; Scrollbar HWND-less.
- **Runtime & visual:** semua app-owned visual via Direct2D/DirectWrite; satu primary surface per
  top-level window; `PerMonitorV2` terbukti manifest + runtime; compatibility smoke lulus Windows 10
  19045 **dan** visual/release-primary smoke lulus Windows 11; first frame tidak menunggu
  scan/network/WSL/plugin/business file op; tidak ada blank/white frame di cold start atau hidden restore.
- **Accessibility & High Contrast:** semua component interaktif keyboard-reachable + visible focus tidak
  bergantung warna saja; Narrator + accessibility inspection tool benar-benar dijalankan sebelum klaim
  PASS (bukan diasumsikan dari adanya HWND).
- **Performance & resource budget:** lihat tabel di atas — diukur dari installed Release, bukan build
  tree.
- **Multi-window & tray:** minimal 2 independent top-level window; external route tidak mengganti window
  lain yang sudah terbuka; maksimal 1 retained hidden route window (hard invariant); kegagalan tray tidak
  pernah meninggalkan proses tanpa window reachable.
- **Stub/business boundary:** stub tidak punya business side effect; semua action/patch lewat typed
  bridge dengan `UiAddress` wajib; business integration nanti tidak mengenal JSON/HWND/paint/layout.
- **Installer, updater, release artifact:** offline `Setup.exe` clean-install tanpa jaringan; `N→N+1`
  update sukses + preservasi data; rollback tidak merusak; update check/download tidak block first
  frame/UI thread; uninstall bersih tanpa menghapus user data diam-diam.

---

## Di luar scope (§24) — jangan dikerjakan sebelum fase yang tepat

- Migrasi/compatibility dengan UI lama.
- Import/link/dependency terhadap nested repository.
- RTL layout mirroring (`WS_EX_LAYOUTRTL`) — tapi Unicode bidi text shaping tetap wajib benar di dalam
  bounds LTR.
- Terminal/Chrome/WSL launch nyata; scan profile nyata; settings/provider/API key persistence nyata.
- Network request business yang tidak diperlukan installer/updater.
- Plugin discovery atau marketplace.
- Penghapusan implementasi lama (sebelum Phase 7.4).
- Business rule baru atau perubahan behavior business lama.

Packaging, installer, installed-update validation, dan release-artifact generation **sengaja masuk**
scope V1 — bukan exception yang perlu dipertanyakan. Public publication tetap butuh instruksi user
tersendiri.

---

## Dokumentasi & Git (§25.4)

- [ ] `AGENTS.md` disinkronkan di Phase 0A (lihat 0A.4).
- [ ] `.gitignore` disinkronkan sebelum build/artifact generation pertama — saat ini masih bawa pola era
  .NET, belum lengkap untuk output MSVC/native/packaging (`x64/`, `Debug/`, `Release/`, `*.obj`,
  `*.tlog`, `*.ipdb`, `*.iobj`, generated installer/update feed, symbol/output lain).
- [ ] Commit/push/PR/release hanya atas instruksi eksplisit user — progres di dokumen ini **tidak**
  otomatis menjadi Git history.

---

*Dokumen ini adalah working checklist, bukan dokumen statis. Update status `[ ]` → `[x]` seiring
progress, dan kalau ada keputusan baru dari measurement/evidence yang mengubah baseline canonical plan,
catat sebagai revisi eksplisit di `Termial-plan.md` — bukan diam-diam di file ini saja.*
