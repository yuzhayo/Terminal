#include "ui/components/button/button_component.h"

#include <algorithm>

namespace ui::components {
namespace {

const config::ButtonProperties& Properties(const config::ResolvedComponent& definition) {
    return std::get<config::ButtonProperties>(definition.properties);
}

}  // namespace

MeasuredSize ButtonComponent::Measure(HDC dc, int available_width, int available_height) {
    (void)dc;
    const std::wstring label = ResolveText(Properties(definition_).label);
    const SIZE text_size = host_.render_runtime->MeasureText(
        label, style().font, host_.dpi, available_width, DT_SINGLELINE | DT_NOPREFIX);
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
    host_.render_runtime->DrawTextRun(dc, label, style().font, host_.dpi, bounds_,
                                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                      rendering::ToColorRef(foreground));
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

AutomationRole ButtonComponent::automation_role() const noexcept { return AutomationRole::Button; }

std::wstring ButtonComponent::automation_name() const {
    return ResolveAutomationName(definition_, ResolveText(Properties(definition_).label));
}

bool ButtonComponent::automation_supports_invoke() const noexcept { return true; }

bool ButtonComponent::AutomationInvoke() {
    if (!enabled()) return false;
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
    EmitEvent("click");
}

}  // namespace ui::components
