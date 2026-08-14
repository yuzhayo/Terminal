#include "storage/settings.h"

#include <windows.h>
#include <objbase.h>

#include "platform/files.h"
#include "platform/paths.h"
#include "storage/json.h"

namespace storage {
namespace {

constexpr size_t kMaxRecentFolders = 12;
constexpr size_t kMaxUrlHistory = 10;

Settings g_settings;
Providers g_providers;

json::Value StringArray(const std::vector<std::wstring>& values) {
    json::Value array = json::Value::Array();
    for (const std::wstring& value : values) array.Append(json::Value::String(value));
    return array;
}

std::vector<std::wstring> ReadStringArray(const json::Value* array, size_t limit) {
    std::vector<std::wstring> out;
    if (!array) return out;
    for (const json::Value& item : array->items()) {
        if (!item.is_string() || item.AsString().empty()) continue;
        out.push_back(item.AsString());
        if (out.size() >= limit) break;
    }
    return out;
}

// `fallback_runtime` is what a per-runtime array implies; the legacy flat array
// passes nothing and relies on each entry's own field.
bool ReadVisibleProfile(const json::Value& item, VisibleProfile* out,
                        std::wstring_view fallback_runtime = {}) {
    if (!item.is_object()) return false;
    out->runtime = item.StringField(L"runtime", fallback_runtime.empty() ? L"windows" : fallback_runtime);
    if (out->runtime != L"wsl") out->runtime = L"windows";
    out->browser = item.StringField(L"browser", L"chrome");
    out->directory = item.StringField(L"directory");
    out->name = item.StringField(L"name");
    return !out->directory.empty();
}

void ReadProfileArray(const json::Value* array, std::wstring_view runtime,
                      std::vector<VisibleProfile>* out) {
    if (!array) return;
    for (const json::Value& item : array->items()) {
        VisibleProfile profile;
        if (!ReadVisibleProfile(item, &profile, runtime)) continue;
        out->push_back(std::move(profile));
    }
}

json::Value ProfileArray(const std::vector<VisibleProfile>& profiles) {
    json::Value array = json::Value::Array();
    for (const VisibleProfile& profile : profiles) {
        json::Value item = json::Value::Object();
        item.Set(L"runtime", json::Value::String(profile.runtime));
        item.Set(L"browser", json::Value::String(profile.browser));
        item.Set(L"directory", json::Value::String(profile.directory));
        item.Set(L"name", json::Value::String(profile.name));
        array.Append(std::move(item));
    }
    return array;
}

void LoadSettingsFile() {
    std::wstring text;
    if (!files::ReadText(paths::SettingsFile(), &text, nullptr)) return;
    json::Value root;
    if (!json::Parse(text, &root, nullptr) || !root.is_object()) return;

    g_settings.theme = root.StringField(L"theme", L"dark");
    if (g_settings.theme != L"light") g_settings.theme = L"dark";

    if (const json::Value* terminal = root.ObjectField(L"terminal")) {
        g_settings.terminal_folder = terminal->StringField(L"folder");
        g_settings.recent_folders = ReadStringArray(terminal->ArrayField(L"recentFolders"), kMaxRecentFolders);
        g_settings.venv_powershell_admin = terminal->BoolField(L"venvPowerShellAdmin");
        g_settings.venv_powershell = terminal->BoolField(L"venvPowerShell");
        g_settings.venv_wsl = terminal->BoolField(L"venvWsl");
    }

    if (const json::Value* inject = root.ObjectField(L"jsonInject")) {
        g_settings.json_target = inject->StringField(L"target", L"windows");
    }

    if (const json::Value* chrome = root.ObjectField(L"chrome")) {
        if (const json::Value* bookmarks = chrome->ArrayField(L"bookmarks")) {
            for (const json::Value& item : bookmarks->items()) {
                if (!item.is_object()) continue;
                Bookmark bookmark;
                bookmark.id = item.StringField(L"id");
                bookmark.label = item.StringField(L"label");
                bookmark.url = item.StringField(L"url");
                if (bookmark.id.empty()) bookmark.id = NewId();
                if (bookmark.url.empty()) continue;
                g_settings.bookmarks.push_back(std::move(bookmark));
            }
        }
        g_settings.selected_bookmark_id = chrome->StringField(L"selectedBookmarkId");
        g_settings.url_history = ReadStringArray(chrome->ArrayField(L"urlHistory"), kMaxUrlHistory);

        if (const json::Value* profiles = chrome->Find(L"visibleProfiles")) {
            // Legacy shape: a flat array from before the runtime split. Its
            // entries carried a "runtime" field, so they are routed by it and the
            // new shape is written from the next save on. Dropping them would
            // silently empty the user's card list.
            if (profiles->is_array()) {
                for (const json::Value& item : profiles->items()) {
                    VisibleProfile profile;
                    if (!ReadVisibleProfile(item, &profile)) continue;
                    ChromeStateFor(profile.runtime).visible.push_back(std::move(profile));
                }
            } else if (profiles->is_object()) {
                ReadProfileArray(profiles->ArrayField(L"windows"), L"windows",
                                 &g_settings.chrome_windows.visible);
                ReadProfileArray(profiles->ArrayField(L"wsl"), L"wsl", &g_settings.chrome_wsl.visible);
            }
        }
        if (const json::Value* presets = chrome->ObjectField(L"presets")) {
            ReadProfileArray(presets->ArrayField(L"windows"), L"windows", &g_settings.chrome_windows.preset);
            ReadProfileArray(presets->ArrayField(L"wsl"), L"wsl", &g_settings.chrome_wsl.preset);
        }
        g_settings.chrome_runtime = chrome->StringField(L"activeRuntime", L"windows");
        if (g_settings.chrome_runtime != L"wsl") g_settings.chrome_runtime = L"windows";
    }

    if (const json::Value* startup = root.ObjectField(L"startup")) {
        g_settings.start_with_windows = startup->BoolField(L"startWithWindows");
    }
}

void LoadProvidersFile() {
    std::wstring text;
    if (!files::ReadText(paths::ProvidersFile(), &text, nullptr)) return;
    json::Value root;
    if (!json::Parse(text, &root, nullptr) || !root.is_object()) return;

    // V1 stores a single application block. Additional applications would live
    // beside "claudeCode" using the same shape.
    const json::Value* app = root.ObjectField(L"claudeCode");
    if (!app) return;

    if (const json::Value* list = app->ArrayField(L"baseUrls")) {
        for (const json::Value& item : list->items()) {
            if (!item.is_object()) continue;
            BaseUrl base;
            base.id = item.StringField(L"id");
            base.label = item.StringField(L"label");
            base.url = item.StringField(L"url");
            base.model = item.StringField(L"model");
            base.selected_key_id = item.StringField(L"selectedKeyId");
            if (base.id.empty()) base.id = NewId();
            if (base.url.empty()) continue;

            if (const json::Value* keys = item.ArrayField(L"keys")) {
                for (const json::Value& key_item : keys->items()) {
                    if (!key_item.is_object()) continue;
                    ApiKey key;
                    key.id = key_item.StringField(L"id");
                    key.label = key_item.StringField(L"label");
                    key.key = key_item.StringField(L"key");
                    if (key.key.empty()) continue;
                    if (key.id.empty()) key.id = NewId();
                    base.keys.push_back(std::move(key));
                }
            }
            g_providers.base_urls.push_back(std::move(base));
        }
    }
    g_providers.selected_base_url_id = app->StringField(L"selectedBaseUrlId");
}

}  // namespace

Settings& CurrentSettings() { return g_settings; }
Providers& CurrentProviders() { return g_providers; }

ChromeRuntimeState& ChromeStateFor(const std::wstring& runtime) {
    return runtime == L"wsl" ? g_settings.chrome_wsl : g_settings.chrome_windows;
}

void Load() {
    LoadSettingsFile();
    LoadProvidersFile();
}

bool SaveSettings() {
    json::Value root = json::Value::Object();
    root.Set(L"version", json::Value::Number(1));
    root.Set(L"theme", json::Value::String(ui::ThemeName(g_settings.theme)));

    json::Value terminal = json::Value::Object();
    terminal.Set(L"folder", json::Value::String(g_settings.terminal_folder));
    terminal.Set(L"recentFolders", StringArray(g_settings.recent_folders));
    terminal.Set(L"venvPowerShellAdmin", json::Value::Bool(g_settings.venv_powershell_admin));
    terminal.Set(L"venvPowerShell", json::Value::Bool(g_settings.venv_powershell));
    terminal.Set(L"venvWsl", json::Value::Bool(g_settings.venv_wsl));
    root.Set(L"terminal", std::move(terminal));

    json::Value inject = json::Value::Object();
    inject.Set(L"target", json::Value::String(g_settings.json_target.empty() ? L"windows" : g_settings.json_target));
    root.Set(L"jsonInject", std::move(inject));

    json::Value chrome = json::Value::Object();
    json::Value bookmarks = json::Value::Array();
    for (const Bookmark& bookmark : g_settings.bookmarks) {
        json::Value item = json::Value::Object();
        item.Set(L"id", json::Value::String(bookmark.id));
        item.Set(L"label", json::Value::String(bookmark.label));
        item.Set(L"url", json::Value::String(bookmark.url));
        bookmarks.Append(std::move(item));
    }
    chrome.Set(L"bookmarks", std::move(bookmarks));
    chrome.Set(L"selectedBookmarkId", json::Value::String(g_settings.selected_bookmark_id));
    chrome.Set(L"urlHistory", StringArray(g_settings.url_history));
    chrome.Set(L"activeRuntime",
               json::Value::String(g_settings.chrome_runtime.empty() ? L"windows" : g_settings.chrome_runtime));

    json::Value visible = json::Value::Object();
    visible.Set(L"windows", ProfileArray(g_settings.chrome_windows.visible));
    visible.Set(L"wsl", ProfileArray(g_settings.chrome_wsl.visible));
    chrome.Set(L"visibleProfiles", std::move(visible));

    json::Value presets = json::Value::Object();
    presets.Set(L"windows", ProfileArray(g_settings.chrome_windows.preset));
    presets.Set(L"wsl", ProfileArray(g_settings.chrome_wsl.preset));
    chrome.Set(L"presets", std::move(presets));
    root.Set(L"chrome", std::move(chrome));

    json::Value startup = json::Value::Object();
    startup.Set(L"startWithWindows", json::Value::Bool(g_settings.start_with_windows));
    root.Set(L"startup", std::move(startup));

    return files::WriteTextAtomic(paths::SettingsFile(), json::Serialize(root), nullptr);
}

bool SaveProviders() {
    json::Value app = json::Value::Object();
    json::Value list = json::Value::Array();
    for (const BaseUrl& base : g_providers.base_urls) {
        json::Value item = json::Value::Object();
        item.Set(L"id", json::Value::String(base.id));
        item.Set(L"label", json::Value::String(base.label));
        item.Set(L"url", json::Value::String(base.url));
        item.Set(L"model", json::Value::String(base.model));
        item.Set(L"selectedKeyId", json::Value::String(base.selected_key_id));

        json::Value keys = json::Value::Array();
        for (const ApiKey& key : base.keys) {
            json::Value key_item = json::Value::Object();
            key_item.Set(L"id", json::Value::String(key.id));
            key_item.Set(L"label", json::Value::String(key.label));
            key_item.Set(L"key", json::Value::String(key.key));
            keys.Append(std::move(key_item));
        }
        item.Set(L"keys", std::move(keys));
        list.Append(std::move(item));
    }
    app.Set(L"baseUrls", std::move(list));
    app.Set(L"selectedBaseUrlId", json::Value::String(g_providers.selected_base_url_id));

    json::Value root = json::Value::Object();
    root.Set(L"version", json::Value::Number(1));
    root.Set(L"claudeCode", std::move(app));

    return files::WriteTextAtomic(paths::ProvidersFile(), json::Serialize(root), nullptr);
}

void RememberFolder(std::wstring folder) {
    if (folder.empty()) return;
    g_settings.terminal_folder = folder;
    for (size_t i = 0; i < g_settings.recent_folders.size(); ++i) {
        if (CompareStringOrdinal(g_settings.recent_folders[i].c_str(), -1, folder.c_str(), -1, TRUE) == CSTR_EQUAL) {
            g_settings.recent_folders.erase(g_settings.recent_folders.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    g_settings.recent_folders.insert(g_settings.recent_folders.begin(), std::move(folder));
    if (g_settings.recent_folders.size() > kMaxRecentFolders) g_settings.recent_folders.resize(kMaxRecentFolders);
}

void RememberUrl(std::wstring url) {
    if (url.empty()) return;
    for (size_t i = 0; i < g_settings.url_history.size(); ++i) {
        if (g_settings.url_history[i] == url) {
            g_settings.url_history.erase(g_settings.url_history.begin() + static_cast<ptrdiff_t>(i));
            break;
        }
    }
    g_settings.url_history.insert(g_settings.url_history.begin(), std::move(url));
    if (g_settings.url_history.size() > kMaxUrlHistory) g_settings.url_history.resize(kMaxUrlHistory);
}

std::wstring NewId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        static unsigned counter = 0;
        return L"id-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++counter);
    }
    wchar_t buffer[40]{};
    StringFromGUID2(guid, buffer, 40);
    std::wstring id(buffer);
    if (!id.empty() && id.front() == L'{') id = id.substr(1, id.size() - 2);
    return id;
}

BaseUrl* FindBaseUrl(const std::wstring& id) {
    if (id.empty()) return nullptr;
    for (BaseUrl& base : g_providers.base_urls) {
        if (base.id == id) return &base;
    }
    return nullptr;
}

ApiKey* FindApiKey(BaseUrl* base, const std::wstring& id) {
    if (!base || id.empty()) return nullptr;
    for (ApiKey& key : base->keys) {
        if (key.id == id) return &key;
    }
    return nullptr;
}

}  // namespace storage
