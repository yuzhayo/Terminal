// App settings — theme persistence, start-with-Windows, UI config status.
// No UI types. The frontend maps the theme token to its own palette.
#pragma once
#include <string>

#include "core/status.h"

namespace features {

// Returns "dark" or "light". Never empty.
const std::wstring& CurrentThemeToken();

// Persists a new theme token. Does NOT apply it live — the frontend handles that.
// No-op and returns NoStatus when the token is unchanged.
core::Status SetTheme(const std::wstring& token);  // "dark" or "light"

// Reads the current state from the OS registry (authoritative source).
bool IsStartWithWindowsEnabled();

// Writes the registry key and mirrors the value into settings.
// On failure: nothing is mutated, nothing saved.
core::Status SetStartWithWindows(bool enabled);

// Re-reads the OS state and mirrors it into settings (without saving).
// Call on screen entry. Returns true when the value changed since last read.
bool SyncStartWithWindows();

// Paths for display only (no editing).
std::wstring AppDataDirPath();
std::wstring UiConfigPath();

// Status precedence: if ui::config has a pending last-error, return it as an
// Error status. Otherwise return Info "ui.json is in use." / "Using built-in defaults."
// Expose the two source calls so the frontend can compose this without knowing the
// ui::config namespace.
bool UiConfigInUse();
std::wstring UiConfigLastError();
core::Status AppSettingsStatus();

// Called by the frontend after a ui_config_draft load so this module can report
// the correct status on the settings screen.
void SetUiConfigState(bool in_use, const std::wstring& last_error,
                      const std::wstring& path);

}  // namespace features
