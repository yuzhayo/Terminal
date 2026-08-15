// Chrome visible-set editor — business logic only, no UI types.
//
// The only job: choose which cached profiles appear as cards, and in what order.
// It does NOT create, rename, delete, or edit Chrome profiles.
//
// Per-runtime drafts survive a runtime switch within a session, but are reset on
// screen entry (Activate) so Back always discards un-applied edits.
#pragma once
#include <string>
#include <vector>

#include "core/status.h"
#include "features/chrome_profiles.h"

namespace features {

// A row in the profile list, built from the process-wide cache.
struct VisibleSetRow {
    std::wstring browser;
    std::wstring directory;
    std::wstring name;       // display name; falls back to directory when empty
    std::wstring label;      // full label for the UI: name + runtime label + directory
    std::wstring key;        // browser|directory
    bool checked = false;    // reflects the current draft
};

// Per-runtime draft of the selection.
struct VisibleSetDraft {
    bool loaded = false;
    std::vector<std::wstring> keys;  // browser|directory keys, in display order
};

// Builds rows from a pre-loaded profile list (pass the result of
// features::ScanProfiles or the cached list after features::LoadProfileCache).
// Also seeds the draft from the currently saved visible list when not yet loaded.
std::vector<VisibleSetRow> BuildRows(ChromeRuntime runtime,
                                     const std::vector<ChromeProfile>& cached_profiles,
                                     VisibleSetDraft* draft);

// Toggle one row by index. New selections append at the end, preserving the
// existing card order for all previously-selected entries.
void ToggleRow(size_t index, const std::vector<VisibleSetRow>& rows, VisibleSetDraft* draft);

// Select all / clear all.
void SelectAll(bool select, const std::vector<VisibleSetRow>& rows, VisibleSetDraft* draft);

// Writes the draft to storage and saves settings. Keys absent from the cache are
// silently dropped. Resets the draft so the next entry reads fresh from disk.
// Returns true (no failure path — a save-settings failure surfaces as a bool).
bool Apply(ChromeRuntime runtime, VisibleSetDraft* draft);

// Resets both drafts. Call on screen entry so Back discards un-applied edits.
void ResetDrafts(VisibleSetDraft* windows_draft, VisibleSetDraft* wsl_draft);

}  // namespace features
