#include "platform/single_instance.h"

#include <cstdint>
#include <cwchar>

namespace platform {
namespace {

constexpr wchar_t kWindowClass[] = L"OpenTerminalNative.MainWindow";
constexpr wchar_t kMutexName[] = L"Local\\Yuzha.OpenTerminalNative.SingleInstance";
constexpr ULONG_PTR kCopyDataId = 0x4F544E31;
constexpr DWORD kFindWindowAttempts = 40;
constexpr DWORD kFindWindowDelayMs = 50;
constexpr UINT kSendTimeoutMs = 1000;
constexpr std::size_t kMaximumCommandCharacters = 32768;

bool ForwardCommand(const std::wstring& command_line) {
    if (command_line.size() >= kMaximumCommandCharacters) {
        return false;
    }

    COPYDATASTRUCT payload{};
    payload.dwData = kCopyDataId;
    payload.cbData = static_cast<DWORD>((command_line.size() + 1) * sizeof(wchar_t));
    payload.lpData = const_cast<wchar_t*>(command_line.c_str());

    for (DWORD attempt = 0; attempt < kFindWindowAttempts; ++attempt) {
        HWND window = FindWindowW(kWindowClass, nullptr);
        if (window) {
            DWORD_PTR result = 0;
            return SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&payload),
                                       SMTO_ABORTIFHUNG | SMTO_BLOCK, kSendTimeoutMs, &result) != 0;
        }
        Sleep(kFindWindowDelayMs);
    }
    return false;
}

}  // namespace

const wchar_t* MainWindowClassName() {
    return kWindowClass;
}

SingleInstance::~SingleInstance() {
    Release();
}

InstanceClaim SingleInstance::Claim(const std::wstring& command_line) {
    mutex_ = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!mutex_) {
        return InstanceClaim::Error;
    }

    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        return InstanceClaim::Primary;
    }

    Release();
    return ForwardCommand(command_line) ? InstanceClaim::SecondaryNotified : InstanceClaim::Error;
}

void SingleInstance::Release() {
    if (mutex_) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

std::optional<std::wstring> ReadForwardedCommand(LPARAM lparam) {
    const auto* payload = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
    if (!payload || payload->dwData != kCopyDataId || !payload->lpData ||
        payload->cbData < sizeof(wchar_t) || payload->cbData % sizeof(wchar_t) != 0) {
        return std::nullopt;
    }

    const std::size_t count = payload->cbData / sizeof(wchar_t);
    if (count > kMaximumCommandCharacters) {
        return std::nullopt;
    }

    const auto* text = static_cast<const wchar_t*>(payload->lpData);
    if (text[count - 1] != L'\0' || std::wmemchr(text, L'\0', count - 1) != nullptr) {
        return std::nullopt;
    }
    return std::wstring(text, count - 1);
}

void ActivateMainWindow(HWND window) {
    if (IsIconic(window)) {
        ShowWindow(window, SW_RESTORE);
    } else {
        ShowWindow(window, SW_SHOW);
    }
    SetForegroundWindow(window);
}

}  // namespace platform
