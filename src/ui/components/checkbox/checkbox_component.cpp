#include "ui/components/checkbox/checkbox_component.h"

#include <algorithm>

namespace ui::components {
namespace {

const config::CheckboxProperties& Properties(const config::ResolvedComponent& definition) {
    return std::get<config::CheckboxProperties>(definition.properties);
}

}  // namespace

CheckboxComponent::CheckboxComponent(const config::ResolvedComponent& definition, ComponentHost& host)
    : Component(definition, host) {}

MeasuredSize CheckboxComponent::Measure(HDC dc, int available_width, int available_height) {
    (void)dc;
    const std::wstring label = ResolveText(Properties(definition_).label);
    const SIZE text = host_.render_runtime->MeasureText(
        label, style().font, host_.dpi, available_width, DT_SINGLELINE | DT_NOPREFIX);
    const int box = ScaleDip(std::max(16, style().minimum_height - 6), host_.dpi);
    const int gap = ScaleDip(8, host_.dpi);
    const int horizontal_padding = ScaleDip(
        style().content_padding.left + style().content_padding.right, host_.dpi);
    const int vertical_padding = ScaleDip(
        style().content_padding.top + style().content_padding.bottom, host_.dpi);
    return ApplyConstraints({box + gap + text.cx + horizontal_padding,
                             std::max(box, static_cast<int>(text.cy)) + vertical_padding},
                            available_width, available_height);
}

void CheckboxComponent::Paint(HDC dc) {
    const config::VisualState state = State();
    PaintStyleBox(dc, state, bounds_);
    const auto& visual = style().states[static_cast<std::size_t>(state)];
    const COLORREF foreground = rendering::ToColorRef(
        host_.render_runtime->ResolveColor(visual.foreground));
    const int padding_left = ScaleDip(style().content_padding.left, host_.dpi);
    const int box_size = std::min(ScaleDip(18, host_.dpi),
                                  std::max(0, static_cast<int>(bounds_.bottom - bounds_.top)));
    const int box_top = bounds_.top + (bounds_.bottom - bounds_.top - box_size) / 2;
    RECT box{bounds_.left + padding_left, box_top, bounds_.left + padding_left + box_size,
             box_top + box_size};
    HBRUSH brush = CreateSolidBrush(checked_ ? foreground : GetSysColor(COLOR_WINDOW));
    FillRect(dc, &box, brush);
    DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleDip(1, host_.dpi)), foreground);
    HGDIOBJ previous = SelectObject(dc, pen);
    HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, box.left, box.top, box.right, box.bottom);
    if (checked_) {
        HPEN check_pen = CreatePen(PS_SOLID, std::max(1, ScaleDip(2, host_.dpi)),
                                   rendering::ToColorRef(host_.render_runtime->ResolveColor(
                                       style().states[static_cast<std::size_t>(config::VisualState::Selected)]
                                           .foreground)));
        SelectObject(dc, check_pen);
        MoveToEx(dc, box.left + box_size / 5, box.top + box_size / 2, nullptr);
        LineTo(dc, box.left + box_size * 2 / 5, box.bottom - box_size / 4);
        LineTo(dc, box.right - box_size / 5, box.top + box_size / 4);
        SelectObject(dc, pen);
        DeleteObject(check_pen);
    }
    SelectObject(dc, previous_brush);
    SelectObject(dc, previous);
    DeleteObject(pen);

    RECT label_bounds{box.right + ScaleDip(8, host_.dpi), bounds_.top,
                      bounds_.right - ScaleDip(style().content_padding.right, host_.dpi), bounds_.bottom};
    const std::wstring label = ResolveText(Properties(definition_).label);
    host_.render_runtime->DrawTextRun(dc, label, style().font, host_.dpi, label_bounds,
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                      foreground);
}

bool CheckboxComponent::PointerMove(POINT point) {
    const bool hovered = enabled() && PointInRectInclusive(bounds_, point);
    if (hovered == hovered_) return false;
    hovered_ = hovered;
    Invalidate();
    return true;
}

bool CheckboxComponent::PointerDown(POINT point) {
    if (!enabled() || !PointInRectInclusive(bounds_, point)) return false;
    pressed_ = true;
    SetCapture(host_.window);
    Invalidate();
    return true;
}

bool CheckboxComponent::PointerUp(POINT point) {
    if (!pressed_) return false;
    const bool activate = enabled() && PointInRectInclusive(bounds_, point);
    pressed_ = false;
    if (GetCapture() == host_.window) ReleaseCapture();
    Invalidate();
    if (activate) Toggle();
    return true;
}

bool CheckboxComponent::CanFocus() const noexcept {
    return visible() && enabled() && Properties(definition_).tab_stop;
}

bool CheckboxComponent::FocusNativePeer() {
    SetFocus(host_.window);
    return GetFocus() == host_.window;
}

void CheckboxComponent::SetLogicalFocus(bool focused, bool window_active) {
    if (focused_ == focused && window_active_ == window_active) return;
    focused_ = focused;
    window_active_ = window_active;
    Invalidate();
}

bool CheckboxComponent::HandleKeyDown(UINT virtual_key) {
    if (!focused_ || !window_active_ || !enabled() || virtual_key != VK_SPACE) return false;
    Toggle();
    return true;
}

AutomationRole CheckboxComponent::automation_role() const noexcept {
    return AutomationRole::Checkbox;
}

std::wstring CheckboxComponent::automation_name() const {
    return ResolveAutomationName(definition_, ResolveText(Properties(definition_).label));
}

std::optional<bool> CheckboxComponent::automation_toggle_state() const noexcept {
    return checked_;
}

bool CheckboxComponent::AutomationToggle() {
    if (!enabled()) return false;
    Toggle();
    return true;
}

config::VisualState CheckboxComponent::State() const noexcept {
    if (!enabled()) return config::VisualState::Disabled;
    if (pressed_) return config::VisualState::Pressed;
    if (focused_ && window_active_) return config::VisualState::Focus;
    if (hovered_) return config::VisualState::Hover;
    if (checked_) return config::VisualState::Selected;
    return config::VisualState::Normal;
}

void CheckboxComponent::Toggle() {
    checked_ = !checked_;
    Invalidate();
    config::EventPayloadValue checked;
    checked.value = checked_;
    EmitEvent("changed", {{"checked", std::move(checked)}});
}

}  // namespace ui::components
