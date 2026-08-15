#include "ui/application/stub_application_bridge.h"

#include <stdexcept>

namespace ui::application {

namespace {

std::optional<std::string> PayloadString(const UiEvent& event, std::string_view key) {
    const auto found = event.payload.find(key);
    if (found == event.payload.end()) return std::nullopt;
    return std::get_if<std::string>(&found->second.value)
               ? std::optional<std::string>(*std::get_if<std::string>(&found->second.value))
               : std::nullopt;
}

std::optional<std::string> PayloadScalar(const UiEvent& event, std::string_view key) {
    const auto found = event.payload.find(key);
    if (found == event.payload.end()) return std::nullopt;
    if (const auto* value = std::get_if<std::string>(&found->second.value)) return *value;
    if (const auto* value = std::get_if<bool>(&found->second.value)) {
        return *value ? "true" : "false";
    }
    if (const auto* value = std::get_if<std::int64_t>(&found->second.value)) {
        return std::to_string(*value);
    }
    return std::nullopt;
}

bool ValidEventEnvelope(const UiEvent& event) noexcept {
    return event.source.window_instance_id != 0 &&
           event.source.screen_instance_id != 0 &&
           event.source.component_instance_id != 0 &&
           !event.source.window_id.empty() && !event.source.route_id.empty() &&
           !event.source.component_id.empty() && !event.event_type.empty() &&
           !event.action.empty() && event.config_generation != 0;
}

std::string CanonicalBinding(std::string_view binding) {
    constexpr std::string_view prefix = "viewState.";
    if (binding.starts_with(prefix)) return std::string(binding);
    return std::string(prefix) + std::string(binding);
}

}  // namespace

StubApplicationBridge::StubApplicationBridge() {
    InitializeDeterministicState();
    RegisterTerminalFeature();
    RegisterJsonInjectFeature();
    RegisterJsonEditorFeature();
    RegisterChromeLauncherFeature();
    RegisterChromeProfileManagerFeature();
    RegisterSettingsFeature();
    RegisterUiEditorFeature();
    RegisterDialogFeature();
    RegisterNavigationFeature();
}

std::optional<UiPatch> StubApplicationBridge::Dispatch(const UiEvent& event) {
    if (!ValidEventEnvelope(event)) return std::nullopt;
    auto patch = actions_.Dispatch(event);
    if (patch) {
        patch->target = event.source;
        patch->config_generation = event.config_generation;
        patch->generation = ++generation_;
    }
    return patch;
}

bool StubApplicationBridge::RegisterAction(std::string action,
                                           UiActionRegistry::Handler handler) {
    return actions_.Register(std::move(action), std::move(handler));
}

bool StubApplicationBridge::ReplaceAction(std::string_view action,
                                          UiActionRegistry::Handler handler) {
    return actions_.Replace(action, std::move(handler));
}

void StubApplicationBridge::SetStringValue(std::string binding, std::string value) {
    view_state_.insert_or_assign(CanonicalBinding(binding), std::move(value));
}

void StubApplicationBridge::SetStringItems(std::string binding,
                                           std::vector<std::wstring> items) {
    item_state_.insert_or_assign(CanonicalBinding(binding), std::move(items));
}

std::optional<std::string> StubApplicationBridge::StringValue(
    std::string_view binding) const {
    const auto found = view_state_.find(CanonicalBinding(binding));
    return found == view_state_.end() ? std::nullopt
                                      : std::optional<std::string>(found->second);
}

bool StubApplicationBridge::HasRegisteredAction(std::string_view action) const noexcept {
    return actions_.Contains(action);
}

std::size_t StubApplicationBridge::registered_action_count() const noexcept {
    return actions_.size();
}

void StubApplicationBridge::InitializeDeterministicState() {
    view_state_ = {
        {"viewState.terminalInput", "C:\\Work\\Terminal"},
        {"viewState.selectedTerminalProfile", "PowerShell"},
        {"viewState.confirmBeforeRun", "true"},
        {"viewState.terminalVenvEnabled", "false"},
        {"viewState.selectedRecentFolder", "C:\\Work\\Terminal"},
        {"viewState.terminalStatus", "Stub siap; tidak ada process yang akan dijalankan."},
        {"viewState.injectConfig", "provider=Anthropic Compatible\r\ntarget=Windows\r\nmodel=stub-model"},
        {"viewState.injectBaseUrl", ""},
        {"viewState.injectModel", "stub-model"},
        {"viewState.injectApiKeyDraft", ""},
        {"viewState.selectedInjectProvider", "Anthropic Compatible"},
        {"viewState.selectedInjectApiKey", ""},
        {"viewState.selectedInjectTarget", "Windows"},
        {"viewState.injectStatus", "Konfigurasi contoh belum divalidasi."},
        {"viewState.selectedJsonEditorTarget", "Windows settings.json"},
        {"viewState.jsonEditorDraft", "{\r\n  \"theme\": \"system\",\r\n  \"stub\": true\r\n}"},
        {"viewState.jsonEditorStatus", "Draft contoh dimuat dari memory."},
        {"viewState.selectedChromeProfile", "Personal — Windows"},
        {"viewState.chromeUrl", "https://example.com"},
        {"viewState.selectedChromeBookmark", "Documentation — https://example.com/docs"},
        {"viewState.chromeStatus", "Profile cache contoh siap."},
        {"viewState.selectedManagedProfile", "Personal — Windows"},
        {"viewState.selectedChromeRuntime", "Windows"},
        {"viewState.profileName", "Personal"},
        {"viewState.profileEnabled", "true"},
        {"viewState.profileStatus", "Metadata profile contoh siap."},
        {"viewState.selectedTheme", "System"},
        {"viewState.startupToTray", "false"},
        {"viewState.settingsConfirmBeforeRun", "true"},
        {"viewState.selectedRecentFolder", "C:\\Work\\Terminal"},
        {"viewState.settingsStatus", "Settings stub belum diterapkan."},
        {"viewState.uiEditorDraft", "tokens.accent = #4F8CFF\r\nstyles.button-primary.radius = 6"},
        {"viewState.uiEditorStatus", "Preview masih lokal pada window ini."},
    };

    item_state_ = {
        {"viewState.terminalProfiles", {L"PowerShell Admin", L"PowerShell", L"Ubuntu (WSL)"}},
        {"viewState.recentFolders", {L"C:\\Work\\Terminal", L"C:\\Projects", L"\\\\wsl.localhost\\Ubuntu\\home\\yuzha"}},
        {"viewState.injectProviders", {L"Anthropic Compatible", L"OpenAI Compatible", L"Local Stub"}},
        {"viewState.injectApiKeys", {L"stub-api-key"}},
        {"viewState.injectTargets", {L"Windows", L"Ubuntu (WSL)"}},
        {"viewState.jsonEditorTargets", {L"Windows settings.json", L"Ubuntu settings.json"}},
        {"viewState.chromeProfiles", {L"Personal — Windows", L"Work — Windows", L"Research — Ubuntu (WSL)"}},
        {"viewState.chromeBookmarks", {L"Documentation — https://example.com/docs", L"Dashboard — https://example.com/app", L"Local tools — http://localhost/"}},
        {"viewState.managedChromeProfiles", {L"Personal — Windows", L"Work — Windows", L"Research — Ubuntu (WSL)"}},
        {"viewState.chromeRuntimes", {L"Windows", L"Ubuntu (WSL)"}},
        {"viewState.themeOptions", {L"System", L"Dark", L"Light"}},
        {"viewState.recentFolders", {L"C:\\Work\\Terminal", L"C:\\Projects", L"\\\\wsl.localhost\\Ubuntu\\home\\yuzha"}},
    };
}

void StubApplicationBridge::RegisterPayloadStateAction(
    std::string action, std::string payload_key, std::string binding,
    std::string status_binding, std::string status) {
    const std::string registered_action = action;
    if (!RegisterAction(
            std::move(action),
            [this, payload_key = std::move(payload_key), binding = std::move(binding),
             status_binding = std::move(status_binding), status = std::move(status)](
                const UiEvent& event) -> std::optional<UiPatch> {
                const auto value = PayloadScalar(event, payload_key);
                if (!value) return std::nullopt;
                view_state_[binding] = *value;
                view_state_[status_binding] = status;
                UiPatch patch;
                patch.view_state[binding] = *value;
                patch.view_state[status_binding] = status;
                patch.request_repaint = true;
                return patch;
            })) {
        throw std::logic_error("Duplicate stub payload action: " + registered_action);
    }
}

void StubApplicationBridge::RegisterStatusAction(
    std::string action, std::string status_binding, std::string status,
    std::optional<std::wstring> window_title) {
    const std::string registered_action = action;
    if (!RegisterAction(
            std::move(action),
            [this, status_binding = std::move(status_binding), status = std::move(status),
             window_title = std::move(window_title)](const UiEvent&)
                -> std::optional<UiPatch> {
                view_state_[status_binding] = status;
                UiPatch patch;
                patch.view_state[status_binding] = status;
                patch.window_title = window_title;
                patch.request_repaint = true;
                return patch;
            })) {
        throw std::logic_error("Duplicate stub status action: " + registered_action);
    }
}

void StubApplicationBridge::RegisterTerminalFeature() {
    RegisterPayloadStateAction("update-terminal-draft", "value",
                               "viewState.terminalInput", "viewState.terminalStatus",
                               "Folder draft diperbarui; belum ada process yang dijalankan.");
    RegisterPayloadStateAction("select-terminal-profile", "selectedValue",
                               "viewState.selectedTerminalProfile",
                               "viewState.terminalStatus", "Profile terminal contoh dipilih.");
    RegisterPayloadStateAction("toggle-confirm-before-run", "checked",
                               "viewState.confirmBeforeRun", "viewState.terminalStatus",
                               "Opsi konfirmasi stub diperbarui.");
    RegisterPayloadStateAction("toggle-terminal-venv", "checked",
                               "viewState.terminalVenvEnabled", "viewState.terminalStatus",
                               "Preference virtual environment diperbarui.");
    RegisterStatusAction("activate-terminal-options", "viewState.terminalStatus",
                         "Card opsi Terminal menerima action activate.");
    RegisterPayloadStateAction("select-terminal-folder", "selectedValue",
                               "viewState.selectedRecentFolder",
                               "viewState.terminalStatus", "Recent folder dipilih.");
    RegisterStatusAction("run-terminal", "viewState.terminalStatus",
                         "Stub selesai; tidak ada PowerShell, WSL, atau process lain yang dijalankan.",
                         L"Terminal — stub selesai");
    if (!RegisterAction("run-terminal-confirmed", [this](const UiEvent&) {
            UiPatch patch;
            view_state_["viewState.terminalStatus"] =
                "Stub confirmation completed; no process was launched.";
            patch.view_state["viewState.terminalStatus"] =
                view_state_["viewState.terminalStatus"];
            patch.dialog_request = DialogRequest{DialogRequestAction::Save,
                                                 "run-confirm-dialog"};
            patch.request_repaint = true;
            return std::optional<UiPatch>(std::move(patch));
        })) {
        throw std::logic_error("Duplicate stub action: run-terminal-confirmed");
    }
    if (!RegisterAction("run-terminal-cancel", [](const UiEvent&) {
            UiPatch patch;
            patch.dialog_request = DialogRequest{DialogRequestAction::Cancel,
                                                 "run-confirm-dialog"};
            patch.request_repaint = true;
            return std::optional<UiPatch>(std::move(patch));
        })) {
        throw std::logic_error("Duplicate stub action: run-terminal-cancel");
    }
}

void StubApplicationBridge::RegisterJsonInjectFeature() {
    RegisterPayloadStateAction("select-inject-provider", "selectedValue",
                               "viewState.selectedInjectProvider", "viewState.injectStatus",
                               "Provider contoh dipilih.");
    RegisterPayloadStateAction("select-inject-target", "selectedValue",
                               "viewState.selectedInjectTarget", "viewState.injectStatus",
                               "Target contoh dipilih.");
    RegisterPayloadStateAction("select-inject-api-key", "selectedValue",
                               "viewState.selectedInjectApiKey", "viewState.injectStatus",
                               "API key contoh dipilih.");
    RegisterPayloadStateAction("update-inject-base-url", "value",
                               "viewState.injectBaseUrl", "viewState.injectStatus",
                               "Base URL draft diperbarui.");
    RegisterPayloadStateAction("update-inject-model", "value",
                               "viewState.injectModel", "viewState.injectStatus",
                               "Model draft diperbarui.");
    RegisterPayloadStateAction("update-inject-api-keys", "value",
                               "viewState.injectApiKeyDraft", "viewState.injectStatus",
                               "API key draft diperbarui.");
    RegisterStatusAction("save-inject-provider", "viewState.injectStatus",
                         "Validasi stub lulus; tidak ada file nyata yang dibaca.");
    RegisterStatusAction("apply-inject", "viewState.injectStatus",
                         "Apply stub selesai; tidak ada settings nyata yang ditulis.");
}

void StubApplicationBridge::RegisterJsonEditorFeature() {
    RegisterPayloadStateAction("select-json-editor-target", "selectedValue",
                               "viewState.selectedJsonEditorTarget",
                               "viewState.jsonEditorStatus", "Target editor contoh dipilih.");
    RegisterPayloadStateAction("update-json-editor-draft", "value",
                               "viewState.jsonEditorDraft", "viewState.jsonEditorStatus",
                               "Draft JSON contoh diperbarui.");
    RegisterStatusAction("validate-json-editor", "viewState.jsonEditorStatus",
                         "Validasi JSON stub lulus.");
    RegisterStatusAction("restore-json-editor-backup", "viewState.jsonEditorStatus",
                         "Backup contoh dipulihkan di memory; filesystem tidak disentuh.");
    RegisterStatusAction("save-json-editor", "viewState.jsonEditorStatus",
                         "Save stub selesai; filesystem tidak disentuh.");
}

void StubApplicationBridge::RegisterChromeLauncherFeature() {
    RegisterPayloadStateAction("select-chrome-profile", "selectedValue",
                               "viewState.selectedChromeProfile", "viewState.chromeStatus",
                               "Profile Chrome contoh dipilih.");
    RegisterPayloadStateAction("update-chrome-url", "value", "viewState.chromeUrl",
                               "viewState.chromeStatus", "URL draft diperbarui.");
    RegisterPayloadStateAction("select-chrome-bookmark", "selectedValue",
                               "viewState.selectedChromeBookmark", "viewState.chromeStatus",
                               "Bookmark contoh dipilih.");
    RegisterStatusAction("launch-chrome", "viewState.chromeStatus",
                         "Launch stub selesai; Chrome tidak dijalankan.");
    RegisterPayloadStateAction("select-chrome-runtime", "selectedValue",
                               "viewState.selectedChromeRuntime", "viewState.profileStatus",
                               "Chrome runtime contoh dipilih.");
    RegisterStatusAction("refresh-chrome-profiles", "viewState.profileStatus",
                         "Refresh stub selesai; filesystem tidak dibaca.");
}

void StubApplicationBridge::RegisterChromeProfileManagerFeature() {
    RegisterPayloadStateAction("select-managed-chrome-profile", "selectedValue",
                               "viewState.selectedManagedProfile", "viewState.profileStatus",
                               "Metadata profile contoh dipilih.");
    RegisterPayloadStateAction("toggle-chrome-profile-enabled", "checked",
                               "viewState.profileEnabled", "viewState.profileStatus",
                               "Status profile draft diperbarui.");
    RegisterStatusAction("save-chrome-profiles", "viewState.profileStatus",
                         "Profile stub disimpan di memory; Chrome data tidak disentuh.");
}

void StubApplicationBridge::RegisterSettingsFeature() {
    RegisterPayloadStateAction("select-settings-theme", "selectedValue",
                               "viewState.selectedTheme", "viewState.settingsStatus",
                               "Theme preference stub diperbarui.");
    RegisterPayloadStateAction("toggle-startup-to-tray", "checked",
                               "viewState.startupToTray", "viewState.settingsStatus",
                               "Startup-to-tray stub diperbarui.");
    RegisterPayloadStateAction("toggle-settings-confirm-before-run", "checked",
                               "viewState.settingsConfirmBeforeRun",
                               "viewState.settingsStatus", "Konfirmasi terminal stub diperbarui.");
    RegisterPayloadStateAction("select-recent-folder", "selectedValue",
                               "viewState.selectedRecentFolder", "viewState.settingsStatus",
                               "Recent folder contoh dipilih.");
    RegisterStatusAction("apply-settings", "viewState.settingsStatus",
                         "Settings stub diterapkan di memory; tidak ada persistence.");
}

void StubApplicationBridge::RegisterUiEditorFeature() {
    RegisterPayloadStateAction("update-ui-editor-draft", "value",
                               "viewState.uiEditorDraft", "viewState.uiEditorStatus",
                               "Draft token UI diperbarui.");
    RegisterStatusAction("preview-ui-editor-stub", "viewState.uiEditorStatus",
                         "Preview stub dirender tanpa mengganti config generation.");
    RegisterStatusAction("apply-ui-editor-stub", "viewState.uiEditorStatus",
                         "Apply stub selesai; override file tidak ditulis.");
    RegisterStatusAction("reload-ui-editor-stub", "viewState.uiEditorStatus",
                         "Reload stub selesai dari deterministic in-memory document.");
    RegisterStatusAction("rollback-ui-editor-stub", "viewState.uiEditorStatus",
                         "Rollback stub selesai; config aktif tidak berubah.");
}

void StubApplicationBridge::RegisterDialogFeature() {
    const auto register_dialog = [this](const char* action, DialogRequestAction request_action) {
        if (!RegisterAction(action, [request_action](const UiEvent& event) {
                UiPatch patch;
                patch.dialog_request = DialogRequest{request_action, "save-discard-dialog"};
                if (request_action == DialogRequestAction::Save) {
                    patch.close_save_result =
                        CloseSaveResult{event.source, event.config_generation, true};
                }
                patch.request_repaint = true;
                return std::optional<UiPatch>(std::move(patch));
            })) {
            throw std::logic_error("Duplicate dialog action registration.");
        }
    };
    register_dialog("dialog-save", DialogRequestAction::Save);
    register_dialog("dialog-discard", DialogRequestAction::Discard);
    register_dialog("dialog-cancel", DialogRequestAction::Cancel);

    const auto record_result = [this](const UiEvent& event) -> std::optional<UiPatch> {
        view_state_["lastDialogAction"] = event.action;
        UiPatch patch;
        patch.view_state["lastDialogAction"] = event.action;
        return patch;
    };
    for (const char* action : {"save-confirmation-accepted",
                               "save-confirmation-cancelled",
                               "save-confirmation-dismissed",
                               "run-confirmation-accepted",
                               "run-confirmation-cancelled",
                               "run-confirmation-dismissed"}) {
        if (!RegisterAction(action, record_result)) {
            throw std::logic_error("Duplicate dialog result action registration.");
        }
    }
}

void StubApplicationBridge::RegisterNavigationFeature() {
    if (!RegisterAction("navigate-route", [](const UiEvent& event) -> std::optional<UiPatch> {
            const auto route = PayloadString(event, "routeId");
            if (!route) return std::nullopt;
            UiPatch patch;
            patch.route_id = *route;
            patch.request_repaint = true;
            return patch;
        })) {
        throw std::logic_error("Duplicate navigation action registration.");
    }
}

const std::map<std::string, std::string, std::less<>>& StubApplicationBridge::view_state() const noexcept {
    return view_state_;
}

std::vector<std::wstring> StubApplicationBridge::ResolveStringItems(
    std::string_view binding) const {
    const auto found = item_state_.find(CanonicalBinding(binding));
    return found == item_state_.end() ? std::vector<std::wstring>{} : found->second;
}

std::optional<std::wstring> StubApplicationBridge::ResolveStringValue(
    std::string_view binding) const {
    const auto found = view_state_.find(CanonicalBinding(binding));
    if (found == view_state_.end()) return std::nullopt;
    return std::wstring(found->second.begin(), found->second.end());
}

}  // namespace ui::application
