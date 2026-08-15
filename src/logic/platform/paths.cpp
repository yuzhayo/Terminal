#include "platform/paths.h"

#include <windows.h>
#include <shlobj.h>

#include "platform/strings.h"

namespace paths {
namespace {

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        result.assign(raw);
    }
    if (raw) CoTaskMemFree(raw);
    return result;
}

}  // namespace

std::wstring ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return {};
        if (written < buffer.size()) {
            buffer.resize(written);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring ExecutableDir() { return Parent(ExecutablePath()); }

std::wstring LocalAppDataDir() { return KnownFolder(FOLDERID_LocalAppData); }
std::wstring UserProfileDir() { return KnownFolder(FOLDERID_Profile); }

std::wstring AppDataDir() { return Join(Join(LocalAppDataDir(), L"Yuzha"), L"Terminal"); }
std::wstring SettingsFile() { return Join(AppDataDir(), L"settings.json"); }
std::wstring ProvidersFile() { return Join(AppDataDir(), L"providers.json"); }
std::wstring ChromeProfilesFile() { return Join(AppDataDir(), L"chrome_profiles.json"); }
std::wstring UiConfigFile() { return Join(AppDataDir(), L"ui.json"); }

std::wstring Join(std::wstring_view a, std::wstring_view b) {
    if (a.empty()) return std::wstring(b);
    if (b.empty()) return std::wstring(a);
    std::wstring out(a);
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    size_t skip = 0;
    while (skip < b.size() && (b[skip] == L'\\' || b[skip] == L'/')) ++skip;
    out.append(b.substr(skip));
    return out;
}

std::wstring Parent(std::wstring_view path) {
    const size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring_view::npos) return {};
    if (cut == 0) return L"\\";
    return std::wstring(path.substr(0, cut));
}

std::wstring FileName(std::wstring_view path) {
    const size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring_view::npos) return std::wstring(path);
    return std::wstring(path.substr(cut + 1));
}

bool FileExists(std::wstring_view path) {
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesW(std::wstring(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(std::wstring_view path) {
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesW(std::wstring(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectory(std::wstring_view path) {
    if (path.empty()) return false;
    if (DirectoryExists(path)) return true;
    const std::wstring parent = Parent(path);
    if (!parent.empty() && parent != path && !DirectoryExists(parent)) {
        if (!EnsureDirectory(parent)) return false;
    }
    if (CreateDirectoryW(std::wstring(path).c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring ExpandEnvironment(std::wstring_view value) {
    const std::wstring input(value);
    if (input.find(L'%') == std::wstring::npos) return input;
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD needed =
            ExpandEnvironmentStringsW(input.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
        if (needed == 0) return input;
        if (needed <= buffer.size()) {
            buffer.resize(needed > 0 ? needed - 1 : 0);
            return buffer;
        }
        buffer.resize(needed);
    }
}

std::wstring Normalize(std::wstring_view path) {
    if (path.empty()) return {};
    std::wstring input = ExpandEnvironment(str::Trim(path));
    if (input.empty()) return {};
    for (wchar_t& c : input) {
        if (c == L'/') c = L'\\';
    }
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD needed =
            GetFullPathNameW(input.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (needed == 0) break;
        if (needed < buffer.size()) {
            buffer.resize(needed);
            input = buffer;
            break;
        }
        buffer.resize(needed);
    }
    while (input.size() > 3 && (input.back() == L'\\' || input.back() == L'/')) input.pop_back();
    return input;
}

}  // namespace paths
