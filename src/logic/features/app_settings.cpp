#include "features/app_settings.h"

#include "platform/paths.h"
#include "platform/startup.h"
#include "storage/settings.h"

namespace features {

const std::wstring& CurrentThemeToken() {
    const std::wstring& token = storage::CurrentSettings().theme;
    static const std::wstring kSystem = L"system";
    return token.empty() ? kSystem : token;
}

core::Status SetTheme(const std::wstring& token) {
    const std::wstring& current = CurrentThemeToken();
    if (token == current) return core::NoStatus();
    if (token != L"system" && token != L"light" && token != L"dark")
        return core::Error(core::ErrorCode::InvalidTheme, L"Unknown theme: " + token);
    storage::CurrentSettings().theme = token;
    if (!storage::SaveSettings())
        return core::Error(core::ErrorCode::PersistenceFailed, L"Could not save the theme.");
    return core::Success(L"Theme changed to " + token + L".");
}

bool ConfirmBeforeRun() {
    return storage::CurrentSettings().confirm_before_run;
}

core::Status SetConfirmBeforeRun(bool enabled) {
    if (storage::CurrentSettings().confirm_before_run == enabled) return core::NoStatus();
    storage::CurrentSettings().confirm_before_run = enabled;
    if (!storage::SaveSettings()) {
        return core::Error(core::ErrorCode::PersistenceFailed,
                           L"Could not save the terminal confirmation preference.");
    }
    return core::Success(enabled ? L"Terminal confirmation is on."
                                 : L"Terminal confirmation is off.");
}

bool IsStartWithWindowsEnabled() {
    return platform::IsStartWithWindowsEnabled();
}

core::Status SetStartWithWindows(bool enabled) {
    std::wstring error;
    if (!platform::SetStartWithWindows(enabled, &error))
        return core::Error(core::ErrorCode::RegistryWriteFailed,
                           L"Cannot change the startup entry: " + error);
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
