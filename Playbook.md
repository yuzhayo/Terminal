# Terminal UI and Business Logic Playbook

Dokumen ini adalah panduan operasional untuk menambah screen, component, navigation, dan business
logic pada Terminal. Kontrak arsitektur tetap berada di `Termial-plan.md`; playbook ini menjelaskan
cara memakai kontrak tersebut tanpa memilih ulang stack.

## 1. Model kerja

```text
Assets/ui/core.json + Assets/ui/screens/*.json
  -> tools/Merge-UiConfig.ps1 (saat build) -> build/generated/ui/terminal.ui.default.v1.json
  -> UiConfigGate + ResolvedUiDocument
  -> ApplicationContainer memiliki process/window registry
  -> WindowContainer memilih route aktif
  -> ComponentRegistry membuat component tree secara lazy
  -> component mengirim UiEvent berdasarkan events.action
  -> UiApplicationBridge
  -> UiActionRegistry mencari handler
  -> handler menjalankan logic dan mengembalikan UiPatch
  -> WindowContainer menerapkan navigation, dialog, title, dan repaint

second launch / tray / taskbar
  -> ApplicationInfrastructureWindow memvalidasi dan mengantrekan intent
  -> ApplicationContainer mengaktifkan route window yang ada atau membuat WindowContainer baru
```

Route tidak memiliki inventory hardcoded. Setiap file `Assets/ui/screens/<routeId>.json` menjadi satu
route yang valid; nama file adalah route ID. Tidak ada manifest — file baru otomatis ikut saat build.
Screen baru dibuat ketika pertama dibuka, lalu disimpan di cache window. Screen yang tidak aktif tidak
ikut layout atau paint.

### Tingkat perubahan

| Kebutuhan | Yang diubah |
| --- | --- |
| Screen baru memakai component dan action yang sudah ada | JSON saja |
| Button baru memakai action yang sudah ada | JSON saja |
| Business action baru | JSON + satu handler registration |
| Backend/service baru | Feature module + handler registration; UI tidak perlu tahu service |
| Component visual baru | Component C++ + schema resolver + ComponentRegistry |

Tidak ada arbitrary C++ atau DLL yang dimuat dari JSON. “Plug and play” berarti screen dapat disusun
dari component terdaftar dan logic dapat dipasang melalui action registry tanpa menambah `if/else` di
`WindowContainer`.

## 2. File utama

- `Assets/ui/core.json`: envelope schema, token dark/light/highContrast, style, dan definisi window.
  Tidak boleh memuat `screens`.
- `Assets/ui/screens/<routeId>.json`: satu file per screen — component tree, style reference, binding,
  event, action, dan payload. Nama file adalah route ID dan wajib sama dengan field `routeId` di dalam.
- `tools/Merge-UiConfig.ps1`: menggabungkan `core.json` + seluruh `screens/*.json` menjadi
  `build/generated/ui/terminal.ui.default.v1.json` sebelum `ResourceCompile`. Dipanggil otomatis oleh
  `src/Terminal.vcxproj` dan `tests/TerminalTests.vcxproj`; jangan menjalankannya manual sebagai syarat
  build. File hasil gabungan adalah build artifact — tidak tracked Git dan tidak boleh diedit.
- `src/ui/config/resolved_ui_document.h/.cpp`: schema, validation, dan resolved object graph.
- `src/ui/components/component_registry.cpp`: factory component yang tersedia untuk JSON.
- `src/ui/application/ui_application_bridge.h`: envelope `UiEvent` dan hasil `UiPatch`.
- `src/ui/application/ui_action_registry.h/.cpp`: registry `action ID -> handler`.
- `src/ui/application/stub_application_bridge.h/.cpp`: composition root untuk handler aplikasi yang
  menyediakan fallback untuk seluruh action JSON.
- `src/logic/features/*`: business rules per feature, tanpa dependency UI.
- `src/logic/storage/*` dan `src/logic/platform/*`: persistence dan operasi Windows milik logic.
- `src/logic/application/core_application.*`: facade typed untuk adapter.
- `src/application/adapters/*`: modul plug-and-play yang mengganti handler stub per feature.
- `src/ui/containers/window_container.h/.cpp`: route lifecycle, lazy screen cache, input routing,
  modal, focus, UIA, layout, dan paint.
