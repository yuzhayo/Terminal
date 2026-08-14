#include "ui/components/screen/screen_component.h"

#include <algorithm>

namespace ui::components {

MeasuredSize ScreenComponent::Measure(HDC dc, int available_width, int available_height) {
    int width = 0;
    int height = 0;
    for (const auto& child : children_) {
        const MeasuredSize measured = child->Measure(dc, available_width, available_height);
        width = std::max(width, measured.width);
        height += measured.height;
    }
    return ApplyConstraints({width, height}, available_width, available_height);
}

void ScreenComponent::Arrange(const RECT& bounds) {
    Component::Arrange(bounds);
    HDC dc = host_.layout_dc;
    const bool release_dc = dc == nullptr;
    if (!dc) dc = GetDC(host_.window);
    int cursor = bounds.top;
    const int width = std::max(0L, bounds.right - bounds.left);
    const int height = std::max(0L, bounds.bottom - bounds.top);
    for (const auto& child : children_) {
        const MeasuredSize measured = child->Measure(dc, width, height);
        const int child_height = child->definition().layout.height.kind == config::DimensionKind::Fill
                                     ? std::max(0, static_cast<int>(bounds.bottom - cursor))
                                     : measured.height;
        child->Arrange({bounds.left, cursor, bounds.right, cursor + child_height});
        cursor += child_height;
    }
    if (dc && release_dc) ReleaseDC(host_.window, dc);
}

void ScreenComponent::Paint(HDC dc) {
    PaintStyleBox(dc, config::VisualState::Normal, bounds_);
    PaintChildren(dc);
}

}  // namespace ui::components
