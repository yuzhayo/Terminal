// CoreApplication — typed facade for all business logic.
// This is the public facade used by application adapters.
//
// Responsibilities:
// - Owns shared state: settings, chrome cache, provider list.
// - Exposes typed feature contracts (terminal, inject, editor, chrome, settings).
// - Does not depend on UI types (HWND, component, renderer, JSON screen config).
// - Does not run threads; blocking operations are marked and the caller runs them
//   on a worker, then applies the result on the owner thread.
//
// Not exposed:
// - app_shell (navigation/command/tray/single-instance stays in ApplicationContainer)
// - ui_config_draft (UI Editor uses UiConfigGate candidate/reload contract directly)
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/status.h"

// Forward declarations for feature types.
namespace features {
    struct TerminalRequest;
    struct TerminalPlan;
    enum class TerminalTarget;
    struct ChromeProfile;
    struct ChromeBookmark;
    struct ChromeScanResult;
    struct CardLaunchResult;
    struct EditorDraft;
    enum class EditorTarget;
    struct EditorLoadResult;
    enum class ChromeRuntime;
    enum class InjectTarget;
    struct InjectChoice;
}

namespace application {

class CoreApplication {
public:
    CoreApplication();
    ~CoreApplication();

    // --- Initialization (call once on startup) ---
    // Loads settings.json, providers.json, chrome_profiles.json.
    // Safe to call multiple times; subsequent calls are no-op.
    core::Status Initialize();

    // --- Terminal Launcher ---
    // Validates folder + venv without launching. Returns validation errors or NoStatus.
    core::Status PlanTerminalLaunch(const features::TerminalRequest& req,
                                    features::TerminalPlan* out_plan);

    // Launches PowerShell/Admin/WSL. On success, persists the recent folder and venv preference.
    // Blocking: WSL readiness probe may take 100–500ms; run on a worker if needed.
    core::Status LaunchTerminal(const features::TerminalRequest& req);

    std::vector<std::wstring> RecentFolders() const;
    std::wstring TerminalFolder() const;
    bool TerminalVenvEnabled(features::TerminalTarget target) const;
    core::Status SetTerminalVenvEnabled(features::TerminalTarget target, bool enabled);
    core::Status ClearRecentFolders();

    // --- Claude Inject ---
    core::Status AddBaseUrl(const std::wstring& url, const std::wstring& label = {},
                            const std::wstring& model = {});
    core::Status BulkAddApiKeys(const std::vector<std::wstring>& keys);
    core::Status SelectBaseUrl(const std::wstring& id);
    core::Status SelectApiKey(const std::wstring& id);
    core::Status CommitInjectModel(const std::wstring& model);
    core::Status InjectClaude(features::InjectTarget target);

    std::vector<features::InjectChoice> BaseUrlChoices() const;
    std::vector<features::InjectChoice> ApiKeyChoices() const;
    std::wstring SelectedBaseUrlId() const;
    std::wstring SelectedApiKeyId() const;
    std::wstring SelectedInjectModel() const;

    // --- Claude Settings File Editor ---
    // Blocking: WSL file read may take 100–500ms for the first call; run on a worker.
    features::EditorLoadResult StartEditorLoad(features::EditorTarget target,
                                               bool force,
                                               const features::EditorDraft& draft);
    core::Status ApplyEditorLoad(const features::EditorLoadResult& result,
                                 features::EditorDraft* draft);

    bool EditorReadyForFileAction(features::EditorTarget target, core::Status* status);
    core::Status SaveEditor(features::EditorTarget target, features::EditorDraft* draft);
    core::Status RestoreEditorBackup(features::EditorTarget target);
    core::Status ValidateEditor(const features::EditorDraft& draft) const;

    // --- Chrome Profiles ---
    // Blocking: profile scan may take 200–1000ms for WSL; run on a worker.
    features::ChromeScanResult StartChromeScan(features::ChromeRuntime runtime);
    core::Status ApplyChromeScan(const features::ChromeScanResult& result);

    core::Status SwitchChromeRuntime(features::ChromeRuntime runtime);
    features::ChromeRuntime ActiveChromeRuntime() const;

    // Launch card at index in the active runtime's visible list.
    // typed_url is raw user input (empty = use selected bookmark).
    features::CardLaunchResult LaunchChromeCard(size_t index, const std::wstring& typed_url);

    // Bookmark CRUD. RemoveBookmark removes the currently selected bookmark.
    core::Status AddChromeBookmark(const std::wstring& label, const std::wstring& url);
    core::Status RemoveChromeBookmark();
    core::Status SelectChromeBookmark(size_t index);

    // Preset management for the active runtime.
    core::Status SaveChromePreset();
    core::Status LoadChromePreset();
    core::Status ClearVisibleChromeProfiles();

    // Reorder visible cards (drag-drop result).
    core::Status ReorderChromeCards(size_t from_index, size_t to_index);
    std::vector<features::ChromeProfile> CachedChromeProfiles(
        features::ChromeRuntime runtime) const;
    std::vector<features::ChromeProfile> VisibleChromeProfiles(
        features::ChromeRuntime runtime) const;
    std::vector<features::ChromeBookmark> ChromeBookmarks() const;
    std::wstring SelectedChromeBookmarkId() const;
    core::Status SelectChromeBookmarkById(const std::wstring& id);

    // --- App Settings ---
    std::wstring CurrentTheme() const;  // "dark" or "light"
    core::Status SetTheme(const std::wstring& token);
    bool ConfirmBeforeRun() const;
    core::Status SetConfirmBeforeRun(bool enabled);

    bool IsStartWithWindowsEnabled() const;
    core::Status SetStartWithWindows(bool enabled);
    bool SyncStartWithWindows();  // re-reads OS state; returns true if changed

    std::wstring AppDataDirPath() const;
    std::wstring UiConfigPath() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace application
