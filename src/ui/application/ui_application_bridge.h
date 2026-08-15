#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ui/config/resolved_ui_document.h"

namespace ui::application {

struct UiAddress {
    std::uint64_t window_instance_id = 0;
    std::uint64_t screen_instance_id = 0;
    std::uint64_t component_instance_id = 0;
    std::string window_id;
    std::string route_id;
    std::string component_id;

    bool operator==(const UiAddress&) const = default;
};

struct UiEvent {
    UiAddress source;
    std::string event_type;
    std::string action;
    config::EventPayloadValue::Object payload;
    std::uint64_t config_generation = 0;

    UiEvent() = default;
    UiEvent(std::string action_value, config::EventPayloadValue::Object payload_value)
        : action(std::move(action_value)), payload(std::move(payload_value)) {}
};

enum class DialogRequestAction { Open, Save, Discard, Cancel };

struct DialogRequest {
    DialogRequestAction action = DialogRequestAction::Open;
    std::string dialog_id;
};

struct CloseSaveResult {
    UiAddress source;
    std::uint64_t config_generation = 0;
    bool success = false;
};

struct UiPatch {
    UiAddress target;
    std::uint64_t config_generation = 0;
    std::uint64_t generation = 0;
    std::map<std::string, std::string, std::less<>> view_state;
    std::optional<std::wstring> window_title;
    std::optional<std::string> route_id;
    std::optional<DialogRequest> dialog_request;
    std::optional<CloseSaveResult> close_save_result;
    bool request_repaint = false;
};

class UiApplicationBridge {
public:
    virtual ~UiApplicationBridge() = default;

    virtual std::optional<UiPatch> Dispatch(const UiEvent& event) = 0;
    virtual std::vector<std::wstring> ResolveStringItems(std::string_view binding) const = 0;
    virtual std::optional<std::wstring> ResolveStringValue(
        std::string_view binding) const = 0;
};

}  // namespace ui::application
