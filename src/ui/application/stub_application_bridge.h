#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/config/resolved_ui_document.h"

namespace ui::application {

struct UiEvent {
    std::string action;
    config::EventPayloadValue::Object payload;
};

enum class DialogRequestAction { Open, Save, Discard, Cancel };

struct DialogRequest {
    DialogRequestAction action = DialogRequestAction::Open;
    std::string dialog_id;
};

struct UiPatch {
    std::uint64_t generation = 0;
    std::map<std::string, std::string, std::less<>> view_state;
    std::optional<std::wstring> window_title;
    std::optional<DialogRequest> dialog_request;
    bool request_repaint = false;
};

class StubApplicationBridge final {
public:
    std::optional<UiPatch> Dispatch(const UiEvent& event);
    const std::map<std::string, std::string, std::less<>>& view_state() const noexcept;
    std::vector<std::wstring> ResolveStringItems(std::string_view binding) const;
    std::optional<std::wstring> ResolveStringValue(std::string_view binding) const;

private:
    std::map<std::string, std::string, std::less<>> view_state_;
    std::uint64_t generation_ = 0;
};

}  // namespace ui::application
