// Chrome profile discovery, cache management, launch, bookmarks, and presets.
// No UI types — the screen passes requests in and gets results back.
//
// Async pattern: ScanProfiles() is blocking and should run on a worker thread.
// Hand the ScanResult to ApplyScan() on your own thread when it completes.
#pragma once
#include <string>
#include <vector>

#include "core/status.h"

namespace storage { struct VisibleProfile; }

namespace features {

enum class ChromeRuntime { Windows, Wsl };

std::wstring ChromeRuntimeName(ChromeRuntime runtime);
ChromeRuntime ChromeRuntimeFromName(const std::wstring& name);

// --- profile model ---

struct ChromeProfile {
    ChromeRuntime runtime = ChromeRuntime::Windows;
    std::wstring browser;    // "chrome" or "chromium"
    std::wstring directory;  // profile dir name, e.g. "Default"
    std::wstring name;       // display name from Local State
    bool available = true;

    std::wstring Key() const;
    std::wstring RuntimeLabel() const;
};

// --- scan result (produced by worker, consumed by ApplyScan) ---

struct ChromeScanResult {
    ChromeRuntime runtime = ChromeRuntime::Windows;
    bool ok = false;
    std::wstring distro;
    std::wstring error;
    std::vector<ChromeProfile> profiles;
};

// Runs a full profile scan for one runtime. BLOCKING — call from a worker thread
// for WSL (wsl::Resolve can take up to 8 seconds on a cold cache).
ChromeScanResult ScanProfiles(ChromeRuntime runtime);

// Applies a finished scan: stores to cache file, reconciles saved names, saves
// settings. Returns a status for the UI.
core::Status ApplyScan(const ChromeScanResult& result);

// --- launch ---

// Returns the URL from the typed input (priority) or the selected bookmark.
// Pass the raw typed text from the UI.
std::wstring ResolveUrl(const std::wstring& typed_url);

struct CardLaunchResult {
    core::Status status;
    std::wstring url_opened;   // empty when no URL was passed
    bool clear_input = false;  // true when the typed URL was used and launch succeeded
};

struct ChromeBookmark {
    std::wstring id;
    std::wstring label;
    std::wstring url;
};

// Launches the card at `index` in the active runtime's visible list. `typed_url`
// is the raw text from the URL input field.
CardLaunchResult LaunchCard(size_t index, const std::wstring& typed_url);

// --- runtime switch ---
core::Status SwitchRuntime(ChromeRuntime runtime);
ChromeRuntime ActiveRuntime();

// --- bookmark CRUD ---
core::Status AddBookmark(const std::wstring& label, const std::wstring& url);
core::Status RemoveBookmark();           // removes the currently selected bookmark
core::Status SelectBookmark(size_t index);  // toggle-on-reclick semantics

// --- preset management ---
core::Status SavePreset();
core::Status LoadPreset();
core::Status ClearVisible();

// --- reorder (drag-drop result) ---
core::Status ReorderCards(size_t from_index, size_t to_index);

// --- cache ---
// Loads chrome_profiles.json into the process-wide cache. Missing/corrupt →
// empty cache, false return. Never auto-scans.
bool LoadProfileCache();

// --- empty-state text: avoids embedding text in the paint path ---
enum class ChromeEmptyState {
    NeverScanned,    // cache has no scan record for this runtime
    NoneFound,       // scanned and found nothing
    NoneVisible,     // found profiles but none are selected
    UseManage,       // suggest Manage Profiles
};
ChromeEmptyState CardEmptyState();

std::vector<ChromeProfile> CachedProfiles(ChromeRuntime runtime);
std::vector<ChromeProfile> VisibleProfiles(ChromeRuntime runtime);
std::vector<ChromeBookmark> Bookmarks();
std::wstring SelectedBookmarkId();
core::Status SelectBookmarkById(const std::wstring& id);

}  // namespace features