- `src/application/application_infrastructure_window.h/.cpp`: hidden process window untuk IPC,
  tray callback, `TaskbarCreated`, dan process-global Windows signals.
- `src/application/application_container.h/.cpp`: composition root process, top-level window registry,
  external route routing, tray lifetime, dan shared resource fan-out.

`Open-terminal` dan `Open-terminal-native` hanya sumber referensi. `Open-terminal-core` telah
dimigrasikan; business logic canonical yang dibangun aplikasi berada di `src/logic`.

## 3. Membuat screen baru

Contoh berikut membuat route `profile-manager`.

### Langkah 1 — Buat file screen baru

Buat file `Assets/ui/screens/profile-manager.json`. Nama file adalah route ID. Isi file adalah object
screen itu sendiri (bukan dibungkus object `screens`):

```json
{
  "id": "profile-manager-screen",
  "type": "Screen",
  "style": {
    "$ref": "styles.surface"
  },
  "routeId": "profile-manager",
  "children": [
    {
      "id": "profile-manager-content",
      "type": "Container",
      "style": {
        "$ref": "styles.surface"
      },
      "layout": {
        "width": "fill",
        "height": "fill"
      },
      "direction": "column",
      "gap": 12,
      "padding": {
        "left": 24,
        "top": 24,
        "right": 24,
        "bottom": 24
      },
      "children": [
        {
          "id": "profile-manager-title",
          "type": "Text",
          "style": {
            "$ref": "styles.text-title"
          },
          "text": "Profile Manager",
          "variant": "title"
        },
        {
          "id": "profile-name-input",
          "type": "Input",
          "style": {
            "$ref": "styles.input"
          },
          "layout": {
            "width": "fill"
          },
          "valueBinding": {
            "$bind": "viewState.profileName"
          },
          "placeholder": "Nama profile",
          "events": {
            "changed": {
              "action": "update-profile-name",
              "payload": {}
            }
          }
        },
        {
          "id": "profile-save-button",
          "type": "Button",
          "style": {
            "$ref": "styles.button-primary"
          },
          "label": "Simpan",
          "variant": "primary",
          "events": {
            "click": {
              "action": "save-profile",
              "payload": {
                "source": "profile-manager"
              }
            }
          }
        }
      ]
    }
  ]
}
```

Aturan penting:

- nama file screen, `routeId`, component `id`, dan action ID memakai lower-kebab-case;
- `routeId` wajib sama dengan nama file tanpa ekstensi — build gagal bila tidak cocok;
- satu file berisi tepat satu screen; screen tidak boleh ditulis di `core.json`;
- tidak ada manifest untuk diperbarui — file baru di `Assets/ui/screens/` otomatis ikut saat build;
- component `id` wajib unik di dalam satu screen tree;
- setiap component wajib mempunyai `type` dan style `$ref` yang sudah tersedia;
- field yang tidak dikenal ditolak sebagai config invalid;
- dimension layout menerima `"auto"`, `"fill"`, atau integer DIP;
- screen tidak perlu didaftarkan di C++ jika semua component type sudah tersedia.

### Langkah 2 — Tambahkan tombol navigation

Button navigation dapat ditempatkan di screen mana pun:

```json
{
  "id": "open-profile-manager-button",
  "type": "Button",
  "style": {
    "$ref": "styles.button-default"
  },
  "label": "Buka Profile Manager",
  "events": {
    "click": {
      "action": "navigate-route",
      "payload": {
        "routeId": "profile-manager"
      }
    }
  }
}
```

`navigate-route` sudah terdaftar. `WindowContainer` memeriksa bahwa target memang ada di resolved
`screens`, membuat screen saat akses pertama, lalu memakai instance cache pada akses berikutnya.
Tombol dan screen tujuan boleh berada di file yang berbeda — validasi route dilakukan setelah seluruh
file digabungkan, bukan per file.

### Langkah 3 — Opsional: jadikan initial route

Setiap Window wajib mempunyai `initialRoute` yang menunjuk screen valid. Window didefinisikan di
`Assets/ui/core.json`:

