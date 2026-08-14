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

}  // namespace

StubApplicationBridge::StubApplicationBridge() {
    RegisterTerminalFeature();
    RegisterDialogFeature();
    RegisterNavigationFeature();
}

std::optional<UiPatch> StubApplicationBridge::Dispatch(const UiEvent& event) {
    auto patch = actions_.Dispatch(event);
    if (patch) patch->generation = ++generation_;
    return patch;
}

bool StubApplicationBridge::RegisterAction(std::string action,
                                           UiActionRegistry::Handler handler) {
    return actions_.Register(std::move(action), std::move(handler));
}

std::size_t StubApplicationBridge::registered_action_count() const noexcept {
    return actions_.size();
}

void StubApplicationBridge::RegisterTerminalFeature() {
    const auto record_action = [this](const UiEvent& event) -> std::optional<UiPatch> {
        view_state_["lastAction"] = event.action;
        UiPatch patch;
        patch.view_state["lastAction"] = event.action;
        return patch;
    };
    for (const char* action : {"update-terminal-draft", "select-terminal-profile",
                               "toggle-confirm-before-run", "toggle-terminal-active",
                               "activate-terminal-options", "select-terminal-session",
                               "activate-terminal-session"}) {
        if (!RegisterAction(action, record_action)) {
            throw std::logic_error("Duplicate terminal action registration.");
        }
    }
    if (!RegisterAction("run-terminal-stub", [this](const UiEvent&) {
            view_state_["stubStatus"] = "completed";
            UiPatch patch;
            patch.view_state["stubStatus"] = "completed";
            patch.window_title = L"Terminal — stub selesai";
            patch.request_repaint = true;
            return std::optional<UiPatch>(std::move(patch));
        })) {
        throw std::logic_error("Duplicate terminal run action registration.");
    }
}

void StubApplicationBridge::RegisterDialogFeature() {
    const auto register_dialog = [this](const char* action, DialogRequestAction request_action) {
        if (!RegisterAction(action, [request_action](const UiEvent&) {
                UiPatch patch;
                patch.dialog_request = DialogRequest{request_action, "save-discard-dialog"};
                patch.request_repaint = true;
                return std::optional<UiPatch>(std::move(patch));
            })) {
            throw std::logic_error("Duplicate dialog action registration.");
        }
    };
    register_dialog("open-save-discard-dialog", DialogRequestAction::Open);
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
                               "save-confirmation-dismissed"}) {
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
    if (binding == "terminalProfiles" || binding == "items" ||
        binding == "viewState.terminalProfiles" || binding == "viewState.items") {
        return {L"PowerShell", L"Command Prompt", L"Ubuntu (WSL)"};
    }
    if (binding == "terminalSessions" || binding == "viewState.terminalSessions") {
        std::vector<std::wstring> sessions;
        sessions.reserve(80);
        for (int index = 1; index <= 80; ++index) {
            std::wstring label = L"Session ";
            if (index < 10) label += L"0";
            label += std::to_wstring(index);
            label += index % 3 == 0 ? L" - Ubuntu (WSL)"
                     : index % 2 == 0 ? L" - Command Prompt"
                                      : L" - PowerShell";
            sessions.push_back(std::move(label));
        }
        return sessions;
    }
    return {};
}

std::optional<std::wstring> StubApplicationBridge::ResolveStringValue(
    std::string_view binding) const {
    const auto found = view_state_.find(std::string(binding));
    if (found == view_state_.end()) return std::nullopt;
    return std::wstring(found->second.begin(), found->second.end());
}

}  // namespace ui::application
