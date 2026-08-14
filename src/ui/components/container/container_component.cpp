#include "ui/components/container/container_component.h"

#include <algorithm>

namespace ui::components {
namespace {

const config::ContainerProperties& Properties(const config::ResolvedComponent& definition) {
    return std::get<config::ContainerProperties>(definition.properties);
}

}  // namespace

MeasuredSize ContainerComponent::Measure(HDC dc, int available_width, int available_height) {
    const config::ContainerProperties& properties = Properties(definition_);
    const int horizontal_padding = ScaleDip(properties.padding.left + properties.padding.right, host_.dpi);
    const int vertical_padding = ScaleDip(properties.padding.top + properties.padding.bottom, host_.dpi);
    const int inner_width = std::max(0, available_width - horizontal_padding);
    const int inner_height = std::max(0, available_height - vertical_padding);
    const int gap = ScaleDip(properties.gap, host_.dpi);
    int width = 0;
    int height = 0;

    for (std::size_t index = 0; index < children_.size(); ++index) {
        const MeasuredSize child = children_[index]->Measure(dc, inner_width, inner_height);
        if (properties.direction == config::ContainerDirection::Row) {
            width += child.width;
            height = std::max(height, child.height);
        } else {
            width = std::max(width, child.width);
            height += child.height;
        }
        if (index > 0) {
            if (properties.direction == config::ContainerDirection::Row) width += gap;
            else height += gap;
        }
    }
    return ApplyConstraints({width + horizontal_padding, height + vertical_padding}, available_width,
                            available_height);
}

void ContainerComponent::Arrange(const RECT& bounds) {
    Component::Arrange(bounds);
    const config::ContainerProperties& properties = Properties(definition_);
    RECT inner{bounds.left + ScaleDip(properties.padding.left, host_.dpi),
               bounds.top + ScaleDip(properties.padding.top, host_.dpi),
               bounds.right - ScaleDip(properties.padding.right, host_.dpi),
               bounds.bottom - ScaleDip(properties.padding.bottom, host_.dpi)};
    const int gap = ScaleDip(properties.gap, host_.dpi);
    const bool horizontal = properties.direction == config::ContainerDirection::Row;
    const int available_width = std::max(0L, inner.right - inner.left);
    const int available_height = std::max(0L, inner.bottom - inner.top);
    HDC dc = host_.layout_dc;
    const bool release_dc = dc == nullptr;
    if (!dc) dc = GetDC(host_.window);
    int cursor = horizontal ? inner.left : inner.top;

    int fixed = 0;
    int fill_count = 0;
    for (const auto& child : children_) {
        const MeasuredSize measured = child->Measure(dc, available_width, available_height);
        const config::Dimension& dimension = horizontal ? child->definition().layout.width
                                                        : child->definition().layout.height;
        if (dimension.kind == config::DimensionKind::Fill) ++fill_count;
        else fixed += horizontal ? measured.width : measured.height;
    }
    if (!children_.empty()) fixed += gap * static_cast<int>(children_.size() - 1);
    const int primary_available = horizontal ? available_width : available_height;
    const int fill_size = fill_count > 0 ? std::max(0, primary_available - fixed) / fill_count : 0;

    for (const auto& child : children_) {
        const MeasuredSize measured = child->Measure(dc, available_width, available_height);
        const config::LayoutDefinition& layout = child->definition().layout;
        const bool fill_primary = (horizontal ? layout.width : layout.height).kind ==
                                  config::DimensionKind::Fill;
        const int primary = fill_primary ? fill_size : (horizontal ? measured.width : measured.height);
        const bool fill_cross = (horizontal ? layout.height : layout.width).kind ==
                                config::DimensionKind::Fill;
        const int cross = fill_cross ? (horizontal ? available_height : available_width)
                                     : (horizontal ? measured.height : measured.width);
        RECT child_bounds{};
        if (horizontal) {
            child_bounds = {cursor, inner.top, cursor + primary, inner.top + cross};
        } else {
            child_bounds = {inner.left, cursor, inner.left + cross, cursor + primary};
        }
        child->Arrange(child_bounds);
        cursor += primary + gap;
    }
    if (dc && release_dc) ReleaseDC(host_.window, dc);
}

void ContainerComponent::Paint(HDC dc) {
    PaintStyleBox(dc, config::VisualState::Normal, bounds_);
    const config::ContainerProperties& properties = Properties(definition_);
    const int saved = SaveDC(dc);
    if (properties.overflow == config::OverflowMode::Clip) IntersectClipRect(dc, bounds_.left, bounds_.top, bounds_.right, bounds_.bottom);
    PaintChildren(dc);
    if (saved != 0) RestoreDC(dc, saved);
}

}  // namespace ui::components