```json
"windows": {
  "main": {
    "id": "main-window",
    "type": "Window",
    "style": {
      "$ref": "styles.window"
    },
    "title": "Terminal",
    "initialRoute": "profile-manager",
    "children": []
  }
}
```

Window adalah metadata/top-level host. Isi route berada di file `Assets/ui/screens/<routeId>.json`,
bukan di `windows.*.children`. `initialRoute` di `core.json` boleh menunjuk screen dari file mana pun.

### Batas navigasi yang perlu diketahui sebelum mendesain ulang

Fakta berikut sudah diverifikasi terhadap kode; jangan mencari ulang.

**`windows.*.children` tidak pernah dirender.** `WindowContainer::ActivateRoute`
(`src/ui/containers/window_container.cpp`) mengambil root component tree dari `document_->screens`,
bukan dari `document_->windows`. `WindowComponent` memang punya `Measure`/`Arrange`/`Paint` yang
mengiterasi children, tetapi tidak ada jalur yang menjadikannya root aktif. Konsekuensinya: chrome
window persisten — tab bar, header, status bar global — belum didukung runtime. Menambahkannya berarti
mengubah `WindowContainer` agar merender window sebagai frame dengan screen aktif di dalamnya, dan itu
perubahan container (validation level 4), bukan perubahan JSON.

**Tidak ada state route aktif.** Tidak ada `viewState` yang membawa route yang sedang tampil
(`activeRoute`/`currentRoute` tidak ada di `src/`). `WindowContainer` mengetahuinya secara internal
tetapi tidak pernah mempublikasikannya sebagai binding. Jadi penanda "tab aktif" lewat
`Button.selected` belum bisa dilakukan dari JSON saja, meskipun `BooleanValue` sudah mendukung binding
(`std::variant<bool, ValueBinding>` di `resolved_ui_document.h`).

**Navigasi berantai adalah desain warisan, bukan kekeliruan.** Bentuk sekarang mengikuti hierarchy
referensi §10 `Termial-plan.md` dan pola `Open-terminal-native/src/shell/main_window.*`, di mana nav
dimiliki main window (`nav_[3]` + tombol Settings), hanya 3 route mendapat tab, dan sub-screen
(JSON Editor, Chrome Profile Manager, UI Editor) dibuka dari induknya sambil mempertahankan highlight
tab induk. Terminal saat ini mewarisi hierarchy-nya tanpa mewarisi chrome window-nya.

**Konsekuensi praktis.** Tab bar hari ini hanya bisa ditulis sebagai Container + Button di dalam setiap
file screen, yang berarti satu blok yang sama diduplikasi ke tiap file. Tidak ada mekanisme include atau
partial di schema — gate hanya menerima satu dokumen utuh.

## 4. Menambahkan component ke screen

### Text

```json
{
  "id": "status-text",
  "type": "Text",
  "style": {
    "$ref": "styles.text-body"
  },
  "text": "Ready",
  "variant": "body",
  "wrap": true,
  "align": "start"
}
```

### Button dengan action

```json
{
  "id": "refresh-button",
  "type": "Button",
  "style": {
    "$ref": "styles.button-primary"
  },
  "label": "Refresh",
  "variant": "primary",
  "events": {
    "click": {
      "action": "refresh-profiles",
      "payload": {
        "mode": "manual"
      }
    }
  }
}
```

### Input

```json
{
  "id": "search-input",
  "type": "Input",
  "style": {
    "$ref": "styles.input"
  },
  "layout": {
    "width": "fill"
  },
  "valueBinding": {
    "$bind": "viewState.searchText"
  },
  "placeholder": "Cari…",
  "maxLength": 256,
  "events": {
    "changed": {
      "action": "search-text-changed",
      "payload": {}
    }
  }
}
```

Untuk event `changed`, runtime menambahkan `payload.value` berisi text terbaru. Runtime payload
menggantikan key static yang sama jika key tersebut juga ditulis di JSON.

### Component dan event yang tersedia

