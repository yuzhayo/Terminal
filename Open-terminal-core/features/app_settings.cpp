#include "features/app_settings.h"

#include "platform/paths.h"
#include "platform/startup.h"
#include "storage/settings.h"

namespace features {

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
std::wstring UiConfigPath() { return paths::UiConfigFile(); }

}  // namespace features
