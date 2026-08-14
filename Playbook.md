# Terminal UI and Business Logic Playbook

Dokumen ini adalah panduan operasional untuk menambah screen, component, navigation, dan business
logic pada Terminal. Kontrak arsitektur tetap berada di `Termial-plan.md`; playbook ini menjelaskan
cara memakai kontrak tersebut tanpa memilih ulang stack.

## 1. Model kerja

```text
terminal.ui.default.v1.json
  -> UiConfigGate + ResolvedUiDocument
  -> WindowContainer memilih route aktif
  -> ComponentRegistry membuat component tree secara lazy
  -> component mengirim UiEvent berdasarkan events.action
  -> UiApplicationBridge
  -> UiActionRegistry mencari handler
  -> handler menjalankan logic dan mengembalikan UiPatch
  -> WindowContainer menerapkan navigation, dialog, title, dan repaint
```

Route tidak memiliki inventory hardcoded. Semua key di object `screens` menjadi route yang valid.
Screen baru dibuat ketika pertama dibuka, lalu disimpan di cache window. Screen yang tidak aktif tidak
ikut layout atau paint.

### Tingkat perubahan

| Kebutuhan | Yang diubah |
| --- | --- |
| Screen baru memakai component dan action yang sudah ada | JSON saja |
| Button baru memakai action yang sudah ada | JSON saja |
| Business action baru | JSON + satu handler registration |
| Backend/service baru | Feature module + handler registration; UI tidak perlu tahu service |
| Component visual baru | Component C++ + schema resolver + ComponentRegistry + tests |

Tidak ada arbitrary C++ atau DLL yang dimuat dari JSON. “Plug and play” berarti screen dapat disusun
dari component terdaftar dan logic dapat dipasang melalui action registry tanpa menambah `if/else` di
`WindowContainer`.

## 2. File utama

- `Assets/ui/terminal.ui.default.v1.json`: window, screen, component tree, style reference, binding,
  event, action, dan payload.
- `src/ui/config/resolved_ui_document.h/.cpp`: schema, validation, dan resolved object graph.
- `src/ui/components/component_registry.cpp`: factory component yang tersedia untuk JSON.
- `src/ui/application/ui_application_bridge.h`: envelope `UiEvent` dan hasil `UiPatch`.
- `src/ui/application/ui_action_registry.h/.cpp`: registry `action ID -> handler`.
- `src/ui/application/stub_application_bridge.h/.cpp`: composition root untuk handler aplikasi yang
  tersedia sekarang.
- `src/ui/containers/window_container.h/.cpp`: route lifecycle, lazy screen cache, input routing,
  modal, focus, UIA, layout, dan paint.
- `tests/test_main.cpp`: contract dan behavior tests.

Nested repository `Open-terminal` dan `Open-terminal-native` hanya referensi. Jangan menaruh source
baru atau mengedit implementation di dalamnya.

## 3. Membuat screen baru

Contoh berikut membuat route `profile-manager`.

### Langkah 1 — Tambahkan screen ke JSON

Tambahkan entry langsung di dalam object `screens`:

