#include "ui/application/stub_application_bridge.h"

namespace ui::application {

std::optional<UiPatch> StubApplicationBridge::Dispatch(const UiEvent& event) {
    UiPatch patch;
    patch.generation = ++generation_;
    if (event.action == "update-terminal-draft") {
        view_state_["lastAction"] = event.action;
        patch.view_state["lastAction"] = event.action;
        return patch;
    }
    if (event.action == "run-terminal-stub") {
        view_state_["stubStatus"] = "completed";
        patch.view_state["stubStatus"] = "completed";
        patch.window_title = L"Terminal — stub selesai";
        patch.request_repaint = true;
        return patch;
    }
    if (event.action == "open-save-discard-dialog") {
        patch.dialog_request = DialogRequest{DialogRequestAction::Open, "save-discard-dialog"};
        patch.request_repaint = true;
        return patch;
    }
    if (event.action == "dialog-save") {
        patch.dialog_request = DialogRequest{DialogRequestAction::Save, "save-discard-dialog"};
        patch.request_repaint = true;
        return patch;
    }
    if (event.action == "dialog-discard") {
        patch.dialog_request = DialogRequest{DialogRequestAction::Discard, "save-discard-dialog"};
        patch.request_repaint = true;
        return patch;
    }
    if (event.action == "dialog-cancel") {
        patch.dialog_request = DialogRequest{DialogRequestAction::Cancel, "save-discard-dialog"};
        patch.request_repaint = true;
        return patch;
    }
    return std::nullopt;
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
