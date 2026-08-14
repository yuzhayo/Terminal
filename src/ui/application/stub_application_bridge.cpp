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

}  // namespace ui::application
