#pragma once

#include <string>

namespace platform {

enum class WindowsRuntimeStatus {
    Supported,
    Unsupported,
    CheckFailed,
};

WindowsRuntimeStatus CheckWindowsRuntime(std::wstring& diagnostic);

}  // namespace platform
