# Terminal Current Structure and Migration Handoff

## Purpose

Dokumen ini menjelaskan struktur codebase Terminal saat ini dan pemindahan yang harus dilakukan menuju struktur modular di `New-Plan.md`. Agent baru harus membaca `Current.md` lalu `New-Plan.md` sebelum memindahkan file.

Tugas ini adalah **pemindahan struktur saja**. Jangan mengubah tampilan, lifecycle window, tab behavior, routing, business logic, installer contract, updater behavior, atau dependency.

## Repository state

- Repository utama: `C:\VSCODE\Teminal`.
- `Open-terminal`, `Open-terminal-native`, dan `Open-terminal-core` hanya referensi; jangan diedit atau dijadikan dependency.
- `Playbook.md` sudah dihapus.
- `Termial-plan.md` juga sudah berada dalam status deleted; jangan dipulihkan.
- Target structure berada di `New-Plan.md`.
- UI source tetap berada di `Assets\ui`.
- Business logic tetap berada di `src\logic`.

## Current structure

```text
Terminal/
├─ Assets/
│  ├─ Terminal-Icon.png
│  ├─ terminal.ico
│  └─ ui/
│     ├─ core.json
│     └─ screens/
├─ src/
│  ├─ app/
│  │  ├─ app_identity.h
│  │  └─ app_version.h
│  ├─ application/
│  │  ├─ application_container.*
│  │  ├─ application_infrastructure_window.*
│  │  └─ adapters/
│  ├─ instrumentation/
│  ├─ logic/
│  │  ├─ application/
│  │  ├─ core/
│  │  ├─ features/
│  │  ├─ platform/
│  │  └─ storage/
│  ├─ platform/
│  │  ├─ app_paths.*
│  │  ├─ single_instance.*
│  │  ├─ updater.*
│  │  └─ windows_runtime.*
│  ├─ rendering/
│  ├─ ui/
│  │  ├─ accessibility/
│  │  ├─ application/
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
│  │  │  ├─ tabs/
│  │  │  ├─ text/
│  │  │  ├─ toggle/
│  │  │  └─ window/
│  │  ├─ config/
│  │  ├─ containers/
│  │  │  ├─ logical_focus_coordinator.*
│  │  │  ├─ modal_overlay_stack.*
│  │  │  ├─ overlay_plane.*
│  │  │  └─ window_container.*
│  │  └─ theme/
│  ├─ app.manifest
│  ├─ app.rc
│  ├─ main.cpp
│  ├─ resource.h
│  └─ Terminal.vcxproj
├─ tools/
├─ tests/
├─ .github/workflows/
└─ Terminal.sln
```

## Current ownership

### Window, tab, and route logic

Saat ini tersebar di:

- `src/application/application_container.*`: process lifecycle, multi-window registry, secondary-launch IPC, tray, route reuse, create/destroy window.
- `src/application/application_infrastructure_window.*`: hidden infrastructure HWND dan dispatch process-level message.
- `src/ui/containers/window_container.*`: satu top-level HWND, active screen, screen cache, layout, rendering, input, route request, close flow.
- `src/ui/containers/logical_focus_coordinator.*`: focus traversal.
- `src/ui/containers/modal_overlay_stack.*`: modal lifecycle.
- `src/ui/containers/overlay_plane.*`: overlay rendering.
- `src/ui/components/tabs/tabs_component.*`: tab measure, paint, pointer input, dan route selection.
- `src/ui/components/component.*`: `ComponentHost`, `RouteTabDefinition`, serta callback tab/route.
- `src/ui/components/component_registry.*`: factory komponen termasuk `Tabs`.
- `src/ui/config/resolved_ui_document.*`: parsing `tabLabel`, `showInTabs`, screen, window, dan component type.
- `Assets/ui/core.json`: composition `window-frame`, `window-chrome`, `route-tabs`, dan `screen-host`.

### Native runtime

Saat ini tersebar di `src/platform`, `src/rendering`, `src/ui`, `src/instrumentation`, resource Win32 di root `src`, serta sebagian `src/application`.

### Installer and updater

- Runtime updater: `src/platform/updater.*`.
- Packaging/update scripts: `tools/Build-Package.ps1`, `Get-NextVersion.ps1`, `Get-ProjectVersion.ps1`, `Set-ProjectVersion.ps1`, `Invoke-InstalledUpdateGuest.ps1`, dan `Test-InstalledUpdate.ps1`.
- Release automation: `.github/workflows/release.yml`.

## Required moves

