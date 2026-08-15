# Open-terminal-core

UI-neutral business logic library for Terminal application.

## Architecture

**CoreApplication** is the single public API facade. It owns shared business state and exposes typed contracts for all features:

- **Terminal Launcher** — validates folders, detects `.venv`, launches PowerShell/Admin/WSL
- **Claude Inject** — manages providers, API keys, injects into Windows/WSL settings.json
- **Claude Settings File Editor** — loads/edits/saves/restores settings.json with backup
- **Chrome Profiles** — scans profiles, manages bookmarks/presets, launches cards
- **App Settings** — theme, start-with-windows, persistent paths

## Not exposed

- **app_shell** (navigation, command line, single-instance, tray) — owned by ApplicationContainer
- **ui_config_draft** (UI editor preview) — uses UiConfigGate candidate/reload contract
- **chrome_visible_set** — UI-session draft helper; adapter owns per-window state

## Identity

- Persistent root: `%LOCALAPPDATA%\Yuzha\Terminal`
- Data files: `settings.json`, `providers.json`, `chrome_profiles.json`
- UI override: `ui\override.v1.json` (managed by UiConfigGate)

## Error handling

All mutating operations return `core::Status` with:
- `kind` — None/Info/Success/Error
- `code` — stable `ErrorCode` enum (ValidationFailed, FolderNotFound, LaunchFailed, etc.)
- `text` — user-facing message

Adapters branch on `code`, not `text`.

## Async pattern

Blocking operations (WSL probe, Chrome scan, file I/O) use Start/Apply pattern:

```cpp
// On worker thread
auto result = app.StartChromeScan(ChromeRuntime::Wsl);

// On owner thread
auto status = app.ApplyChromeScan(result);
```

No threads run inside core. Caller owns cancellation and stale-target detection.

## Ownership

- **Shared process-wide state** — settings, chrome cache, provider list (owned by CoreApplication)
- **Caller-owned drafts** — EditorDraft, VisibleSetDraft (owned by adapter, per window/route)

## Build

Requires MSVC C++20 with `/W4 /WX`.

```cmd
build.bat
```

Produces:
- `build\bin\open_terminal_core.lib` — static library
- `build\bin\core_application_test.exe` — contract tests

## Usage

```cpp
#include "core_gate.h"

application::CoreApplication app;
app.Initialize();

// Terminal launch
features::TerminalRequest req;
req.folder = L"C:\\Project";
req.mode = features::TerminalMode::PowerShell;
req.activate_venv = true;

core::Status status = app.LaunchTerminal(req);
if (status.code == core::ErrorCode::FolderNotFound) {
    // Handle error
}

// Chrome profiles
app.SwitchChromeRuntime(features::ChromeRuntime::Windows);
auto result = app.LaunchChromeCard(0, L"https://example.com");
```

## Feature order

Facades implemented:
1. Terminal → complete
2. Settings → complete
3. Chrome → complete
4. Inject → complete
5. JSON Editor → complete

UI Editor contract deferred until UiConfigGate is provided by frontend.
