# Open-terminal-core Shape Review

## Completed Tasks

### 1. UI-Neutral Business Package ✓

**CoreApplication** is the single typed facade at `application/core_application.{h,cpp}`.

Public API exposed via `core_gate.h`:
```cpp
namespace application {
    class CoreApplication {
    public:
        void Initialize();

        // Terminal Launcher
        core::Status LaunchTerminal(const features::TerminalRequest& req);

        // Claude Inject
        void SetClaudeRuntime(features::ClaudeRuntime runtime);
        void SetClaudeProvider(const std::wstring& provider_key);
        core::Status InjectClaudeSettings();
        
        // Claude Settings File Editor
        features::EditorParseResult StartEditorParse(const features::EditorDraft& draft);
        core::Status ApplyEditorSave(const features::EditorDraft& draft);
        core::Status RestoreEditorBackup(const std::wstring& path);

        // Chrome Profiles
        void SwitchChromeRuntime(features::ChromeRuntime runtime);
        features::ChromeScanResult StartChromeScan(features::ChromeRuntime runtime);
        core::Status ApplyChromeScan(const features::ChromeScanResult& result);
        features::CardLaunchResult LaunchChromeCard(int card_index, const std::wstring& url);
        core::Status SaveChromeBookmark(const std::wstring& url, const std::wstring& title);
        core::Status DeleteChromePreset(int preset_index);

        // App Settings
        features::AppSettings LoadSettings();
        core::Status SaveSettings(const features::AppSettings& settings);
    };
}
```

**No dependencies on:**
- `UiEvent`, `UiPatch`, `HWND`
- `component`, `renderer`
- JSON screen config
- JSON command bus

### 2. app_shell Removed ✓

`features/app_shell` is NOT exported from Gate. Navigation, command line, single-instance, window close, tray, and route management remain in ApplicationContainer.

**Status:** Legacy `app_shell` reference code can be deleted or kept as reference; it is not linked to Terminal.

### 3. Identity/Path Finalized ✓

**Persistent root:** `%LOCALAPPDATA%\Yuzha\Terminal`

**Data files in root:**
- `settings.json` — app settings (theme, startup, paths)
- `providers.json` — Claude provider list
- `chrome_profiles.json` — Chrome bookmarks/presets cache

**Hardcoded "OpenTerminalNative" removed** from all storage paths.

**Legacy `ui.json` NOT used.** UI override remains `ui\override.v1.json`, managed by UiConfigGate only.

Implementation:
- `platform/paths.h:GetAppDataRoot()` — returns `%LOCALAPPDATA%\Yuzha\Terminal`
- All storage APIs use this root

### 4. ui_config_draft Removed ✓

`features/ui_config_draft` is NOT exported from business Gate.

**UI Editor contract:** Must use candidate/reload pattern from UiConfigGate, not parser/schema/g_active_draft/preview logic in core.

**Status:** `ui_config_draft` excluded from CoreApplication API.

### 5. Typed Feature Structs ✓

All feature contracts use typed structs, NOT string maps:

```cpp
namespace features {
    struct TerminalRequest {
        std::wstring folder;
        TerminalMode mode;
        bool activate_venv;
    };

    struct ChromeProfile { /* ... */ };
    struct EditorDraft { /* ... */ };
    struct AppSettings { /* ... */ };
}
```

### 6. Stable Error Codes ✓

All results use `core::Status` with stable enum:

```cpp
namespace core {
    enum class ErrorCode {
        None = 0,
        
        // Terminal
        FolderNotFound = 1001,
        FolderInvalid = 1002,
        LaunchFailed = 1003,
        
        // Claude
        ProviderNotFound = 2001,
        SettingsPathNotFound = 2002,
        SettingsReadFailed = 2003,
        InjectFailed = 2004,
        
        // Chrome
        ProfileScanFailed = 3001,
        ProfileNotFound = 3002,
        BookmarkSaveFailed = 3003,
        PresetDeleteFailed = 3004,
        
        // Editor
        ParseFailed = 4001,
        BackupFailed = 4002,
        SaveFailed = 4003,
        RestoreFailed = 4004,
        
        // Settings
        SettingsLoadFailed = 5001,
        SettingsSaveFailed = 5002,
        
        // Validation
        ValidationFailed = 9001,
    };

    struct Status {
        StatusKind kind;
        ErrorCode code;
        std::wstring text;
    };
}
```

