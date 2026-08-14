#include "ui/components/card/card_component.h"

#include <algorithm>

namespace ui::components {
namespace {

const config::CardProperties& Properties(const config::ResolvedComponent& definition) {
    return std::get<config::CardProperties>(definition.properties);
}

bool InitialSelected(const config::BooleanValue& selected) {
    const auto* literal = std::get_if<bool>(&selected);
    return literal && *literal;
}

}  // namespace

CardComponent::CardComponent(const config::ResolvedComponent& definition, ComponentHost& host)
    : Component(definition, host), selected_(InitialSelected(Properties(definition).selected)) {}

MeasuredSize CardComponent::Measure(HDC dc, int available_width, int available_height) {
    const int horizontal_padding = ScaleDip(
        style().content_padding.left + style().content_padding.right, host_.dpi);
    const int vertical_padding = ScaleDip(
        style().content_padding.top + style().content_padding.bottom, host_.dpi);
    const int inner_width = std::max(0, available_width - horizontal_padding);
    const int inner_height = std::max(0, available_height - vertical_padding);
    const int gap = ScaleDip(8, host_.dpi);
    int width = 0;
    int height = 0;
    for (std::size_t index = 0; index < children_.size(); ++index) {
        const MeasuredSize measured = children_[index]->Measure(dc, inner_width, inner_height);
        width = std::max(width, measured.width);
        height += measured.height;
        if (index > 0) height += gap;
    }
    return ApplyConstraints({width + horizontal_padding, height + vertical_padding}, available_width,
                            available_height);
}

void CardComponent::Arrange(const RECT& bounds) {
    Component::Arrange(bounds);
    const int left = bounds.left + ScaleDip(style().content_padding.left, host_.dpi);
    const int right = bounds.right - ScaleDip(style().content_padding.right, host_.dpi);
    int cursor = bounds.top + ScaleDip(style().content_padding.top, host_.dpi);
    const int available_width = std::max(0, right - left);
    const int available_height = std::max(
        0, static_cast<int>(bounds.bottom - cursor) -
               ScaleDip(style().content_padding.bottom, host_.dpi));
    const int gap = ScaleDip(8, host_.dpi);
    HDC dc = host_.layout_dc;
    const bool release_dc = dc == nullptr;
    if (!dc) dc = GetDC(host_.window);
    for (const auto& child : children_) {
        const MeasuredSize measured = child->Measure(dc, available_width, available_height);
        const int width = child->definition().layout.width.kind == config::DimensionKind::Fill
                              ? available_width
                              : measured.width;
        child->Arrange({left, cursor, left + width, cursor + measured.height});
        cursor += measured.height + gap;
    }
    if (dc && release_dc) ReleaseDC(host_.window, dc);
}

void CardComponent::Paint(HDC dc) {
    PaintStyleBox(dc, State(), bounds_);
    PaintChildren(dc);
}

bool CardComponent::PointerMove(POINT point) {
    if (!Properties(definition_).interactive) return false;
    const bool hovered = enabled() && PointInRectInclusive(bounds_, point);
    if (hovered == hovered_) return false;
    hovered_ = hovered;
    Invalidate();
    return true;
}

bool CardComponent::PointerDown(POINT point) {
    if (!Properties(definition_).interactive || !enabled() ||
        !PointInRectInclusive(bounds_, point)) return false;
    pressed_ = true;
    SetCapture(host_.window);
    Invalidate();
    return true;
}

bool CardComponent::PointerUp(POINT point) {
    if (!pressed_) return false;
    const bool activate = enabled() && PointInRectInclusive(bounds_, point);
    pressed_ = false;
    if (GetCapture() == host_.window) ReleaseCapture();
    Invalidate();
    if (activate) Activate();
    return true;
}

bool CardComponent::CanFocus() const noexcept {
    const auto& properties = Properties(definition_);
    return visible() && enabled() && properties.interactive && properties.tab_stop;
}

bool CardComponent::FocusNativePeer() {
    SetFocus(host_.window);
    return GetFocus() == host_.window;
}

void CardComponent::SetLogicalFocus(bool focused, bool window_active) {
    if (focused_ == focused && window_active_ == window_active) return;
    focused_ = focused;
    window_active_ = window_active;
    Invalidate();
}

bool CardComponent::HandleKeyDown(UINT virtual_key) {
    if (!focused_ || !window_active_ || !enabled() ||
        (virtual_key != VK_RETURN && virtual_key != VK_SPACE)) return false;
    Activate();
    return true;
}

AutomationRole CardComponent::automation_role() const noexcept {
    return Properties(definition_).interactive ? AutomationRole::Group : AutomationRole::None;
}

bool CardComponent::automation_supports_invoke() const noexcept {
    return Properties(definition_).interactive;
}

bool CardComponent::AutomationInvoke() {
    if (!enabled() || !Properties(definition_).interactive) return false;
    Activate();
    return true;
}

config::VisualState CardComponent::State() const noexcept {
    if (!enabled()) return config::VisualState::Disabled;
    if (pressed_) return config::VisualState::Pressed;
    if (focused_ && window_active_) return config::VisualState::Focus;
    if (hovered_) return config::VisualState::Hover;
    if (selected_) return config::VisualState::Selected;
    return config::VisualState::Normal;
}

void CardComponent::Activate() {
    EmitEvent("activate");
}

}  // namespace ui::components
