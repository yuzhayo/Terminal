#include "platform/windows_runtime.h"

#include <windows.h>

namespace platform {

WindowsRuntimeStatus CheckWindowsRuntime(std::wstring& diagnostic) {
    static_assert(sizeof(void*) == 8, "Terminal supports x64 only.");

    OSVERSIONINFOEXW requirement{};
    requirement.dwOSVersionInfoSize = sizeof(requirement);
    requirement.dwMajorVersion = 10;
    requirement.dwMinorVersion = 0;
    requirement.dwBuildNumber = 19045;

    ULONGLONG condition_mask = 0;
    condition_mask = VerSetConditionMask(condition_mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    condition_mask = VerSetConditionMask(condition_mask, VER_MINORVERSION, VER_GREATER_EQUAL);
    condition_mask = VerSetConditionMask(condition_mask, VER_BUILDNUMBER, VER_GREATER_EQUAL);

    constexpr DWORD kVersionFields = VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER;
    if (VerifyVersionInfoW(&requirement, kVersionFields, condition_mask)) {
        diagnostic.clear();
        return WindowsRuntimeStatus::Supported;
    }

    if (GetLastError() == ERROR_OLD_WIN_VERSION) {
        diagnostic = L"Terminal membutuhkan Windows 10 22H2 build 19045 atau yang lebih baru.";
        return WindowsRuntimeStatus::Unsupported;
    }

    diagnostic = L"Versi Windows tidak dapat diverifikasi.";
    return WindowsRuntimeStatus::CheckFailed;
}

}  // namespace platform
