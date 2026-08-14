// Filesystem paths and app storage locations.
#pragma once
#include <string>
#include <string_view>

namespace paths {

// Full path of the running executable.
std::wstring ExecutablePath();
std::wstring ExecutableDir();

// %LOCALAPPDATA%\OpenTerminalNative — not created until data must be saved.
std::wstring AppDataDir();
std::wstring SettingsFile();
std::wstring ProvidersFile();
// Discovered Chrome profiles. App-owned derived cache: safe to delete, rebuilt
// by Refresh, written with an atomic replace.
std::wstring ChromeProfilesFile();
// Optional user UI override. Never created unless the user asks for it.
std::wstring UiConfigFile();

std::wstring UserProfileDir();
std::wstring LocalAppDataDir();

std::wstring Join(std::wstring_view a, std::wstring_view b);
std::wstring Parent(std::wstring_view path);
std::wstring FileName(std::wstring_view path);

bool FileExists(std::wstring_view path);
bool DirectoryExists(std::wstring_view path);

// Creates every missing component of `path`. Returns true when it exists after.
bool EnsureDirectory(std::wstring_view path);

// Expands %VAR% references. Returns the input when expansion fails.
std::wstring ExpandEnvironment(std::wstring_view value);

// Normalizes to a full path with backslashes and no trailing separator.
std::wstring Normalize(std::wstring_view path);

}  // namespace paths
