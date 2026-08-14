#pragma once

#include <string>

namespace platform {

struct AppPaths {
    std::wstring data_root;
    std::wstring ui_override;
    std::wstring ui_config_log;
    std::wstring updater_state;
};

bool ResolveAppPaths(AppPaths& paths, std::wstring& diagnostic);

}  // namespace platform