| Type | Field utama | Event |
| --- | --- | --- |
| `Screen` | `routeId`, `children` | — |
| `Container` | `direction`, `gap`, `padding`, `align`, `justify`, `wrap`, `overflow`, `children` | — |
| `Text` | `text`/`textBinding`, `variant`, `wrap`, `selectable`, `align` | — |
| `Button` | `label`, `variant`, `selected`, `tabStop` | `click` |
| `Input` | `valueBinding`, `mode`, `placeholder`, `readOnly`, `password`, `maxLength`, `scrollbar` | `changed`, `focus`, `blur`; `commit` tersedia di schema tetapi harus diuji sebelum dipakai |
| `Combo` | `itemsBinding`, `selectedValueBinding`, `placeholder`, popup limits | `opened`, `closed`, `changed` |
| `Checkbox` | `label`, `checkedBinding`, `triState`, `tabStop` | `changed` |
| `Toggle` | `label`, `checkedBinding`, `variant`, `tabStop` | `changed` |
| `Card` | `interactive`, `selected`, `tabStop`, `children` | `activate` |
| `List` | `itemsBinding`, `itemTemplate`, `selectedIdBinding`, row/selection/scroll fields | `selectionChanged`, `activate` |
| `Scrollbar` | `orientation`, `thickness`, `minThumbLength`, steps | — |
| `Dialog` | `title`, `width`, `maxHeight`, `dismissPolicy`, `children` | `accept`, `cancel`, `dismiss` |

Runtime menambahkan payload berikut:

- `Input.changed`: `value`;
- `Checkbox.changed` dan `Toggle.changed`: `checked`;
- `Combo` dan `List`: `selectedIndex` dan `selectedValue`.

## 5. Menambahkan business action

JSON hanya menyebut semantic action ID. Logic sebenarnya berada di handler C++.

### Langkah 1 — Pakai action ID di JSON

```json
"events": {
  "click": {
    "action": "save-profile",
    "payload": {
      "source": "profile-manager"
    }
  }
}
```

### Langkah 2 — Registrasikan handler feature

Jangan menambah branch action di `WindowContainer`. Tambahkan logic ke feature yang tepat di
`src/logic`, lalu buat adapter kecil di `src/application/adapters`:

```cpp
bool RegisterSettingsAdapter(
    ui::application::StubApplicationBridge& bridge,
    const std::shared_ptr<logic::CoreApplication>& logic) {
    return bridge.ReplaceAction(
        "apply-settings",
        [&bridge, logic](const ui::application::UiEvent& event)
            -> std::optional<ui::application::UiPatch> {
            const logic::core::Status status = logic->SetTheme(L"dark");
            ui::application::UiPatch patch;
            patch.view_state["viewState.profileStatus"] = ToUtf8(status.text);
            patch.request_repaint = true;
            return patch;
        });
}
```

Setiap action JSON harus lebih dahulu mempunyai fallback di `StubApplicationBridge`. Adapter feature
memakai `ReplaceAction`, sehingga feature nyata dapat dipasang atau dilepas tanpa membuat action JSON
hilang. Adapter hanya menerjemahkan payload, typed request/result, dan `UiPatch`; validation,
persistence, scan, atau process launch tetap berada di `src/logic`.

Registration dipanggil di composition root setelah bridge dibuat:

```cpp
auto application_bridge = std::make_shared<ui::application::StubApplicationBridge>();
auto logic = std::make_shared<logic::CoreApplication>();
logic->Initialize();
if (!RegisterSettingsAdapter(*application_bridge, logic)) {
    throw std::runtime_error("Settings action registration failed.");
}
```

Adapter menangkap `shared_ptr<logic::CoreApplication>`, sehingga lifetime logic mengikuti bridge.

`RegisterAction` tetap menolak duplicate action. `ReplaceAction` hanya berhasil untuk fallback action
yang memang sudah terdaftar.

Bridge stub membagi fallback registration per feature (`RegisterTerminalFeature`,
`RegisterJsonInjectFeature`, `RegisterJsonEditorFeature`, `RegisterChromeLauncherFeature`,
`RegisterChromeProfileManagerFeature`, `RegisterSettingsFeature`, dan `RegisterUiEditorFeature`).
Fallback tidak membaca atau menulis file nyata, menjalankan process, membuka browser, atau melakukan
network request. Adapter nyata menggantinya saat composition root dibuat.

