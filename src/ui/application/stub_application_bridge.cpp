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
    return {};
}

std::optional<std::wstring> StubApplicationBridge::ResolveStringValue(
    std::string_view binding) const {
    const auto found = view_state_.find(std::string(binding));
    if (found == view_state_.end()) return std::nullopt;
    return std::wstring(found->second.begin(), found->second.end());
}

}  // namespace ui::application
