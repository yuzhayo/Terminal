#include "ui/components/toggle/toggle_component.h"

#include <algorithm>

namespace ui::components {
namespace {

const config::ToggleProperties& Properties(const config::ResolvedComponent& definition) {
    return std::get<config::ToggleProperties>(definition.properties);
}

}  // namespace

ToggleComponent::ToggleComponent(const config::ResolvedComponent& definition, ComponentHost& host)
    : Component(definition, host) {}

MeasuredSize ToggleComponent::Measure(HDC dc, int available_width, int available_height) {
    (void)dc;
    const std::wstring label = ResolveText(Properties(definition_).label);
    const SIZE text = host_.render_runtime->MeasureText(
        label, style().font, host_.dpi, available_width, DT_SINGLELINE | DT_NOPREFIX);
    const int track_width = ScaleDip(36, host_.dpi);
    const int track_height = ScaleDip(20, host_.dpi);
    const int horizontal_padding = ScaleDip(
        style().content_padding.left + style().content_padding.right, host_.dpi);
    const int vertical_padding = ScaleDip(
        style().content_padding.top + style().content_padding.bottom, host_.dpi);
    return ApplyConstraints({track_width + ScaleDip(8, host_.dpi) + text.cx + horizontal_padding,
                             std::max(track_height, static_cast<int>(text.cy)) + vertical_padding},
                            available_width, available_height);
}

void ToggleComponent::Paint(HDC dc) {
    const config::VisualState state = State();
    PaintStyleBox(dc, state, bounds_);
    const auto& visual = style().states[static_cast<std::size_t>(state)];
    const COLORREF foreground = rendering::ToColorRef(
        host_.render_runtime->ResolveColor(visual.foreground));
    const COLORREF track_color = rendering::ToColorRef(host_.render_runtime->ResolveColor(
        style().states[static_cast<std::size_t>(checked_ ? config::VisualState::Selected
                                                        : config::VisualState::Normal)]
            .border));
    const int track_width = ScaleDip(36, host_.dpi);
    const int track_height = ScaleDip(20, host_.dpi);
    const int left = bounds_.left + ScaleDip(style().content_padding.left, host_.dpi);
    const int top = bounds_.top + (bounds_.bottom - bounds_.top - track_height) / 2;
    RECT track{left, top, left + track_width, top + track_height};
    HBRUSH track_brush = CreateSolidBrush(track_color);
    HPEN track_pen = CreatePen(PS_SOLID, 1, track_color);
    HGDIOBJ old_brush = SelectObject(dc, track_brush);
    HGDIOBJ old_pen = SelectObject(dc, track_pen);
    RoundRect(dc, track.left, track.top, track.right, track.bottom, track_height, track_height);
    const int inset = ScaleDip(3, host_.dpi);
    const int knob_size = track_height - inset * 2;
    const int knob_left = checked_ ? track.right - inset - knob_size : track.left + inset;
    HBRUSH knob_brush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    SelectObject(dc, knob_brush);
    Ellipse(dc, knob_left, track.top + inset, knob_left + knob_size, track.top + inset + knob_size);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(knob_brush);
    DeleteObject(track_pen);
    DeleteObject(track_brush);

    RECT label_bounds{track.right + ScaleDip(8, host_.dpi), bounds_.top,
                      bounds_.right - ScaleDip(style().content_padding.right, host_.dpi), bounds_.bottom};
    const std::wstring label = ResolveText(Properties(definition_).label);
    host_.render_runtime->DrawTextRun(dc, label, style().font, host_.dpi, label_bounds,
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                      foreground);
}

bool ToggleComponent::PointerMove(POINT point) {
    const bool hovered = enabled() && PointInRectInclusive(bounds_, point);
    if (hovered == hovered_) return false;
    hovered_ = hovered;
    Invalidate();
    return true;
}

bool ToggleComponent::PointerDown(POINT point) {
    if (!enabled() || !PointInRectInclusive(bounds_, point)) return false;
    pressed_ = true;
    SetCapture(host_.window);
    Invalidate();
    return true;
}

bool ToggleComponent::PointerUp(POINT point) {
    if (!pressed_) return false;
    const bool activate = enabled() && PointInRectInclusive(bounds_, point);
    pressed_ = false;
    if (GetCapture() == host_.window) ReleaseCapture();
    Invalidate();
    if (activate) Toggle();
    return true;
}

bool ToggleComponent::CanFocus() const noexcept {
    return visible() && enabled() && Properties(definition_).tab_stop;
}

bool ToggleComponent::FocusNativePeer() {
    SetFocus(host_.window);
    return GetFocus() == host_.window;
}

void ToggleComponent::SetLogicalFocus(bool focused, bool window_active) {
    if (focused_ == focused && window_active_ == window_active) return;
    focused_ = focused;
    window_active_ = window_active;
    Invalidate();
}

bool ToggleComponent::HandleKeyDown(UINT virtual_key) {
    if (!focused_ || !window_active_ || !enabled() || virtual_key != VK_SPACE) return false;
    Toggle();
    return true;
}

AutomationRole ToggleComponent::automation_role() const noexcept {
    return AutomationRole::ToggleButton;
}

std::wstring ToggleComponent::automation_name() const {
    return ResolveAutomationName(definition_, ResolveText(Properties(definition_).label));
}

std::optional<bool> ToggleComponent::automation_toggle_state() const noexcept {
    return checked_;
}

bool ToggleComponent::AutomationToggle() {
    if (!enabled()) return false;
    Toggle();
    return true;
}

config::VisualState ToggleComponent::State() const noexcept {
    if (!enabled()) return config::VisualState::Disabled;
    if (pressed_) return config::VisualState::Pressed;
    if (focused_ && window_active_) return config::VisualState::Focus;
    if (hovered_) return config::VisualState::Hover;
    if (checked_) return config::VisualState::Selected;
    return config::VisualState::Normal;
}

void ToggleComponent::Toggle() {
    checked_ = !checked_;
    Invalidate();
    config::EventPayloadValue checked;
    checked.value = checked_;
    EmitEvent("changed", {{"checked", std::move(checked)}});
}

}  // namespace ui::components
