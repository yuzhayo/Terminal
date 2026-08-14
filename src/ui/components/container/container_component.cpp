#include "ui/components/container/container_component.h"

#include <algorithm>

namespace ui::components {
namespace {

const config::ContainerProperties& Properties(const config::ResolvedComponent& definition) {
    return std::get<config::ContainerProperties>(definition.properties);
}

}  // namespace

ContainerComponent::ContainerComponent(const config::ResolvedComponent& definition,
                                       ComponentHost& host)
    : Component(definition, host) {
    if (Properties(definition_).overflow == config::OverflowMode::Scroll) EnsureScrollbar();
}

void ContainerComponent::EnsureScrollbar() {
    if (scrollbar_) return;
    scrollbar_definition_.id = definition_.id + "-scrollbar";
    scrollbar_definition_.type = config::ComponentType::Scrollbar;
    scrollbar_definition_.visible = true;
    scrollbar_definition_.enabled = definition_.enabled;
    scrollbar_definition_.style_id = "scrollbar";
    scrollbar_definition_.style_index = definition_.style_index;
    for (std::size_t index = 0; index < host_.theme->styles.size(); ++index) {
        if (host_.theme->styles[index].id == "scrollbar") {
            scrollbar_definition_.style_index = index;
            break;
        }
    }
    config::ScrollbarProperties properties;
    properties.orientation = Properties(definition_).direction == config::ContainerDirection::Row
                                 ? config::Orientation::Horizontal
                                 : config::Orientation::Vertical;
    properties.line_step = 24;
    scrollbar_definition_.properties = properties;
    scrollbar_ = std::make_unique<ScrollbarComponent>(scrollbar_definition_, host_);
    scrollbar_->Bind(this);
}

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
    int available_width = std::max(0, static_cast<int>(inner.right - inner.left));
    int available_height = std::max(0, static_cast<int>(inner.bottom - inner.top));
    HDC dc = host_.layout_dc;
    const bool release_dc = dc == nullptr;
    if (!dc) dc = GetDC(host_.window);
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
    int primary_available = horizontal ? available_width : available_height;
    content_extent_ = fixed;
    viewport_extent_ = primary_available;
    scrollbar_visible_ = scrollbar_ && content_extent_ > viewport_extent_;
    if (scrollbar_visible_) {
        const int thickness = ScaleDip(
            std::get<config::ScrollbarProperties>(scrollbar_definition_.properties).thickness,
            host_.dpi);
        if (horizontal) available_height = std::max(0, available_height - thickness);
        else available_width = std::max(0, available_width - thickness);
        primary_available = horizontal ? available_width : available_height;
        viewport_extent_ = primary_available;
    }
    scroll_value_ = std::clamp(scroll_value_, 0, std::max(0, content_extent_ - viewport_extent_));
    viewport_ = {inner.left, inner.top, inner.left + available_width, inner.top + available_height};
    int cursor = (horizontal ? inner.left : inner.top) - scroll_value_;
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
    if (scrollbar_visible_) {
        if (horizontal) {
            scrollbar_->Arrange({inner.left, viewport_.bottom, inner.right, inner.bottom});
        } else {
            scrollbar_->Arrange({viewport_.right, inner.top, inner.right, inner.bottom});
        }
    }
    if (dc && release_dc) ReleaseDC(host_.window, dc);
}

void ContainerComponent::Paint(HDC dc) {
    PaintStyleBox(dc, config::VisualState::Normal, bounds_);
    const config::ContainerProperties& properties = Properties(definition_);
    const int saved = SaveDC(dc);
    if (properties.overflow == config::OverflowMode::Clip ||
        properties.overflow == config::OverflowMode::Scroll) {
        const RECT clip = properties.overflow == config::OverflowMode::Scroll ? viewport_ : bounds_;
        IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    }
    PaintChildren(dc);
    if (saved != 0) RestoreDC(dc, saved);
    if (scrollbar_visible_) scrollbar_->Paint(dc);
}

Component* ContainerComponent::HitTest(POINT point) {
    if (!visible() || !PointInRectInclusive(bounds_, point)) return nullptr;
    if (scrollbar_visible_ && scrollbar_->HitTest(point)) return scrollbar_.get();
    if (Properties(definition_).overflow == config::OverflowMode::Scroll &&
        !PointInRectInclusive(viewport_, point)) return this;
    for (auto item = children_.rbegin(); item != children_.rend(); ++item) {
        if (Component* hit = (*item)->HitTest(point)) return hit;
    }
    return this;
}

bool ContainerComponent::PointerWheel(int delta) {
    return scrollbar_visible_ && scrollbar_->PointerWheel(delta);
}

void ContainerComponent::CollectFocusable(std::vector<Component*>& focusable) {
    Component::CollectFocusable(focusable);
    if (scrollbar_) focusable.push_back(scrollbar_.get());
}

void ContainerComponent::CollectAutomationElements(std::vector<Component*>& elements) {
    Component::CollectAutomationElements(elements);
    if (scrollbar_) scrollbar_->CollectAutomationElements(elements);
}

void ContainerComponent::OnDpiChanged() {
    Component::OnDpiChanged();
    if (scrollbar_) scrollbar_->OnDpiChanged();
}

bool ContainerComponent::PrepareResources(COLORREF parent_background) {
    if (!Component::PrepareResources(parent_background)) return false;
    return !scrollbar_ || scrollbar_->PrepareResources(parent_background);
}

int ContainerComponent::ScrollMinimum() const noexcept { return 0; }
int ContainerComponent::ScrollMaximum() const noexcept {
    return std::max(0, content_extent_ - viewport_extent_);
}
int ContainerComponent::ScrollPageSize() const noexcept { return viewport_extent_; }
int ContainerComponent::ScrollValue() const noexcept { return scroll_value_; }

void ContainerComponent::SetScrollValue(int value) {
    const int clamped = std::clamp(value, ScrollMinimum(), ScrollMaximum());
    if (clamped == scroll_value_) return;
    scroll_value_ = clamped;
    Arrange(bounds_);
    Invalidate();
}

}  // namespace ui::components
