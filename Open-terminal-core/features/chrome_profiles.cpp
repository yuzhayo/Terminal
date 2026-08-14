#include "features/chrome_profiles.h"

#include <windows.h>

#include <algorithm>
#include <utility>

#include "platform/files.h"
#include "platform/foreground.h"
#include "platform/paths.h"
#include "platform/process.h"
#include "platform/strings.h"
#include "platform/wsl.h"
#include "storage/json.h"
#include "storage/settings.h"

namespace features {
namespace {

// ---- browser tables ----

struct WindowsBrowser {
    const wchar_t* id;
    const wchar_t* user_data_relative;
    const wchar_t* exe_relative;
};

constexpr WindowsBrowser kWindowsBrowsers[] = {
    {L"chrome", L"Google\\Chrome\\User Data", L"Google\\Chrome\\Application\\chrome.exe"},
};

struct WslBrowser {
    const wchar_t* id;
    const wchar_t* config_relative;
    const wchar_t* command;
};

constexpr WslBrowser kWslBrowsers[] = {
    {L"chrome",    L".config/google-chrome", L"google-chrome"},
    {L"chromium",  L".config/chromium",      L"chromium"},
};

// ---- profile cache (process-wide) ----

struct CachedRuntime {
    bool scanned = false;
    std::wstring scanned_at_utc;
    std::wstring distro;
    std::vector<ChromeProfile> profiles;
};

struct ProfileCache {
    CachedRuntime windows;
    CachedRuntime wsl;
    CachedRuntime& For(ChromeRuntime r) { return r == ChromeRuntime::Wsl ? wsl : windows; }
    const CachedRuntime& For(ChromeRuntime r) const { return r == ChromeRuntime::Wsl ? wsl : windows; }
};

ProfileCache g_cache;

// ---- helpers ----

std::wstring WindowsExe(const WindowsBrowser& browser) {
    const std::wstring roots[] = {
        paths::ExpandEnvironment(L"%ProgramFiles%"),
        paths::ExpandEnvironment(L"%ProgramFiles(x86)%"),
        paths::Join(paths::LocalAppDataDir(), L""),
    };
    for (const std::wstring& root : roots) {
        const std::wstring candidate = paths::Join(root, browser.exe_relative);
        if (paths::FileExists(candidate)) return candidate;
    }
    return {};
}

void ReadProfileIndex(const std::wstring& user_data_dir, ChromeRuntime runtime,
                      const std::wstring& browser_id, std::vector<ChromeProfile>* out) {
    const std::wstring local_state = paths::Join(user_data_dir, L"Local State");
    std::wstring text;
    if (!files::ReadText(local_state, &text, nullptr)) return;
    json::Value root;
    if (!json::Parse(text, &root, nullptr) || !root.is_object()) return;
    const json::Value* profile = root.ObjectField(L"profile");
    if (!profile) return;
    const json::Value* cache = profile->ObjectField(L"info_cache");
    if (!cache) return;
    for (const auto& member : cache->members()) {
        if (!member.second.is_object()) continue;
        ChromeProfile entry;
        entry.runtime   = runtime;
        entry.browser   = browser_id;
        entry.directory = member.first;
        entry.name      = member.second.StringField(L"name");
        if (entry.name.empty()) entry.name = member.first;
        entry.available = true;
        out->push_back(std::move(entry));
    }
    std::sort(out->begin(), out->end(), [](const ChromeProfile& a, const ChromeProfile& b) {
        if (a.runtime  != b.runtime)  return a.runtime < b.runtime;
        if (a.browser  != b.browser)  return a.browser < b.browser;
        return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
}

std::wstring UtcNow() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    wchar_t buf[32]{};
    wsprintfW(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

void ReadCachedRuntime(const json::Value* block, ChromeRuntime runtime, CachedRuntime* out) {
    if (!block || !block->is_object()) return;
    out->scanned        = true;
    out->scanned_at_utc = block->StringField(L"scannedAtUtc");
    out->distro         = block->StringField(L"distro");
    if (const json::Value* list = block->ArrayField(L"profiles")) {
        for (const json::Value& item : list->items()) {
            if (!item.is_object()) continue;
            ChromeProfile p;
            p.runtime   = runtime;
            p.browser   = item.StringField(L"browser", L"chrome");
            p.directory = item.StringField(L"directory");
            p.name      = item.StringField(L"name");
            p.available = item.BoolField(L"available", true);
            if (p.directory.empty()) continue;
            out->profiles.push_back(std::move(p));
        }
    }
}

json::Value WriteCachedRuntime(const CachedRuntime& section, ChromeRuntime runtime) {
    json::Value block = json::Value::Object();
    block.Set(L"scannedAtUtc", json::Value::String(section.scanned_at_utc));
    if (runtime == ChromeRuntime::Wsl)
        block.Set(L"distro", json::Value::String(section.distro));
    json::Value list = json::Value::Array();
    for (const ChromeProfile& p : section.profiles) {
        json::Value item = json::Value::Object();
        item.Set(L"runtime",   json::Value::String(runtime == ChromeRuntime::Wsl ? L"wsl" : L"windows"));
        item.Set(L"browser",   json::Value::String(p.browser));
        item.Set(L"directory", json::Value::String(p.directory));
        item.Set(L"name",      json::Value::String(p.name));
        item.Set(L"available", json::Value::Bool(p.available));
        list.Append(std::move(item));
    }
    block.Set(L"profiles", std::move(list));
    return block;
}

bool StoreCachedRuntime(ChromeRuntime runtime, std::vector<ChromeProfile> profiles,
                        const std::wstring& distro, std::wstring* error) {
    CachedRuntime& section = g_cache.For(runtime);
    section.scanned        = true;
    section.scanned_at_utc = UtcNow();
    section.distro         = distro;
    section.profiles       = std::move(profiles);

    json::Value root = json::Value::Object();
    root.Set(L"version", json::Value::Number(1));
    root.Set(L"windows", WriteCachedRuntime(g_cache.windows, ChromeRuntime::Windows));
    root.Set(L"wsl",     WriteCachedRuntime(g_cache.wsl,     ChromeRuntime::Wsl));
    return files::WriteTextAtomic(paths::ChromeProfilesFile(), json::Serialize(root), error);
}

const ChromeProfile* FindInCache(const std::vector<ChromeProfile>& cached,
                                 const storage::VisibleProfile& saved) {
    for (const ChromeProfile& p : cached)
        if (p.browser == saved.browser && p.directory == saved.directory) return &p;
    return nullptr;
}

std::wstring NormalizeUrl(const std::wstring& input) {
    const std::wstring trimmed = str::Trim(input);
    if (trimmed.empty()) return {};
    if (str::StartsWith(trimmed, L"http://") || str::StartsWith(trimmed, L"https://"))
        return trimmed;
    // Reject strings without a dot — garbage like "foobar" is not a URL.
    if (trimmed.find(L'.') == std::wstring::npos &&
        trimmed.find(L'/') == std::wstring::npos) return {};
    return L"https://" + trimmed;
}

std::vector<storage::VisibleProfile>& VisibleList() {
    return storage::ChromeStateFor(storage::CurrentSettings().chrome_runtime).visible;
}

}  // namespace

// ---- public API ----

std::wstring ChromeRuntimeName(ChromeRuntime r) {
    return r == ChromeRuntime::Wsl ? L"wsl" : L"windows";
}
ChromeRuntime ChromeRuntimeFromName(const std::wstring& name) {
    return name == L"wsl" ? ChromeRuntime::Wsl : ChromeRuntime::Windows;
}

std::wstring ChromeProfile::Key() const {
    return ChromeRuntimeName(runtime) + L"|" + browser + L"|" + directory;
}
std::wstring ChromeProfile::RuntimeLabel() const {
    if (runtime == ChromeRuntime::Wsl)
        return browser == L"chromium" ? L"Ubuntu WSL · Chromium" : L"Ubuntu WSL · Chrome";
    return L"Windows · Chrome";
}

ChromeScanResult ScanProfiles(ChromeRuntime runtime) {
    ChromeScanResult out;
    out.runtime = runtime;
    if (runtime == ChromeRuntime::Windows) {
        for (const WindowsBrowser& browser : kWindowsBrowsers) {
            const std::wstring user_data =
                paths::Join(paths::LocalAppDataDir(), browser.user_data_relative);
            if (!paths::DirectoryExists(user_data)) continue;
            ReadProfileIndex(user_data, ChromeRuntime::Windows,
                             browser.id, &out.profiles);
        }
        out.ok = true;
        return out;
    }
    // WSL — wsl::Resolve blocks up to 8s on a cold cache.
    std::wstring error;
    if (!wsl::Resolve(&out.distro, nullptr, &error)) {
        out.error = error.empty() ? L"Ubuntu (WSL) is not available." : error;
        return out;
    }
    for (const WslBrowser& browser : kWslBrowsers) {
        std::wstring config_unc;
        std::wstring unc_error;
        if (!wsl::HomeFile(browser.config_relative, &config_unc, &unc_error)) continue;
        const std::wstring user_data = paths::Parent(config_unc);
        if (!paths::DirectoryExists(user_data)) continue;
        ReadProfileIndex(user_data, ChromeRuntime::Wsl,
                         browser.id, &out.profiles);
    }
    out.ok = true;
    return out;
}

core::Status ApplyScan(const ChromeScanResult& result) {
    if (!result.ok) return core::Error(result.error);

    const size_t count = result.profiles.size();
    std::wstring write_error;
    if (!StoreCachedRuntime(result.runtime,
                            std::vector<ChromeProfile>(result.profiles),
                            result.distro, &write_error)) {
        return core::Error(L"Scanned " + std::to_wstring(count) +
                           L" profile(s) but could not save the cache: " + write_error);
    }

    // Reconcile saved display names with the fresh scan.
    const std::vector<ChromeProfile>& cached = g_cache.For(result.runtime).profiles;
    for (storage::VisibleProfile& saved :
             storage::ChromeStateFor(ChromeRuntimeName(result.runtime)).visible) {
        if (const ChromeProfile* found = FindInCache(cached, saved))
            saved.name = found->name;
    }
    storage::SaveSettings();

    if (count == 0) {
        return core::Info(result.runtime == ChromeRuntime::Wsl
            ? L"No Chrome profile found in Ubuntu (WSL)."
            : L"No Chrome profile found. Install Chrome, then press Refresh.");
    }
    return core::Success(L"Found " + std::to_wstring(count) + L" profile(s).");
}

std::wstring ResolveUrl(const std::wstring& typed_url) {
    const std::wstring trimmed = str::Trim(typed_url);
    if (!trimmed.empty()) return NormalizeUrl(trimmed);
    const storage::Settings& s = storage::CurrentSettings();
    if (s.selected_bookmark_id.empty()) return {};
    for (const storage::Bookmark& bm : s.bookmarks)
        if (bm.id == s.selected_bookmark_id) return NormalizeUrl(bm.url);
    return {};
}

CardLaunchResult LaunchCard(size_t index, const std::wstring& typed_url) {
    CardLaunchResult out;
    const std::vector<storage::VisibleProfile>& visible = VisibleList();
    if (index >= visible.size()) {
        out.status = core::Error(L"No profile at that index.");
        return out;
    }
    const storage::VisibleProfile& saved = visible[index];
    const ChromeRuntime runtime = ActiveRuntime();
    const ChromeProfile* found = FindInCache(g_cache.For(runtime).profiles, saved);
    if (!found) {
        const std::wstring name = saved.name.empty() ? saved.directory : saved.name;
        out.status = core::Error(
            name + L" is unavailable. Press Refresh, or remove it from Manage Profiles.");
        return out;
    }

    const std::wstring trimmed_raw = str::Trim(typed_url);
    const bool from_input = !trimmed_raw.empty();
    const std::wstring url = ResolveUrl(typed_url);
    if (from_input && url.empty()) {
        out.status = core::Error(L"That URL could not be understood: " + trimmed_raw);
        return out;
    }

    // Launch via the appropriate platform helper.
    std::wstring launch_error;
    bool launch_ok = false;
    if (found->runtime == ChromeRuntime::Windows) {
        const WindowsBrowser* browser = nullptr;
        for (const WindowsBrowser& b : kWindowsBrowsers)
            if (found->browser == b.id) { browser = &b; break; }
        if (!browser) {
            out.status = core::Error(L"Unsupported browser: " + found->browser);
            return out;
        }
        const std::wstring exe = WindowsExe(*browser);
        if (exe.empty()) {
            out.status = core::Error(L"Google Chrome is not installed for this user.");
            return out;
        }
        std::wstring command = str::QuoteArg(exe) +
                               L" --profile-directory=" + str::QuoteArg(found->directory);
        if (!url.empty()) command += L" " + str::QuoteArg(url);
        launch_ok = process::Launch(exe, command, paths::Parent(exe),
                                    process::Window::Hidden, &launch_error);
        if (launch_ok && url.empty() && platform::ChromeWindowExists())
            platform::FocusChromeWindow();
    } else {
        const WslBrowser* browser = nullptr;
        for (const WslBrowser& b : kWslBrowsers)
            if (found->browser == b.id) { browser = &b; break; }
        if (!browser) {
            out.status = core::Error(L"Unsupported browser: " + found->browser);
            return out;
        }
        std::wstring distro;
        if (!wsl::Resolve(&distro, nullptr, &launch_error)) {
            out.status = core::Error(launch_error);
            return out;
        }
        std::wstring inner = std::wstring(browser->command) +
                             L" --profile-directory='" +
                             str::EscapePosixSingleQuoted(found->directory) + L"'";
        if (!url.empty())
            inner += L" '" + str::EscapePosixSingleQuoted(url) + L"'";
        inner += L" >/dev/null 2>&1 &";
        const std::wstring command = str::QuoteArg(wsl::Exe()) +
                                     L" -d " + str::QuoteArg(distro) +
                                     L" -- bash -lc " + str::QuoteArg(inner);
        launch_ok = process::Launch({}, command, {}, process::Window::Hidden, &launch_error);
    }

    if (!launch_ok) { out.status = core::Error(launch_error); return out; }

    if (!url.empty()) storage::RememberUrl(url);
    storage::SaveSettings();

    out.url_opened   = url;
    out.clear_input  = from_input;
    std::wstring msg = L"Opened " + found->name;
    if (!url.empty()) msg += L" at " + url;
    out.status = core::Success(msg);
    return out;
}

ChromeRuntime ActiveRuntime() {
    return storage::CurrentSettings().chrome_runtime == L"wsl"
        ? ChromeRuntime::Wsl : ChromeRuntime::Windows;
}

core::Status SwitchRuntime(ChromeRuntime runtime) {
    if (runtime == ActiveRuntime()) return core::NoStatus();
    storage::CurrentSettings().chrome_runtime = ChromeRuntimeName(runtime);
    storage::SaveSettings();
    return core::NoStatus();
}

core::Status AddBookmark(const std::wstring& label, const std::wstring& url) {
    const std::wstring normalized = NormalizeUrl(url);
    if (normalized.empty())
        return core::Error(L"That URL could not be understood: " + str::Trim(url));
    storage::Bookmark bm;
    bm.id    = storage::NewId();
    bm.label = str::Trim(label);
    bm.url   = normalized;
    storage::CurrentSettings().bookmarks.push_back(std::move(bm));
    storage::SaveSettings();
    return core::Success(L"Bookmark added.");
}

core::Status RemoveBookmark() {
    storage::Settings& s = storage::CurrentSettings();
    if (s.selected_bookmark_id.empty())
        return core::Error(L"Select a bookmark first.");
    for (size_t i = 0; i < s.bookmarks.size(); ++i) {
        if (s.bookmarks[i].id != s.selected_bookmark_id) continue;
        const std::wstring label =
            s.bookmarks[i].label.empty() ? s.bookmarks[i].url : s.bookmarks[i].label;
        s.bookmarks.erase(s.bookmarks.begin() + static_cast<ptrdiff_t>(i));
        s.selected_bookmark_id.clear();
        storage::SaveSettings();
        return core::Success(L"Removed " + label + L".");
    }
    return core::Error(L"Bookmark not found.");
}

core::Status SelectBookmark(size_t index) {
    storage::Settings& s = storage::CurrentSettings();
    if (index >= s.bookmarks.size()) return core::Error(L"Index out of range.");
    const std::wstring& id = s.bookmarks[index].id;
    s.selected_bookmark_id = (s.selected_bookmark_id == id) ? std::wstring() : id;
    storage::SaveSettings();
    return core::NoStatus();
}

core::Status SavePreset() {
    storage::ChromeRuntimeState& state =
        storage::ChromeStateFor(storage::CurrentSettings().chrome_runtime);
    state.preset = state.visible;
    storage::SaveSettings();
    return state.preset.empty()
        ? core::Success(L"Preset saved as empty for this runtime.")
        : core::Success(L"Preset saved: " + std::to_wstring(state.preset.size()) + L" profile(s).");
}

core::Status LoadPreset() {
    storage::ChromeRuntimeState& state =
        storage::ChromeStateFor(storage::CurrentSettings().chrome_runtime);
    if (state.preset.empty())
        return core::Error(L"No preset saved for this runtime yet.");
    state.visible = state.preset;
    storage::SaveSettings();
    return core::Success(L"Preset loaded: " + std::to_wstring(state.visible.size()) + L" profile(s).");
}

core::Status ClearVisible() {
    storage::ChromeRuntimeState& state =
        storage::ChromeStateFor(storage::CurrentSettings().chrome_runtime);
    if (state.visible.empty())
        return core::Info(L"No profiles are shown for this runtime.");
    state.visible.clear();
    storage::SaveSettings();
    return core::Success(state.preset.empty()
        ? L"Cleared."
        : L"Cleared. Load Preset restores the saved set.");
}

core::Status ReorderCards(size_t from_index, size_t to_index) {
    std::vector<storage::VisibleProfile>& visible = VisibleList();
    if (from_index >= visible.size() || to_index >= visible.size())
        return core::Error(L"Index out of range.");
    if (from_index == to_index) return core::NoStatus();
    storage::VisibleProfile moved = visible[from_index];
    visible.erase(visible.begin() + static_cast<ptrdiff_t>(from_index));
    visible.insert(visible.begin() + static_cast<ptrdiff_t>(to_index), std::move(moved));
    storage::SaveSettings();
    return core::Success(L"Profile order saved.");
}

bool LoadProfileCache() {
    g_cache = ProfileCache{};
    std::wstring text;
    if (!files::ReadText(paths::ChromeProfilesFile(), &text, nullptr)) return false;
    json::Value root;
    if (!json::Parse(text, &root, nullptr) || !root.is_object()) return false;
    ReadCachedRuntime(root.ObjectField(L"windows"), ChromeRuntime::Windows, &g_cache.windows);
    ReadCachedRuntime(root.ObjectField(L"wsl"),     ChromeRuntime::Wsl,     &g_cache.wsl);
    return true;
}

ChromeEmptyState CardEmptyState() {
    const CachedRuntime& section = g_cache.For(ActiveRuntime());
    if (!section.scanned)           return ChromeEmptyState::NeverScanned;
    if (section.profiles.empty())   return ChromeEmptyState::NoneFound;
    if (VisibleList().empty())      return ChromeEmptyState::NoneVisible;
    return ChromeEmptyState::UseManage;
}

}  // namespace features
