#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "ui/config/resolved_ui_document.h"

namespace ui::application {

struct UiEvent {
    std::string action;
    config::EventPayloadValue::Object payload;
};

struct UiPatch {
    std::uint64_t generation = 0;
    std::map<std::string, std::string, std::less<>> view_state;
    std::optional<std::wstring> window_title;
    bool request_repaint = false;
};

class StubApplicationBridge final {
public:
    std::optional<UiPatch> Dispatch(const UiEvent& event);
    const std::map<std::string, std::string, std::less<>>& view_state() const noexcept;

private:
    std::map<std::string, std::string, std::less<>> view_state_;
    std::uint64_t generation_ = 0;
};

}  // namespace ui::application
