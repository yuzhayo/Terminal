#include "platform/startup.h"

#include <windows.h>

#include "platform/paths.h"
#include "platform/process.h"
#include "platform/strings.h"

namespace platform {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"Terminal";

std::wstring TrayCommand() { return str::QuoteArg(paths::ExecutablePath()) + L" --tray"; }

}  // namespace

bool IsStartWithWindowsEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0;
    DWORD size = 0;
    const bool present =
        RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(key);
    return present;
}

bool SetStartWithWindows(bool enabled, std::wstring* error) {
    HKEY key = nullptr;
    const LSTATUS opened =
        RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS) {
        if (error) *error = process::ErrorMessage(static_cast<unsigned long>(opened));
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring command = TrayCommand();
        status = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);

    if (status != ERROR_SUCCESS) {
        if (error) *error = process::ErrorMessage(static_cast<unsigned long>(status));
        return false;
    }
    return true;
}

}  // namespace platform
