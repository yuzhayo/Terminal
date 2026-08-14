// Persisted app state. Loaded once at startup; saved when something changes.
// Nothing is written until the first save, so a fresh install leaves no folder.
#pragma once
#include <string>
#include <vector>

namespace storage {

// One API key belonging to a base URL. Stored and displayed in the clear: this
// is a single-user local launcher, so masking only made choosing a key harder.
struct ApiKey {
    std::wstring id;
    std::wstring label;
    std::wstring key;
};

struct BaseUrl {
    std::wstring id;
    std::wstring label;
    std::wstring url;
    std::wstring model;  // manual model id, bound to the base URL
    std::vector<ApiKey> keys;
    std::wstring selected_key_id;
};

// Extension point: a second application (OpenCode, Continue, Cline, ...) would
// add its own provider list here plus a target enum in json_inject.
struct Providers {
    std::vector<BaseUrl> base_urls;
    std::wstring selected_base_url_id;
};

struct Bookmark {
    std::wstring id;
    std::wstring label;
    std::wstring url;
};

// A Chrome profile the user chose to show, in display order.
struct VisibleProfile {
    std::wstring runtime;  // "windows" or "wsl"
    std::wstring browser;  // "chrome" or "chromium"
    std::wstring directory;
    std::wstring name;
};

// Visible profiles and the saved preset, both per runtime. The selection is user
// intent and must outlive any rebuild of the discovered-profile cache, which is
// why it lives here and not in chrome_profiles.json.
struct ChromeRuntimeState {
    std::vector<VisibleProfile> visible;
    std::vector<VisibleProfile> preset;
};

struct Settings {
    // Theme is stored as the persisted token ("dark" / "light") rather than a UI
    // enum: core has no opinion on rendering, so the frontend maps the token to
    // whatever palette it uses.
    std::wstring theme = L"dark";

    std::wstring terminal_folder;
    std::vector<std::wstring> recent_folders;
    bool venv_powershell_admin = false;
    bool venv_powershell = false;
    bool venv_wsl = false;

    std::wstring json_target;  // "windows" or "wsl"

    std::vector<Bookmark> bookmarks;
    std::wstring selected_bookmark_id;
    std::vector<std::wstring> url_history;
    // Bookmarks and URL history stay shared across runtimes; only the profile
    // selection is per runtime.
    std::wstring chrome_runtime;  // "windows" or "wsl"
    ChromeRuntimeState chrome_windows;
    ChromeRuntimeState chrome_wsl;

    bool start_with_windows = false;
};

// The state for whichever runtime name is passed; unknown names read as Windows.
ChromeRuntimeState& ChromeStateFor(const std::wstring& runtime);

// Process-wide state. Load() is called once at startup.
Settings& CurrentSettings();
Providers& CurrentProviders();

// Reads both files. Missing or corrupt files leave the defaults in place; core
// never touches the UI, so applying the theme is the caller's job.
void Load();
bool SaveSettings();
bool SaveProviders();

// Helpers that keep the ordered, de-duplicated lists consistent.
void RememberFolder(std::wstring folder);
void RememberUrl(std::wstring url);
std::wstring NewId();

BaseUrl* FindBaseUrl(const std::wstring& id);
ApiKey* FindApiKey(BaseUrl* base, const std::wstring& id);

}  // namespace storage
