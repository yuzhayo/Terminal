#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace platform {

const wchar_t* MainWindowClassName();

enum class InstanceClaim {
    Primary,
    SecondaryNotified,
    Error,
};

class SingleInstance final {
public:
    SingleInstance() = default;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    InstanceClaim Claim(const std::wstring& command_line);
    void Release();

private:
    HANDLE mutex_ = nullptr;
};

std::optional<std::wstring> ReadForwardedCommand(LPARAM lparam);
void ActivateMainWindow(HWND window);

}  // namespace platform
