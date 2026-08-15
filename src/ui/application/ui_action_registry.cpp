#include "ui/application/ui_action_registry.h"

namespace ui::application {

namespace {

bool IsActionId(std::string_view action) noexcept {
    if (action.empty() || action.size() > 128 || action.front() == '-' ||
        action.back() == '-') {
        return false;
    }
    bool previous_hyphen = false;
    for (const unsigned char character : action) {
        const bool hyphen = character == '-';
        if (!(character >= 'a' && character <= 'z') &&
            !(character >= '0' && character <= '9') && !hyphen) {
            return false;
        }
        if (hyphen && previous_hyphen) return false;
        previous_hyphen = hyphen;
    }
    return true;
}

}  // namespace

bool UiActionRegistry::Register(std::string action, Handler handler) {
    if (!IsActionId(action) || !handler) return false;
    return handlers_.emplace(std::move(action), std::move(handler)).second;
}

bool UiActionRegistry::Replace(std::string_view action, Handler handler) {
    if (!handler) return false;
    const auto found = handlers_.find(action);
    if (found == handlers_.end()) return false;
    found->second = std::move(handler);
    return true;
}

bool UiActionRegistry::Contains(std::string_view action) const noexcept {
    return handlers_.contains(action);
}

std::optional<UiPatch> UiActionRegistry::Dispatch(const UiEvent& event) const {
    const auto found = handlers_.find(event.action);
    return found == handlers_.end() ? std::nullopt : found->second(event);
}

std::size_t UiActionRegistry::size() const noexcept {
    return handlers_.size();
}

}  // namespace ui::application
