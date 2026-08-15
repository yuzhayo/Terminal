#include "features/chrome_visible_set.h"

#include <algorithm>
#include <utility>

#include "storage/settings.h"

namespace features {
namespace {

std::wstring RowKey(const std::wstring& browser, const std::wstring& directory) {
    return browser + L"|" + directory;
}

}  // namespace

std::vector<VisibleSetRow> BuildRows(ChromeRuntime runtime,
                                     const std::vector<ChromeProfile>& cached_profiles,
                                     VisibleSetDraft* draft) {
    if (!draft->loaded) {
        const std::vector<storage::VisibleProfile>& visible =
            storage::ChromeStateFor(ChromeRuntimeName(runtime)).visible;
        for (const storage::VisibleProfile& p : visible)
            draft->keys.push_back(RowKey(p.browser, p.directory));
        draft->loaded = true;
    }

    std::vector<VisibleSetRow> rows;
    for (const ChromeProfile& profile : cached_profiles) {
        VisibleSetRow row;
        row.browser   = profile.browser;
        row.directory = profile.directory;
        row.name      = profile.name.empty() ? profile.directory : profile.name;
        row.label     = row.name + L"    " + profile.RuntimeLabel() + L" · " + profile.directory;
        row.key       = RowKey(profile.browser, profile.directory);
        row.checked   = std::find(draft->keys.begin(), draft->keys.end(), row.key) != draft->keys.end();
        rows.push_back(std::move(row));
    }
    return rows;
}

void ToggleRow(size_t index, const std::vector<VisibleSetRow>& rows, VisibleSetDraft* draft) {
    if (index >= rows.size()) return;
    const std::wstring& key = rows[index].key;
    const auto it = std::find(draft->keys.begin(), draft->keys.end(), key);
    if (it != draft->keys.end()) {
        draft->keys.erase(it);
    } else {
        // Append at the end: preserves the existing card order for already-selected entries.
        draft->keys.push_back(key);
    }
}

void SelectAll(bool select, const std::vector<VisibleSetRow>& rows, VisibleSetDraft* draft) {
    if (!select) {
        draft->keys.clear();
        return;
    }
    for (const VisibleSetRow& row : rows) {
        if (std::find(draft->keys.begin(), draft->keys.end(), row.key) == draft->keys.end())
            draft->keys.push_back(row.key);
    }
}

bool Apply(ChromeRuntime runtime, VisibleSetDraft* draft) {
    // Rebuild visible list in draft-key order. Keys absent from the current cache
    // are silently dropped (the profile may have been removed or renamed).
    const std::wstring runtime_name = ChromeRuntimeName(runtime);
    std::vector<storage::VisibleProfile> visible;

    // Walk draft keys; look each up in the saved settings to get the name.
    // The authoritative data for available profiles is in the cache (chrome_profiles),
    // but we only have the storage::VisibleProfile shape here. The name comes from
    // whatever was last reconciled by ApplyScan — good enough.
    const std::vector<storage::VisibleProfile>& existing =
        storage::ChromeStateFor(runtime_name).visible;

    for (const std::wstring& key : draft->keys) {
        // Parse key: "browser|directory"
        const size_t bar = key.find(L'|');
        if (bar == std::wstring::npos) continue;
        const std::wstring browser   = key.substr(0, bar);
        const std::wstring directory = key.substr(bar + 1);
        if (browser.empty() || directory.empty()) continue;

        // Try to carry the existing saved name forward.
        std::wstring name;
        for (const storage::VisibleProfile& p : existing)
            if (p.browser == browser && p.directory == directory) { name = p.name; break; }

        storage::VisibleProfile saved;
        saved.runtime   = runtime_name;
        saved.browser   = browser;
        saved.directory = directory;
        saved.name      = name;
        visible.push_back(std::move(saved));
    }

    storage::ChromeStateFor(runtime_name).visible = std::move(visible);
    storage::SaveSettings();
    draft->loaded = false;
    draft->keys.clear();
    return true;
}

void ResetDrafts(VisibleSetDraft* windows_draft, VisibleSetDraft* wsl_draft) {
    *windows_draft = VisibleSetDraft{};
    *wsl_draft     = VisibleSetDraft{};
}

}  // namespace features
