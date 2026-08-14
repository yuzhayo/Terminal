#include "ui/components/button/button_component.h"

#include <algorithm>

namespace ui::components {
namespace {

const config::ButtonProperties& Properties(const config::ResolvedComponent& definition) {
    return std::get<config::ButtonProperties>(definition.properties);
}

}  // namespace

MeasuredSize ButtonComponent::Measure(HDC dc, int available_width, int available_height) {
    const std::wstring label = ResolveText(Properties(definition_).label);
    HFONT font = host_.render_runtime->Font(style().font, host_.dpi);
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    SIZE text_size{};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &text_size);
    if (previous) SelectObject(dc, previous);
    const int width = text_size.cx + ScaleDip(style().content_padding.left +
                                              style().content_padding.right, host_.dpi);
    const int text_height = text_size.cy + ScaleDip(style().content_padding.top +
                                                    style().content_padding.bottom, host_.dpi);
    const int height = std::max(ScaleDip(style().minimum_height, host_.dpi), text_height);
    return ApplyConstraints({width, height}, available_width, available_height);
}

void ButtonComponent::Paint(HDC dc) {
    PaintStyleBox(dc, State(), bounds_);
    const std::wstring label = ResolveText(Properties(definition_).label);
    const config::ResolvedVisualState& visual = style().states[static_cast<std::size_t>(State())];
    const rendering::RgbaColor foreground = host_.render_runtime->ResolveColor(visual.foreground);
    HFONT font = host_.render_runtime->Font(style().font, host_.dpi);
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, rendering::ToColorRef(foreground));
    RECT text_bounds = bounds_;
    DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &text_bounds,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previous) SelectObject(dc, previous);
}

bool ButtonComponent::PointerMove(POINT point) {
    const bool hovered = enabled() && PointInRectInclusive(bounds_, point);
    if (hovered == hovered_) return false;
    hovered_ = hovered;
    Invalidate();
    return true;
}

bool ButtonComponent::PointerDown(POINT point) {
    if (!enabled() || !PointInRectInclusive(bounds_, point)) return false;
    pressed_ = true;
    SetCapture(host_.window);
    Invalidate();
    return true;
}

bool ButtonComponent::PointerUp(POINT point) {
    if (!pressed_) return false;
    const bool activate = enabled() && PointInRectInclusive(bounds_, point);
    pressed_ = false;
    if (GetCapture() == host_.window) ReleaseCapture();
    Invalidate();
    if (activate) Activate();
    return true;
}

bool ButtonComponent::CanFocus() const noexcept {
    return visible() && enabled() && Properties(definition_).tab_stop;
}

bool ButtonComponent::FocusNativePeer() {
    return SetFocus(host_.window) != nullptr || GetFocus() == host_.window;
}

void ButtonComponent::SetLogicalFocus(bool focused, bool window_active) {
    if (focused_ == focused && window_active_ == window_active) return;
    focused_ = focused;
    window_active_ = window_active;
    Invalidate();
}

bool ButtonComponent::HandleKeyDown(UINT virtual_key) {
    if (!focused_ || !window_active_ || !enabled() ||
        (virtual_key != VK_RETURN && virtual_key != VK_SPACE)) {
        return false;
    }
    Activate();
    return true;
}

config::VisualState ButtonComponent::State() const noexcept {
    if (!enabled()) return config::VisualState::Disabled;
    if (pressed_) return config::VisualState::Pressed;
    if (focused_ && window_active_) return config::VisualState::Focus;
    if (hovered_) return config::VisualState::Hover;
    return config::VisualState::Normal;
}

void ButtonComponent::Activate() {
    const auto event = definition_.events.find("click");
    if (event != definition_.events.end() && host_.dispatch_event) host_.dispatch_event(event->second);
}

}  // namespace ui::components
