#include "features/app_settings.h"

#include "platform/paths.h"
#include "platform/startup.h"
#include "storage/settings.h"

namespace features {
namespace {

// ui::config equivalents are not available in core — the ui_config_draft feature
// owns that domain. app_settings provides stubs that return sensible defaults;
// the frontend overrides the behaviour by calling ui_config_draft directly.
bool g_ui_config_in_use = false;
std::wstring g_ui_config_last_error;
std::wstring g_ui_config_path;

}  // namespace

const std::wstring& CurrentThemeToken() {
    const std::wstring& token = storage::CurrentSettings().theme;
    static const std::wstring kDark = L"dark";
    return token.empty() ? kDark : token;
}

core::Status SetTheme(const std::wstring& token) {
    const std::wstring& current = CurrentThemeToken();
    if (token == current) return core::NoStatus();
    const std::wstring normalized = (token == L"light") ? L"light" : L"dark";
    storage::CurrentSettings().theme = normalized;
    storage::SaveSettings();
    return core::Success(L"Theme changed to " + normalized + L".");
}

bool IsStartWithWindowsEnabled() {
    return platform::IsStartWithWindowsEnabled();
}

core::Status SetStartWithWindows(bool enabled) {
    std::wstring error;
    if (!platform::SetStartWithWindows(enabled, &error))
        return core::Error(L"Cannot change the startup entry: " + error);
    storage::CurrentSettings().start_with_windows = enabled;
    storage::SaveSettings();
    return core::Success(enabled
        ? L"Start with Windows is on; the app will open in the tray."
        : L"Start with Windows is off.");
}

bool SyncStartWithWindows() {
    const bool os_state = platform::IsStartWithWindowsEnabled();
    const bool stored   = storage::CurrentSettings().start_with_windows;
    storage::CurrentSettings().start_with_windows = os_state;
    // Per plan: persist on read so the file stays authoritative.
    if (os_state != stored) storage::SaveSettings();
    return os_state != stored;
}

std::wstring AppDataDirPath() { return paths::AppDataDir(); }

std::wstring UiConfigPath() {
    return g_ui_config_path.empty() ? paths::UiConfigFile() : g_ui_config_path;
}

bool UiConfigInUse() { return g_ui_config_in_use; }
std::wstring UiConfigLastError() { return g_ui_config_last_error; }

// Called by the frontend after it has applied a ui_config_draft load so this
// module can report the correct status.
void SetUiConfigState(bool in_use, const std::wstring& last_error,
                      const std::wstring& path) {
    g_ui_config_in_use   = in_use;
    g_ui_config_last_error = last_error;
    g_ui_config_path     = path;
}

core::Status AppSettingsStatus() {
    if (!g_ui_config_last_error.empty())
        return core::Error(g_ui_config_last_error);
    return g_ui_config_in_use
        ? core::Info(L"ui.json is in use.")
        : core::Info(L"Using the built-in UI defaults.");
}

}  // namespace features
