// Claude Code configuration injector — business logic only, no UI types.
//
// Handles: settings.json path resolution, base URL / API key / model management,
// provider list CRUD with deduplication, Plan/Run split for the WSL probe gate.
#pragma once
#include <string>
#include <vector>

#include "core/status.h"

namespace storage {
struct BaseUrl;
struct ApiKey;
}

namespace features {

enum class InjectTarget { Windows, UbuntuWsl };

std::wstring InjectTargetName(InjectTarget target);
InjectTarget InjectTargetFromName(const std::wstring& name);

// Ensures the persisted json_target is set to a valid token. Call on app start.
// Saves settings when the value was missing. Returns the active target.
InjectTarget EnsureDefaultTarget();

// Returns true when the WSL path cannot be resolved yet (probe still needed).
bool InjectNeedsWslProbe(InjectTarget target);

// --- base-URL management ---

// Adds a new entry. If a case-insensitive URL duplicate exists, selects it and
// returns a Status::Info (not an error — the duplicate is now selected).
// On success the new entry is selected and providers are saved.
core::Status AddBaseUrl(const std::wstring& url, const std::wstring& label,
                        const std::wstring& model);

// Selects a base URL by id and saves providers.
core::Status SelectBaseUrl(const std::wstring& id);

// Updates the model for the currently selected base URL (dirty-check, saves only
// when the value changed).
core::Status CommitModel(const std::wstring& model_text);

// --- API-key management ---

// Parses "Label | key" or bare "key" lines; blank lines ignored.
// Returned keys have empty ids — caller assigns ids after dedup.
struct ParsedKey { std::wstring label; std::wstring key; };
std::vector<ParsedKey> ParseBulkKeys(const std::wstring& text);

// Adds keys to the currently selected base URL.
// Case-sensitive exact-match dedup. Saves providers when at least one key was added.
struct BulkAddResult {
    int added = 0;
    int skipped = 0;
    bool persist_failed = false;  // added keys but the save to disk failed
};
BulkAddResult BulkAddApiKeys(const std::vector<ParsedKey>& keys);

// Selects an API key by id for the currently selected base URL. Saves providers.
core::Status SelectApiKey(const std::wstring& id);

// --- inject ---

// Validates that a base URL and API key are selected, then applies them to the
// settings file. Backs up before writing.
core::Status Inject(InjectTarget target);

// Display helpers (no persistence, no side effects).
std::wstring BaseUrlDisplayText(const storage::BaseUrl& base);
std::wstring ApiKeyDisplayText(const storage::ApiKey& key);

// Display lists for the facade: one string per entry.
std::vector<std::wstring> BaseUrlDisplayList();
// API keys of the currently selected base URL.
std::vector<std::wstring> ApiKeyDisplayList();

// Selection auto-heal: if the stored selected_base_url_id is not in the list,
// select the first entry and save. Call on screen enter and after any list change.
void HealBaseUrlSelection();
// Same for the API key within the currently selected base URL.
void HealApiKeySelection();

}  // namespace features
