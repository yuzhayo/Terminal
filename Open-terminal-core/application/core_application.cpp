#include "application/core_application.h"

#include "features/app_settings.h"
#include "features/chrome_profiles.h"
#include "features/claude_inject.h"
#include "features/claude_settings_file.h"
#include "features/terminal_launch.h"
#include "storage/settings.h"

namespace application {

class CoreApplication::Impl {
public:
    bool initialized = false;
};

CoreApplication::CoreApplication() : impl_(std::make_unique<Impl>()) {}
CoreApplication::~CoreApplication() = default;

void CoreApplication::Initialize() {
    if (impl_->initialized) return;
    storage::Load();             // loads settings.json and providers.json
    features::LoadProfileCache();
    impl_->initialized = true;
}

// --- Terminal ---

core::Status CoreApplication::PlanTerminalLaunch(
    const features::TerminalRequest& req, features::TerminalPlan* out_plan) {
    *out_plan = features::Plan(req);
    if (out_plan->ok) return core::NoStatus();
    return core::Error(core::ErrorCode::FolderNotFound, out_plan->error);
}

core::Status CoreApplication::LaunchTerminal(const features::TerminalRequest& req) {
    return features::Run(req);
}

std::vector<std::wstring> CoreApplication::RecentFolders() const {
    return storage::CurrentSettings().recent_folders;
}

void CoreApplication::ClearRecentFolders() {
    storage::CurrentSettings().recent_folders.clear();
    storage::SaveSettings();
}

// --- Claude Inject ---

core::Status CoreApplication::AddBaseUrl(const std::wstring& url) {
    return features::AddBaseUrl(url, L"", L"");
}

core::Status CoreApplication::BulkAddApiKeys(
    const std::vector<std::wstring>& keys) {
    std::vector<features::ParsedKey> parsed;
    parsed.reserve(keys.size());
    for (const std::wstring& raw : keys) {
        for (const features::ParsedKey& k : features::ParseBulkKeys(raw))
            parsed.push_back(k);
    }
    const features::BulkAddResult result = features::BulkAddApiKeys(parsed);
    if (result.persist_failed)
        return core::Error(core::ErrorCode::PersistenceFailed, L"Could not save the API keys.");
    return result.added > 0
        ? core::Success(L"Added " + std::to_wstring(result.added) + L" API key(s).")
        : core::Info(L"No new API keys to add.");
}

core::Status CoreApplication::InjectClaude(bool target_wsl) {
    const features::InjectTarget target = target_wsl
        ? features::InjectTarget::UbuntuWsl
        : features::InjectTarget::Windows;
    if (features::InjectNeedsWslProbe(target))
        return core::Info(
            L"Still looking for the Ubuntu WSL home directory — try again in a moment.");
    return features::Inject(target);
}

std::vector<std::wstring> CoreApplication::BaseUrls() const {
    return features::BaseUrlDisplayList();
}

std::vector<std::wstring> CoreApplication::ApiKeys() const {
    return features::ApiKeyDisplayList();
}

// --- Claude Settings File Editor ---

features::EditorLoadResult CoreApplication::StartEditorLoad(
    features::EditorTarget target, bool force,
    const features::EditorDraft& draft) {
    return features::StartLoad(target, force, draft);
}

core::Status CoreApplication::ApplyEditorLoad(
    const features::EditorLoadResult& result, features::EditorDraft* draft) {
    return features::ApplyLoad(result, draft);
}

bool CoreApplication::EditorReadyForFileAction(features::EditorTarget target,
                                                core::Status* status) {
    return features::ReadyForFileAction(target, status);
}

core::Status CoreApplication::SaveEditor(features::EditorTarget target,
                                          features::EditorDraft* draft) {
    return features::Save(target, draft);
}

core::Status CoreApplication::RestoreEditorBackup(features::EditorTarget target) {
    return features::RestoreBackup(target);
}

// --- Chrome Profiles ---

features::ChromeScanResult CoreApplication::StartChromeScan(features::ChromeRuntime runtime) {
    return features::ScanProfiles(runtime);
}

core::Status CoreApplication::ApplyChromeScan(const features::ChromeScanResult& result) {
    return features::ApplyScan(result);
}

core::Status CoreApplication::SwitchChromeRuntime(features::ChromeRuntime runtime) {
    return features::SwitchRuntime(runtime);
}

features::ChromeRuntime CoreApplication::ActiveChromeRuntime() const {
    return features::ActiveRuntime();
}

features::CardLaunchResult CoreApplication::LaunchChromeCard(
    size_t index, const std::wstring& typed_url) {
    return features::LaunchCard(index, typed_url);
}

core::Status CoreApplication::AddChromeBookmark(const std::wstring& label,
                                                 const std::wstring& url) {
    return features::AddBookmark(label, url);
}

core::Status CoreApplication::RemoveChromeBookmark() {
    return features::RemoveBookmark();
}

core::Status CoreApplication::SelectChromeBookmark(size_t index) {
    return features::SelectBookmark(index);
}

core::Status CoreApplication::SaveChromePreset() {
    return features::SavePreset();
}

core::Status CoreApplication::LoadChromePreset() {
    return features::LoadPreset();
}

core::Status CoreApplication::ClearVisibleChromeProfiles() {
    return features::ClearVisible();
}

core::Status CoreApplication::ReorderChromeCards(size_t from_index, size_t to_index) {
    return features::ReorderCards(from_index, to_index);
}

// --- App Settings ---

std::wstring CoreApplication::CurrentTheme() const {
    return features::CurrentThemeToken();
}

core::Status CoreApplication::SetTheme(const std::wstring& token) {
    return features::SetTheme(token);
}

bool CoreApplication::IsStartWithWindowsEnabled() const {
    return features::IsStartWithWindowsEnabled();
}

core::Status CoreApplication::SetStartWithWindows(bool enabled) {
    return features::SetStartWithWindows(enabled);
}

bool CoreApplication::SyncStartWithWindows() {
    return features::SyncStartWithWindows();
}

std::wstring CoreApplication::AppDataDirPath() const {
    return features::AppDataDirPath();
}

std::wstring CoreApplication::UiConfigPath() const {
    return features::UiConfigPath();
}

}  // namespace application