### Data yang diterima handler

`UiEvent` menyediakan:

- `source.window_instance_id`, `screen_instance_id`, dan `component_instance_id`;
- `source.window_id`, `route_id`, dan `component_id`;
- `event_type`, misalnya `click` atau `changed`;
- semantic `action`;
- payload static JSON yang sudah digabung dengan runtime payload;
- `config_generation`.

Business logic tidak menerima `HWND`, device context, atau pointer component.

### Hasil yang dapat dikembalikan handler

`UiPatch` saat ini menyediakan:

- `target`: identity window/screen/component asal event;
- `config_generation`: generation config yang menghasilkan event;
- `generation`: urutan patch monotonik dari bridge;
- `view_state`: perubahan state semantic;
- `window_title`: mengganti title top-level window;
- `route_id`: navigation ke screen lain;
- `dialog_request`: open/save/discard/cancel dialog;
- `close_save_result`: hasil persistence Save untuk close transaction, lengkap dengan source identity
  dan config generation;
- `request_repaint`: meminta render ulang.

Handler feature cukup mengisi perubahan semantic. `StubApplicationBridge::Dispatch` menempelkan
`target`, `config_generation`, dan `generation` setelah handler berhasil. `WindowContainer` menolak
patch yang target-nya berbeda dari source event, berasal dari config generation lain, bernilai nol,
atau generation-nya tidak lebih baru daripada patch terakhir. Jangan mengarang identity/generation
di handler.

`view_state` belum merupakan binding engine universal yang otomatis menulis semua property component.
Jika feature menambah binding baru, bridge harus mengeksposnya melalui `ResolveStringItems` atau
`ResolveStringValue`, dan component terkait harus mempunyai refresh behavior yang sesuai.

### Process-level route intent

Navigation dari `UiPatch.route_id` dimulai sebagai same-window intent, tetapi tetap diteruskan ke
`ApplicationContainer` untuk menjaga `reuse-per-route`: target yang sama pada window pemanggil adalah
no-op, matching visible window lain diaktifkan, dan matching retained window dipulihkan tanpa mengganti
route pemanggil. Hanya target yang belum dimiliki window lain yang mengganti route window pemanggil.
Intent dari second launch, tray, atau taskbar masuk melalui `ApplicationContainer::OpenExternalRoute`;
matching visible/retained window direuse dan hanya route tanpa match yang membuat top-level
`WindowContainer` baru. Route harus sudah ada di object `screens`; route invalid ditolak sebelum masuk
IPC queue. Jangan memanggil `CreateWindowEx`, `Shell_NotifyIcon`, atau mencari HWND route dari
feature/action handler.

`ApplicationInfrastructureWindow` adalah hidden top-level tool window, bukan `HWND_MESSAGE`, agar satu
receiver yang sama dapat menerima `WM_COPYDATA`, tray callback, process-global theme/settings signal,
dan broadcast `TaskbarCreated`. `WM_DPICHANGED` tetap ditangani masing-masing route window/popup.

### Reload generation dan state reconciliation

Jangan mengganti `ResolvedUiDocument` atau mengosongkan screen cache langsung dari feature. Serahkan
candidate document yang sudah valid ke `WindowContainer::ReloadDocument`. Generation candidate wajib
lebih tinggi daripada generation aktif.

Reload menormalkan transient UI lebih dahulu: popup ditutup, modal stack di-drain, native peer/IME
disuspend, lalu runtime state diambil berdasarkan stable component ID. Reconciliation mempertahankan
draft dan baseline Input, caret/selection, scroll, selection Combo/List, checkbox/toggle/card state,
serta focus target untuk identity yang masih ada. Screen inactive tetap lazy dalam bentuk pending
snapshot sampai route dibuka lagi. State route/component yang hilang dibuang dan active route yang
hilang fallback ke `windows.<id>.initialRoute`.

Gunakan `WindowContainer::IsDirty` atau `dirty_participant_count` untuk window-level dirty status;
business layer tidak perlu membaca draft component. Koordinasi candidate dari `UiConfigGate` ke semua
window akan dimiliki `ApplicationContainer`, bukan screen atau action handler.

