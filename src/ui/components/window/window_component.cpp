#include "ui/components/window/window_component.h"

namespace ui::components {

MeasuredSize WindowComponent::Measure(HDC dc, int available_width, int available_height) {
    for (const auto& child : children_) child->Measure(dc, available_width, available_height);
    return ApplyConstraints({available_width, available_height}, available_width, available_height);
}

void WindowComponent::Arrange(const RECT& bounds) {
    Component::Arrange(bounds);
    for (const auto& child : children_) child->Arrange(bounds);
}

void WindowComponent::Paint(HDC dc) {
    PaintStyleBox(dc, config::VisualState::Normal, bounds_);
    PaintChildren(dc);
}

}  // namespace ui::components
