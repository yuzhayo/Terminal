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
core::Status SetTheme(const std::wstring& token);  // "system", "dark", or "light"

bool ConfirmBeforeRun();
core::Status SetConfirmBeforeRun(bool enabled);

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

}  // namespace features
