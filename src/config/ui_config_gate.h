#pragma once

#include <windows.h>

#include <string>

#include "platform/app_paths.h"

namespace config {

struct UiConfigMetadata {
    std::string schema;
    int version = 0;
    int minimum_reader_contract = 0;
    std::string written_by_app_version;
    int written_by_config_contract = 0;
};

class UiConfigGate final {
public:
    UiConfigGate(HINSTANCE instance, platform::AppPaths paths);

    bool ResolveBootstrap(std::wstring& diagnostic);

    const UiConfigMetadata& metadata() const noexcept;
    const platform::AppPaths& paths() const noexcept;

private:
    HINSTANCE instance_ = nullptr;
    platform::AppPaths paths_;
    UiConfigMetadata metadata_;
};

}  // namespace config
