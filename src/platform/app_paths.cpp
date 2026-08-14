#include "platform/app_paths.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace platform {

bool ResolveAppPaths(AppPaths& paths, std::wstring& diagnostic) {
    PWSTR local_app_data = nullptr;
    const HRESULT result =
        SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &local_app_data);
    if (FAILED(result) || !local_app_data || local_app_data[0] == L'\0') {
        if (local_app_data) {
            CoTaskMemFree(local_app_data);
        }
        diagnostic = L"Folder LocalAppData Windows tidak dapat ditemukan.";
        return false;
    }

    paths.data_root = std::wstring(local_app_data) + L"\\Yuzha\\Terminal";
    CoTaskMemFree(local_app_data);

    paths.ui_override = paths.data_root + L"\\ui\\override.v1.json";
    paths.ui_config_log = paths.data_root + L"\\logs\\ui-config.log";
    paths.updater_state = paths.data_root + L"\\updater\\state.json";
    diagnostic.clear();
    return true;
}

}  // namespace platform