### Close, retain, dan Exit

`WM_CLOSE` adalah process-level lifecycle intent. Jangan menghancurkan `HWND` dari feature handler.
`ApplicationContainer` memilih transaksi berikut:

- bila masih ada route window lain yang reachable, hanya target yang melewati `PrepareClose` lalu
  `CommitClose`;
- bila target adalah window terakhir dan tray tersedia tanpa retained window, instance yang sama
  disuspend dan disimpan sebagai retained window tanpa prompt, termasuk ketika masih dirty;
- bila slot retained sudah terisi, retained window lama dipulihkan dan diprepare lebih dahulu. Save
  atau Discard menghancurkan yang lama lalu menaruh window terbaru di slot; Cancel mempertahankan
  retained window lama dan membiarkan window terbaru terbuka;
- Exit memulihkan retained window lalu menjalankan `PrepareCloseAll`. Tidak ada window dihancurkan
  sebelum semuanya siap; Cancel mengembalikan staged discard dan visibility semula.

Ketika retained, component tree dan draft tetap hidup, tetapi native peer disuspend serta persistent
DIB dan native GDI lease dilepas. Restore memakai tree yang sama, memperoleh resource lagi, merender
satu frame lengkap, baru menampilkan window.

Close confirmation memakai definisi JSON `save-discard-dialog`. `WindowContainer` memasang instance
runtime dari definisi itu pada setiap screen yang dibuat lazy, sehingga dialog dapat muncul tanpa
mengganti route aktif. ID tersebut harus tetap unik di dalam setiap screen tree.

Handler Save wajib melakukan persistence business logic terlebih dahulu. Hanya setelah berhasil,
kembalikan `UiPatch.close_save_result` dengan `source` dari `UiEvent`, `config_generation` yang sama,
dan `success = true`; hasil stale atau milik window/screen/route lain diabaikan. Implementasi stub
sekarang selalu mengembalikan sukses hanya untuk membuktikan wiring—ganti handler itu saat backend
nyata dipasang. Discard distage oleh UI dan baru permanen pada `CommitClose`; Cancel melakukan rollback.

## 6. Dialog dari JSON

Dialog ditempatkan di dalam screen yang memilikinya, kemudian dibuka melalui action handler yang
mengembalikan `DialogRequest`.

```json
{
  "id": "delete-profile-dialog",
  "type": "Dialog",
  "style": {
    "$ref": "styles.dialog"
  },
  "title": "Hapus profile?",
  "width": 480,
  "maxHeight": 720,
  "dismissPolicy": {
    "escape": true,
    "outsideClick": false,
    "explicitAction": true
  },
  "children": []
}
```

Gunakan pola registration pada `RegisterDialogFeature` sebagai referensi untuk membuat open/accept/
discard/cancel handler. Jangan memakai native message box untuk dialog yang merupakan bagian dari
component flow.

## 7. Menambahkan component type baru

Lakukan ini hanya jika screen tidak dapat disusun dari component yang sudah tersedia.

1. Tambahkan `ComponentType` dan properties di `resolved_ui_document.h`.
2. Tambahkan nama type, allowed field, allowed event, dan resolver properties di
   `resolved_ui_document.cpp`.
3. Buat class component di `src/ui/components/<type>/` yang mengikuti lifecycle `Measure`, `Arrange`,
   `Paint`, input, focus, native-peer, suspend/resume, dan automation yang diperlukan.
4. Registrasikan factory di `ComponentRegistry`.
5. Tambahkan source/header ke `src/Terminal.vcxproj` dan source yang diperlukan ke
   `tests/TerminalTests.vcxproj`.
6. Tambahkan schema, registry, behavior, UIA, dan render tests yang relevan.
7. Jika perubahan memutus compatibility config, ubah contract/version secara eksplisit sesuai
   `Termial-plan.md`; jangan menyelundupkan perubahan incompatible sebagai field V1 biasa.

## 8. Build dan verification

