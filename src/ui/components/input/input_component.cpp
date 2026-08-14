#include "ui/components/input/input_component.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>

namespace ui::components {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}

}  // namespace

InputComponent::InputComponent(const config::ResolvedComponent& definition, ComponentHost& host)
    : Component(definition, host) {
    const config::InputProperties& properties = Properties();
    DWORD style = WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | ES_LEFT;
    if (definition.visible) style |= WS_VISIBLE;
    if (properties.mode == config::InputMode::Multiline) {
        style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
    } else {
        style |= ES_AUTOHSCROLL;
    }
    if (properties.password) style |= ES_PASSWORD;
    edit_ = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 0, 0, host.window, nullptr,
                            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(host.window, GWLP_HINSTANCE)),
                            nullptr);
    if (edit_) ApplyNativeStyle();
}

InputComponent::~InputComponent() {
    if (edit_ && IsWindow(edit_)) {
        RemoveWindowSubclass(edit_, EditSubclassProcedure, 1);
        DestroyWindow(edit_);
    }
}

const config::InputProperties& InputComponent::Properties() const {
    return std::get<config::InputProperties>(definition_.properties);
}

void InputComponent::ApplyNativeStyle() {
    const config::InputProperties& properties = Properties();
    const rendering::RgbaColor background = host_.render_runtime->ResolveColor(
        style().states[static_cast<std::size_t>(config::VisualState::Normal)].background);
    const int luminance = static_cast<int>(background.red) * 299 +
                          static_cast<int>(background.green) * 587 +
                          static_cast<int>(background.blue) * 114;
    SetWindowTheme(edit_, luminance < 128000 ? L"DarkMode_Explorer" : nullptr, nullptr);
    SetWindowSubclass(edit_, EditSubclassProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(edit_, EM_SETREADONLY, properties.read_only ? TRUE : FALSE, 0);
    SendMessageW(edit_, EM_SETLIMITTEXT, static_cast<WPARAM>(properties.maximum_length), 0);
    HFONT font = host_.render_runtime->Font(style().font, host_.dpi);
    if (font) SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    EnableWindow(edit_, enabled());
}

MeasuredSize InputComponent::Measure(HDC dc, int available_width, int available_height) {
    HFONT font = host_.render_runtime->Font(style().font, host_.dpi);
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    if (previous) SelectObject(dc, previous);
    const int content_height = metrics.tmHeight +
                               ScaleDip(style().content_padding.top + style().content_padding.bottom,
                                        host_.dpi);
    const int height = std::max(ScaleDip(style().minimum_height, host_.dpi), content_height);
    return ApplyConstraints({std::min(available_width, ScaleDip(320, host_.dpi)), height},
                            available_width, available_height);
}

void InputComponent::Arrange(const RECT& bounds) {
    Component::Arrange(bounds);
    if (!edit_) return;
    const int border = ScaleDip(focused_ ? style().focus_width : style().border_width, host_.dpi);
    const int left = bounds.left + border + ScaleDip(style().content_padding.left, host_.dpi);
    const int top = bounds.top + border + ScaleDip(style().content_padding.top, host_.dpi);
    const int right = bounds.right - border - ScaleDip(style().content_padding.right, host_.dpi);
    const int bottom = bounds.bottom - border - ScaleDip(style().content_padding.bottom, host_.dpi);
    MoveWindow(edit_, left, top, std::max(0, right - left), std::max(0, bottom - top), TRUE);
}

void InputComponent::Paint(HDC dc) {
    PaintStyleBox(dc, State(), bounds_);
}

bool InputComponent::PointerDown(POINT point) {
    if (!enabled() || !edit_ || !PointInRectInclusive(bounds_, point)) return false;
    SetFocus(edit_);
    return true;
}

bool InputComponent::HandleCommand(HWND source, WORD notification) {
    if (source != edit_) return false;
    if (notification == EN_SETFOCUS) {
        focused_ = true;
        Arrange(bounds_);
        Invalidate();
    } else if (notification == EN_KILLFOCUS) {
        focused_ = false;
        Arrange(bounds_);
        Invalidate();
    } else if (notification == EN_CHANGE) {
        const auto event = definition_.events.find("changed");
        if (event != definition_.events.end() && host_.dispatch_event) host_.dispatch_event(event->second);
    }
    return true;
}

HBRUSH InputComponent::HandleControlColor(HDC dc, HWND source) {
    if (source != edit_) return nullptr;
    const config::ResolvedVisualState& visual = style().states[static_cast<std::size_t>(State())];
    const rendering::RgbaColor foreground = host_.render_runtime->ResolveColor(visual.foreground);
    const rendering::RgbaColor background = host_.render_runtime->ResolveColor(visual.background);
    SetTextColor(dc, rendering::ToColorRef(foreground));
    SetBkColor(dc, rendering::ToColorRef(background));
    SetBkMode(dc, OPAQUE);
    return host_.render_runtime->Brush(rendering::ToColorRef(background));
}

bool InputComponent::OwnsNativePeer(HWND source) const noexcept {
    return source == edit_;
}

LRESULT CALLBACK InputComponent::EditSubclassProcedure(HWND window, UINT message, WPARAM wparam,
                                                        LPARAM lparam, UINT_PTR subclass_id,
                                                        DWORD_PTR reference_data) {
    auto* owner = reinterpret_cast<InputComponent*>(reference_data);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, EditSubclassProcedure, subclass_id);
        if (owner && owner->edit_ == window) owner->edit_ = nullptr;
        return DefSubclassProc(window, message, wparam, lparam);
    }
    const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    if (owner && message == WM_PAINT) owner->PaintPlaceholder();
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) InvalidateRect(window, nullptr, FALSE);
    return result;
}

void InputComponent::PaintPlaceholder() {
    if (!edit_ || GetFocus() == edit_ || GetWindowTextLengthW(edit_) != 0 ||
        Properties().placeholder.empty()) {
        return;
    }
    HDC dc = GetDC(edit_);
    if (!dc) return;
    const config::ResolvedVisualState& visual =
        style().states[static_cast<std::size_t>(config::VisualState::Disabled)];
    const rendering::RgbaColor foreground = host_.render_runtime->ResolveColor(visual.foreground);
    HFONT font = host_.render_runtime->Font(style().font, host_.dpi);
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, rendering::ToColorRef(foreground));
    RECT bounds{};
    GetClientRect(edit_, &bounds);
    bounds.left += ScaleDip(1, host_.dpi);
    const std::wstring placeholder = Utf8ToWide(Properties().placeholder);
    DrawTextW(dc, placeholder.c_str(), static_cast<int>(placeholder.size()), &bounds,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (previous) SelectObject(dc, previous);
    ReleaseDC(edit_, dc);
}

config::VisualState InputComponent::State() const noexcept {
    if (!enabled()) return config::VisualState::Disabled;
    if (focused_) return config::VisualState::Focus;
    return config::VisualState::Normal;
}

}  // namespace ui::components
