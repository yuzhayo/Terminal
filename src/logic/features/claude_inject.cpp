#include "features/claude_inject.h"

#include "platform/files.h"
#include "platform/paths.h"
#include "platform/strings.h"
#include "platform/wsl.h"
#include "storage/json.h"
#include "storage/settings.h"

namespace features {
namespace {

constexpr wchar_t kEnvBaseUrl[]   = L"ANTHROPIC_BASE_URL";
constexpr wchar_t kEnvAuthToken[] = L"ANTHROPIC_AUTH_TOKEN";
constexpr wchar_t kEnvModel[]     = L"ANTHROPIC_MODEL";

bool ResolveSettingsPath(InjectTarget target, std::wstring* path, std::wstring* error) {
    if (target == InjectTarget::Windows) {
        const std::wstring home = paths::UserProfileDir();
        if (home.empty()) {
            if (error) *error = L"Cannot determine the Windows user profile folder.";
            return false;
        }
        *path = paths::Join(home, L".claude\\settings.json");
        return true;
    }
    return wsl::HomeFile(L".claude/settings.json", path, error);
}

bool LoadRoot(const std::wstring& path, json::Value* root, std::wstring* error) {
    if (!paths::FileExists(path)) { *root = json::Value::Object(); return true; }
    std::wstring text;
    if (!files::ReadText(path, &text, error)) return false;
    if (str::Trim(text).empty()) { *root = json::Value::Object(); return true; }
    if (!json::Parse(text, root, error)) {
        if (error) *error = L"The existing settings file is not valid JSON:\n" + *error;
        return false;
    }
    if (!root->is_object()) {
        if (error) *error = L"The existing settings file must contain a JSON object.";
        return false;
    }
    return true;
}

}  // namespace

std::wstring InjectTargetName(InjectTarget target) {
    return target == InjectTarget::UbuntuWsl ? L"wsl" : L"windows";
}

InjectTarget InjectTargetFromName(const std::wstring& name) {
    return name == L"wsl" ? InjectTarget::UbuntuWsl : InjectTarget::Windows;
}

InjectTarget EnsureDefaultTarget() {
    storage::Settings& s = storage::CurrentSettings();
    if (s.json_target.empty()) {
        s.json_target = L"windows";
        storage::SaveSettings();
    }
    return InjectTargetFromName(s.json_target);
}

bool InjectNeedsWslProbe(InjectTarget target) {
    return target == InjectTarget::UbuntuWsl && !wsl::IsResolved();
}

// --- display helpers ---

std::wstring BaseUrlDisplayText(const storage::BaseUrl& base) {
    if (base.label.empty()) return base.url;
    return base.label + L"  —  " + base.url;
}

std::wstring ApiKeyDisplayText(const storage::ApiKey& key) {
    if (key.label.empty()) return key.key;
    return key.label + L"  —  " + key.key;
}

std::vector<std::wstring> BaseUrlDisplayList() {
    std::vector<std::wstring> out;
    for (const storage::BaseUrl& base : storage::CurrentProviders().base_urls)
        out.push_back(BaseUrlDisplayText(base));
    return out;
}

std::vector<std::wstring> ApiKeyDisplayList() {
    std::vector<std::wstring> out;
    const storage::BaseUrl* base =
        storage::FindBaseUrl(storage::CurrentProviders().selected_base_url_id);
    if (!base) return out;
    for (const storage::ApiKey& key : base->keys)
        out.push_back(ApiKeyDisplayText(key));
    return out;
}

// --- selection auto-heal ---

void HealBaseUrlSelection() {
    storage::Providers& p = storage::CurrentProviders();
    if (p.base_urls.empty()) return;
    for (const storage::BaseUrl& b : p.base_urls)
        if (b.id == p.selected_base_url_id) return;
    p.selected_base_url_id = p.base_urls.front().id;
    storage::SaveProviders();
}

void HealApiKeySelection() {
    storage::BaseUrl* base = storage::FindBaseUrl(storage::CurrentProviders().selected_base_url_id);
    if (!base || base->keys.empty()) return;
    for (const storage::ApiKey& k : base->keys)
        if (k.id == base->selected_key_id) return;
    base->selected_key_id = base->keys.front().id;
    storage::SaveProviders();
}

std::vector<InjectChoice> BaseUrlChoices() {
    std::vector<InjectChoice> result;
    for (const storage::BaseUrl& base : storage::CurrentProviders().base_urls) {
        result.push_back({base.id, BaseUrlDisplayText(base)});
    }
    return result;
}

std::vector<InjectChoice> ApiKeyChoices() {
    std::vector<InjectChoice> result;
    const storage::BaseUrl* base =
        storage::FindBaseUrl(storage::CurrentProviders().selected_base_url_id);
    if (!base) return result;
    for (const storage::ApiKey& key : base->keys) {
        result.push_back({key.id, ApiKeyDisplayText(key)});
    }
    return result;
}

std::wstring SelectedBaseUrlId() {
    return storage::CurrentProviders().selected_base_url_id;
}

std::wstring SelectedApiKeyId() {
    const storage::BaseUrl* base =
        storage::FindBaseUrl(storage::CurrentProviders().selected_base_url_id);
    return base ? base->selected_key_id : std::wstring{};
}

std::wstring SelectedModel() {
    const storage::BaseUrl* base =
        storage::FindBaseUrl(storage::CurrentProviders().selected_base_url_id);
    return base ? base->model : std::wstring{};
}

// --- base-URL management ---

core::Status AddBaseUrl(const std::wstring& url, const std::wstring& label,
                        const std::wstring& model) {
    const std::wstring trimmed_url = str::Trim(url);
    if (trimmed_url.empty()) return core::Error(core::ErrorCode::InvalidBaseUrl, L"Base URL cannot be empty.");

    storage::Providers& providers = storage::CurrentProviders();
    for (const storage::BaseUrl& existing : providers.base_urls) {
        if (str::IEquals(existing.url, trimmed_url)) {
            providers.selected_base_url_id = existing.id;
            // Not an error — the duplicate is now selected so the user can proceed.
            return core::Info(L"That base URL already exists — it has been selected.");
        }
    }
    storage::BaseUrl base;
    base.id    = storage::NewId();
    base.url   = trimmed_url;
    base.label = str::Trim(label);
    base.model = str::Trim(model);
    providers.base_urls.push_back(std::move(base));
    providers.selected_base_url_id = providers.base_urls.back().id;
    if (!storage::SaveProviders())
        return core::Error(core::ErrorCode::PersistenceFailed, L"Could not save the base URL.");
    return core::Success(L"Base URL added.");
}

core::Status SelectBaseUrl(const std::wstring& id) {
    if (!storage::FindBaseUrl(id)) return core::Error(core::ErrorCode::ValidationFailed, L"Base URL not found.");
    storage::CurrentProviders().selected_base_url_id = id;
    const bool saved = storage::SaveProviders();
    HealApiKeySelection();
    if (!saved) return core::Error(core::ErrorCode::PersistenceFailed, L"Could not save the selection.");
    return core::NoStatus();
}

core::Status CommitModel(const std::wstring& model_text) {
    storage::BaseUrl* base = storage::FindBaseUrl(storage::CurrentProviders().selected_base_url_id);
    if (!base) return core::NoStatus();
    const std::wstring model = str::Trim(model_text);
    if (model == base->model) return core::NoStatus();
    base->model = model;
    storage::SaveProviders();
    return core::NoStatus();
}

// --- API-key management ---

std::vector<ParsedKey> ParseBulkKeys(const std::wstring& text) {
    std::vector<ParsedKey> out;
    for (const std::wstring& raw : str::SplitLines(text)) {
        const std::wstring line = str::Trim(raw);
        if (line.empty()) continue;
        ParsedKey k;
        const size_t bar = line.find(L'|');
        if (bar != std::wstring::npos) {
            k.label = str::Trim(line.substr(0, bar));
            k.key   = str::Trim(line.substr(bar + 1));
        } else {
            k.key = line;
        }
        if (!k.key.empty()) out.push_back(std::move(k));
    }
    return out;
}

BulkAddResult BulkAddApiKeys(const std::vector<ParsedKey>& keys) {
    BulkAddResult result;
    storage::BaseUrl* base = storage::FindBaseUrl(storage::CurrentProviders().selected_base_url_id);
    if (!base) return result;

    for (const ParsedKey& candidate : keys) {
        bool duplicate = false;
        for (const storage::ApiKey& existing : base->keys) {
            if (existing.key == candidate.key) { duplicate = true; break; }
        }
        if (duplicate) { ++result.skipped; continue; }
        storage::ApiKey key;
        key.id    = storage::NewId();
        key.label = candidate.label;
        key.key   = candidate.key;
        base->keys.push_back(std::move(key));
        ++result.added;
    }

    if (result.added > 0) {
        if (base->selected_key_id.empty())
            base->selected_key_id = base->keys.front().id;
        result.persist_failed = !storage::SaveProviders();
    }
    return result;
}

core::Status SelectApiKey(const std::wstring& id) {
    storage::BaseUrl* base = storage::FindBaseUrl(storage::CurrentProviders().selected_base_url_id);
    if (!base) return core::Error(core::ErrorCode::ValidationFailed, L"No base URL selected.");
    if (!storage::FindApiKey(base, id)) return core::Error(core::ErrorCode::ValidationFailed, L"API key not found.");
    base->selected_key_id = id;
    if (!storage::SaveProviders())
        return core::Error(core::ErrorCode::PersistenceFailed, L"Could not save the selection.");
    return core::NoStatus();
}

// --- inject ---

core::Status Inject(InjectTarget target) {
    storage::BaseUrl* base = storage::FindBaseUrl(storage::CurrentProviders().selected_base_url_id);
    if (!base) return core::Error(core::ErrorCode::ValidationFailed, L"Add a base URL first.");
    storage::ApiKey* key = storage::FindApiKey(base, base->selected_key_id);
    if (!key) return core::Error(core::ErrorCode::InvalidApiKey, L"Add and select an API key first.");

    std::wstring path;
    std::wstring path_error;
    if (!ResolveSettingsPath(target, &path, &path_error))
        return core::Error(core::ErrorCode::SettingsFileNotFound, path_error);

    json::Value root;
    std::wstring load_error;
    if (!LoadRoot(path, &root, &load_error))
        return core::Error(core::ErrorCode::SettingsFileMalformed, load_error);

    json::Value* env = root.Find(L"env");
    if (!env || !env->is_object()) {
        root.Set(L"env", json::Value::Object());
        env = root.Find(L"env");
    }
    env->Set(kEnvBaseUrl,   json::Value::String(base->url));
    env->Set(kEnvAuthToken, json::Value::String(key->key));
    if (base->model.empty()) {
        env->Remove(kEnvModel);
    } else {
        env->Set(kEnvModel, json::Value::String(base->model));
    }

    const std::wstring serialized = json::Serialize(root);

    // Validate the output before touching disk.
    json::Value verify;
    std::wstring verify_error;
    if (!json::Parse(serialized, &verify, &verify_error))
        return core::Error(core::ErrorCode::InvalidJson, L"Refusing to write invalid JSON: " + verify_error);

    std::wstring io_error;
    if (!files::MakeBackup(path, &io_error))
        return core::Error(core::ErrorCode::FileWriteFailed, io_error);
    if (!files::WriteTextAtomic(path, serialized, &io_error))
        return core::Error(core::ErrorCode::SettingsFileWriteFailed, io_error);

    return core::Success(L"Applied to " + path);
}

}  // namespace features