```json
"profile-manager": {
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

- key screen, `routeId`, component `id`, dan action ID memakai lower-kebab-case;
- `routeId` wajib sama dengan key screen;
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

### Langkah 3 — Opsional: jadikan initial route

Setiap Window wajib mempunyai `initialRoute` yang menunjuk screen valid:

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

Window adalah metadata/top-level host. Isi route berada di `screens`, bukan di `windows.*.children`.

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

Jangan menambah branch action di `WindowContainer`. Buat fungsi registration milik feature dan
pasang ke bridge saat composition:

```cpp
class ProfileFeature final {
public:
    bool RegisterActions(ui::application::StubApplicationBridge& bridge,
                         ProfileService& service) {
        if (!bridge.RegisterAction(
                "update-profile-name",
                [this](const ui::application::UiEvent& event)
                    -> std::optional<ui::application::UiPatch> {
                    const auto found = event.payload.find("value");
                    if (found != event.payload.end()) {
                        if (const auto* value =
                                std::get_if<std::string>(&found->second.value)) {
                            draft_name_ = *value;
                        }
                    }
                    ui::application::UiPatch patch;
                    patch.view_state["profileName"] = draft_name_;
                    return patch;
                })) {
            return false;
        }

        return bridge.RegisterAction(
            "save-profile",
            [this, &service](const ui::application::UiEvent&)
                -> std::optional<ui::application::UiPatch> {
                if (!service.Save(draft_name_)) return std::nullopt;

                ui::application::UiPatch patch;
                patch.view_state["profileStatus"] = "saved";
                patch.window_title = L"Terminal — profile saved";
                patch.request_repaint = true;
                return patch;
            });
    }

private:
    std::string draft_name_;
};
```

`ProfileService` pada contoh adalah backend milik feature, bukan class framework yang wajib dibuat.
Ia dapat diganti dengan repository, process launcher, file service, atau logic lain. Handler boleh
memanggil real logic; JSON tidak berubah ketika implementasi stub diganti dengan implementasi nyata.

Registration dipanggil di composition root setelah bridge dibuat:

```cpp
auto application_bridge =
    std::make_shared<ui::application::StubApplicationBridge>();
ProfileService profile_service;
ProfileFeature profile_feature;
if (!profile_feature.RegisterActions(*application_bridge, profile_service)) {
    throw std::runtime_error("Profile action registration failed.");
}
```

`profile_service` dan `profile_feature` harus hidup selama bridge memakai handler karena lambda
menyimpan reference/pointer ke keduanya.

Duplicate action ID ditolak oleh registry. Action ID invalid atau tidak terdaftar tidak boleh dianggap
berhasil; tambahkan test untuk handler tersebut.

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

- `view_state`: perubahan state semantic;
- `window_title`: mengganti title top-level window;
- `route_id`: navigation ke screen lain;
- `dialog_request`: open/save/discard/cancel dialog;
- `request_repaint`: meminta render ulang.

`view_state` belum merupakan binding engine universal yang otomatis menulis semua property component.
Jika feature menambah binding baru, bridge harus mengeksposnya melalui `ResolveStringItems` atau
`ResolveStringValue`, dan component terkait harus mempunyai refresh behavior yang sesuai.

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

Jalankan dari root repository:

```powershell
.\tools\Build.ps1 -Configuration Debug
.\build\x64\Debug\TerminalTests.exe

.\tools\Build.ps1 -Configuration Release
.\build\x64\Release\TerminalTests.exe
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

Sebelum commit:

```powershell
git diff --check
git status --short
```

### Checklist selesai

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
| `invalid-route` | route ID bukan lower-kebab atau `initialRoute` tidak ada di `screens` | Samakan screen key, `routeId`, navigation payload, dan `initialRoute` |
| `route-mismatch` | `screens.<key>` berbeda dari `routeId` | Gunakan nilai yang sama |
| `duplicate-component-id` | dua component dalam satu tree memakai ID sama | Beri ID stabil dan unik |
| `missing-reference` | style/token `$ref` tidak tersedia | Gunakan ref yang sudah ada atau definisikan pada semua theme |
| `unsupported-event` | event tidak didukung component type | Gunakan event pada tabel atau implementasikan contract baru |
| `invalid-action` | action bukan lower-kebab-case | Gunakan format seperti `save-profile` |
| action tidak bereaksi | handler belum diregistrasikan | Tambahkan feature registration dan test |
| screen kosong | content masih diletakkan di `windows.*.children` | Pindahkan component tree ke `screens.<route>.children` |

## 10. Menjaga playbook tetap benar

Jika schema, event envelope, action registry, route lifecycle, build command, atau validation gate
berubah, update playbook pada perubahan yang sama. Setelah Phase 5 benar-benar selesai dan workflow
ini sudah dipakai berulang, playbook dapat dijadikan sumber untuk skill generator khusus Terminal.
Skill tersebut tidak boleh menjadi architecture plan kedua.
