#include "ui/containers/logical_focus_coordinator.h"

#include <algorithm>

#include "ui/components/component.h"

namespace ui::containers {

void LogicalFocusCoordinator::Rebuild(components::Component& root) {
    components::Component* previous = focused_;
    focusable_.clear();
    root.CollectFocusable(focusable_);
    focused_ = Contains(previous) ? previous : nullptr;
}

void LogicalFocusCoordinator::Clear() noexcept {
    focusable_.clear();
    focused_ = nullptr;
    synchronizing_ = false;
}

bool LogicalFocusCoordinator::RequestFocus(components::Component* target, bool synchronize_native) {
    if (!target || !Contains(target) || !target->CanFocus() || synchronizing_) return false;
    if (focused_ == target) {
        target->SetLogicalFocus(true, window_active_);
        return !synchronize_native || target->FocusNativePeer();
    }
    synchronizing_ = true;
    if (focused_) focused_->SetLogicalFocus(false, window_active_);
    focused_ = target;
    focused_->SetLogicalFocus(true, window_active_);
    const bool native_result = !synchronize_native || focused_->FocusNativePeer();
    synchronizing_ = false;
    return native_result;
}

bool LogicalFocusCoordinator::Move(bool reverse) {
    if (focusable_.empty()) return false;
    const auto current = std::find(focusable_.begin(), focusable_.end(), focused_);
    std::size_t index = current == focusable_.end()
                            ? (reverse ? 0 : focusable_.size() - 1)
                            : static_cast<std::size_t>(current - focusable_.begin());
    for (std::size_t attempt = 0; attempt < focusable_.size(); ++attempt) {
        index = reverse ? (index + focusable_.size() - 1) % focusable_.size()
                        : (index + 1) % focusable_.size();
        components::Component* candidate = focusable_[index];
        if (candidate->CanFocus()) return RequestFocus(candidate);
    }
    return false;
}

void LogicalFocusCoordinator::NotifyNativeFocus(components::Component* target, bool focused) {
    if (synchronizing_ || !target || !Contains(target)) return;
    if (focused) {
        RequestFocus(target, false);
    } else if (focused_ == target) {
        target->SetLogicalFocus(true, false);
    }
}

void LogicalFocusCoordinator::SetWindowActive(bool active) {
    window_active_ = active;
    if (!focused_) return;
    focused_->SetLogicalFocus(true, active);
    if (active && !synchronizing_) {
        synchronizing_ = true;
        focused_->FocusNativePeer();
        synchronizing_ = false;
    }
}

bool LogicalFocusCoordinator::HandleKeyDown(unsigned int virtual_key) {
    return focused_ && focused_->HandleKeyDown(virtual_key);
}

components::Component* LogicalFocusCoordinator::focused() const noexcept {
    return focused_;
}

std::size_t LogicalFocusCoordinator::focusable_count() const noexcept {
    return focusable_.size();
}

bool LogicalFocusCoordinator::Contains(const components::Component* target) const noexcept {
    return std::find(focusable_.begin(), focusable_.end(), target) != focusable_.end();
}

}  // namespace ui::containers