| Current path | Target path | Action |
|---|---|---|
| `src/application/application_container.*` | `src/native/window-shell/` | Move without changing behavior or namespace. |
| `src/application/application_infrastructure_window.*` | `src/native/window-shell/` | Move beside process/window lifecycle owner. |
| `src/ui/containers/window_container.*` | `src/native/window-shell/` | Move top-level container implementation. |
| `src/ui/containers/logical_focus_coordinator.*` | `src/native/window-shell/` | Move window-owned focus handler. |
| `src/ui/containers/modal_overlay_stack.*` | `src/native/window-shell/` | Move window-owned modal handler. |
| `src/ui/containers/overlay_plane.*` | `src/native/window-shell/` | Move window-owned overlay handler. |
| `src/ui/components/tabs/tabs_component.*` | `src/native/window-shell/` | Keep tab component beside its route/window handler. |
| Route handling currently inside `ApplicationContainer` and `WindowContainer` | `src/native/window-shell/route_handler.*` | Extract only after the files above compile from their new paths; preserve exact behavior. |
| `src/ui/components/*` except `tabs/` | `src/native/components/` | Move reusable primitives together. |
| `src/rendering/*` | `src/native/rendering/` | Mechanical move. |
| `src/ui/accessibility/*` | `src/native/accessibility/` | Mechanical move. |
| `src/platform/*` except `updater.*` | `src/native/platform/` | Move app shell and Windows integration. |
| `src/ui/config/*` | `src/native/config/` | Move config resolution/gate. |
| `src/ui/theme/*` | `src/native/theme/` | Move native theme adapter. |
| `src/instrumentation/*` | `src/native/instrumentation/` | Move runtime instrumentation. |
| `src/app/*`, `src/app.manifest`, `src/app.rc`, `src/resource.h` | `src/native/resources/` | Move native identity, version, manifest, RC, and resource header together. |
| `src/ui/application/*` | `src/application/bridge/` | Keep UI/business bridge beside adapters. |
| `src/application/adapters/*` | `src/application/adapters/` | Keep in place. |
| `src/logic/*` | `src/logic/` | Keep in place. |
| `src/platform/updater.*` | `src/updater/` | Separate runtime updater from generic platform code. |
| Packaging/version/update scripts listed above | `distribution/scripts/` | Move release-only scripts together. |
| Generated packages and feed layout | `distribution/packaging/` and `distribution/update-feed/` | Update output/input paths without changing artifact contract. |
| Installer-specific files | `distribution/installer/` | Keep installer inputs in one location when present. |

## Migration order

1. Preserve the existing worktree; do not revert user changes.
2. Create the target directories from `New-Plan.md`.
3. Move the complete `window-shell` group first: application container, infrastructure window, window container, focus, modal, overlay, and tabs.
4. Update includes and `Terminal.vcxproj`; build before moving the next group.
5. Move reusable components, rendering, accessibility, platform, config, theme, instrumentation, and resources into `src/native`.
6. Move `src/ui/application` into `src/application/bridge`; keep adapters and logic unchanged.
7. Move runtime updater into `src/updater`.
8. Move release-only scripts into `distribution`; update workflow and script references.
9. Only after all moves compile, optionally extract route forwarding into `route_handler.*` without changing its decisions.
10. Remove directories that become empty and confirm no old include/path remains.

## Files that must be updated during moves

- `src/Terminal.vcxproj` source/header/resource paths.
- `tests/TerminalTests.vcxproj` and `tests/performance/TerminalPerformance.vcxproj` paths.
- All C++ `#include` paths.
- `tools/Build.ps1`, `build.cmd`, and scripts that reference moved files.
- `.github/workflows/ci.yml` and `.github/workflows/release.yml` when script paths move.
- Resource include paths in `app.rc` and project resource entries.
- Documentation links in `Current.md` and `New-Plan.md` after migration completes.

Do not rename namespaces during the physical move. Namespace cleanup is a separate task after the new structure is stable.

## Behavior that must remain unchanged

- `route-tabs` remains permanently inside every `WindowContainer`.
- Tab items continue to be generated from screen definitions using `tabLabel` and `showInTabs`.
- Only the selected screen is active in each window.
- Screen composition remains JSON-driven.
- Secondary launch, IPC, route reuse/new-window behavior, tray, close, updater, and installer behavior must not be redesigned during migration.
- Business actions continue through application adapters and never move into UI components.
- No screen content or visual design is added as part of this task.

## Completion criteria

- All files are located according to the target mapping.
- No source include or project entry points to an old path.
- JSON UI files and business logic remain unchanged except when a path reference must change.
- Debug and Release builds succeed.
- `git diff --check` succeeds.
- The diff contains structural/path changes only; any behavioral change must be removed or handled as a separate task.

Do not add packaging, installed-update, performance, accessibility, or visual test work to this migration. Build success and clean path references are sufficient for the structural task.