Jalankan dari root repository. `tools\Merge-UiConfig.ps1` dipanggil otomatis oleh kedua project
sebelum `ResourceCompile`, jadi mengedit `Assets/ui/**` cukup diikuti build biasa. Kesalahan JSON,
`routeId` yang tidak cocok dengan nama file, atau `screens` di `core.json` menggagalkan build dengan
menyebut nama file yang salah:

```powershell
.\tools\Build.ps1 -Configuration Debug
.\build\x64\Debug\TerminalTests.exe

.\tools\Build.ps1 -Configuration Release
.\build\x64\Release\TerminalTests.exe
```

Untuk memeriksa hasil gabungan tanpa build penuh:

```powershell
.\tools\Merge-UiConfig.ps1
```

Atau build dan test dua configuration:

```powershell
.\tools\Test.ps1 -Configuration All
```

Smoke route dari executable Release:

```powershell
.\build\x64\Release\Terminal.exe --route profile-manager
.\build\x64\Release\Terminal.exe --route terminal
.\build\x64\Release\Terminal.exe --exit
```

Smoke runtime/visual otomatis (route, multi-window, retained, interaksi keyboard,
combo popup, dialog, list scroll, resource counter, dan live switch Dark/Light).
`-ThemeMatrix` menambah High Contrast; `-ThemeOnly` hanya menjalankan matrix theme.
Theme dipulihkan otomatis, dan script menolak berjalan bila executable target sudah
aktif agar tidak mematikan instance yang mungkin membawa draft user:

```powershell
.\tools\Smoke-Runtime.ps1
.\tools\Smoke-Runtime.ps1 -ThemeOnly
```

Sebelum commit:

```powershell
git diff --check
git status --short
```

### Checklist selesai

- `tools\Merge-UiConfig.ps1` lulus dan nama file screen cocok dengan `routeId`-nya;
- config embedded dapat di-resolve;
- screen route baru ada di `ResolvedUiDocument.screens`;
- startup `--route <route>` membuka screen yang benar;
- button navigation dua arah bekerja;
- action handler dipanggil dan menerima source/payload yang benar;
- unknown/duplicate action mempunyai test;
- focus, keyboard, modal, native peer, dan UIA tetap bekerja bila tersentuh perubahan;
- Debug dan Release build/test hijau;
- visible/runtime smoke dilakukan untuk perubahan UI;
- package/installed smoke diulang jika asset embedded atau release payload berubah.

## 9. Error yang paling sering terjadi

| Error | Penyebab umum | Perbaikan |
| --- | --- | --- |
| build gagal di `Merge-UiConfig` | `routeId` tidak cocok nama file, `screens` ada di `core.json`, nama file bukan lower-kebab, atau JSON rusak | Baca nama file yang disebut pesan error lalu perbaiki file itu |
| screen tidak muncul sama sekali | file tidak berada di `Assets/ui/screens/` atau ekstensinya bukan `.json` | Pindahkan file ke folder yang benar |
| `invalid-route` | route ID bukan lower-kebab atau `initialRoute` tidak ada di `screens` | Samakan nama file screen, `routeId`, navigation payload, dan `initialRoute` |
| `route-mismatch` | `routeId` berbeda dari nama file screen | Gunakan nilai yang sama |
| `duplicate-component-id` | dua component dalam satu tree memakai ID sama | Beri ID stabil dan unik |
| `missing-reference` | style/token `$ref` tidak tersedia | Gunakan ref yang sudah ada atau definisikan pada semua theme di `core.json` |
| `unsupported-event` | event tidak didukung component type | Gunakan event pada tabel atau implementasikan contract baru |
| `invalid-action` | action bukan lower-kebab-case | Gunakan format seperti `save-profile` |
| action tidak bereaksi | handler belum diregistrasikan | Tambahkan feature registration dan test |
| screen kosong | content masih diletakkan di `windows.*.children` | Pindahkan component tree ke file screen-nya |

## 10. Menjaga playbook tetap benar

Jika schema, event envelope, action registry, route lifecycle, build command, atau validation gate
berubah, update playbook pada perubahan yang sama. Setelah Phase 5 benar-benar selesai dan workflow
ini sudah dipakai berulang, playbook dapat dijadikan sumber untuk skill generator khusus Terminal.
Skill tersebut tidak boleh menjadi architecture plan kedua.