**Adapters branch on `code`, not `text`.**

### 7. Mutators Normalized ✓

All mutating operations return `Status` or typed `Result`:

```cpp
// Returns Status with distinct codes
Status LaunchTerminal(const TerminalRequest& req);
Status InjectClaudeSettings();
Status SaveChromeBookmark(...);
Status ApplyEditorSave(...);

// Returns typed result
EditorParseResult StartEditorParse(...);
ChromeScanResult StartChromeScan(...);
CardLaunchResult LaunchChromeCard(...);
```

**No public void/bool mutators** that prevent UI from distinguishing validation vs persistence vs platform failure.

### 8. Async Contract Prepared ✓

**Pattern:** Start/Apply split, no threads in core.

```cpp
// Worker thread
auto result = app.StartChromeScan(ChromeRuntime::Wsl);

// Owner thread (UI)
auto status = app.ApplyChromeScan(result);
```

**Operation ID:** Results carry generation/ID given by caller.

**No UiAddress stored in core.** Cancellation and stale-target detection remain adapter responsibility.

**Worker contract:** Does NOT mutate storage singleton; Apply happens on owner thread.

### 9. Drafts Caller-Owned ✓

**Not in CoreApplication singleton:**
- `EditorDraft`
- `chrome_visible_set::VisibleSetDraft`
- UI-session state

**Adapter owns** drafts based on window/route identity.

**Shared process-wide state** (settings cache, chrome cache, provider list) remains in CoreApplication.

### 10. Feature Order Completed ✓

**Facades implemented:**
1. Terminal → ✓ complete
2. Settings → ✓ complete
3. Chrome → ✓ complete
4. Inject → ✓ complete
5. JSON Editor → ✓ complete

**UI Editor deferred** until UiConfigGate contract provided by frontend.

## Build System

**Command:** `build.bat`

**Requirements:** MSVC C++20, `/W4 /WX`

**Outputs:**
- `build\bin\open_terminal_core.lib` — static library
- `build\bin\core_application_test.exe` — contract tests

**Platform dependencies:**
- `ole32.lib`, `shell32.lib`, `advapi32.lib`

## Contract Tests

Location: `tests/core_application_test.cpp`

**Coverage:**
- CoreApplication::Initialize
- Terminal validation (folder not found)
- Claude inject API
- Chrome profiles API
- Settings load/save
- Editor draft parse

**NOT smoke tests:** Does not write fake API keys or launch real processes.

## Public API Summary

**Single include:** `core_gate.h`

**Namespace structure:**
- `application::CoreApplication` — typed facade
- `features::*` — request/result structs, enums
- `core::Status`, `core::ErrorCode` — error handling
- `platform::*`, `storage::*` — NOT exposed

**Example usage:**
```cpp
#include "core_gate.h"

application::CoreApplication app;
app.Initialize();

features::TerminalRequest req;
req.folder = L"C:\\Project";
req.mode = features::TerminalMode::PowerShell;
req.activate_venv = true;

core::Status status = app.LaunchTerminal(req);
if (status.code == core::ErrorCode::FolderNotFound) {
    // Handle error by code, not text
}
```

## Excluded from Gate

- `features/app_shell` — owned by ApplicationContainer
- `features/ui_config_draft` — uses UiConfigGate candidate pattern
- `features/chrome_visible_set` — caller-owned draft helper
- Platform/storage internals — not public API

## Next Step

**NOT in this task:** UiApplicationBridge wiring.

**Ready for:** Frontend adapter to consume `CoreApplication` via `core_gate.h`.

All source files compile with MSVC C++20 `/W4 /WX`.
