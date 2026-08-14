#include "ui/components/input/input_component.h"

#include <commctrl.h>
#include <imm.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <utility>

#include "ui/components/input/native_peer_geometry.h"

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
    if (properties.mode == config::InputMode::Multiline &&
        properties.scrollbar == config::ScrollbarMode::Auto) {
        EnsureScrollbar();
    }
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
    const config::ResolvedVisualState& visual =
        style().states[static_cast<std::size_t>(enabled() ? config::VisualState::Normal
                                                         : config::VisualState::Disabled)];
    const rendering::RgbaColor foreground = host_.render_runtime->ResolveColor(visual.foreground);
    const rendering::RgbaColor background = host_.render_runtime->ResolveColor(visual.background);
    const int luminance = static_cast<int>(background.red) * 299 +
                          static_cast<int>(background.green) * 587 +
                          static_cast<int>(background.blue) * 114;
    SetWindowTheme(edit_, luminance < 128000 ? L"DarkMode_Explorer" : nullptr, nullptr);
    SetWindowSubclass(edit_, EditSubclassProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(edit_, EM_SETREADONLY, properties.read_only ? TRUE : FALSE, 0);
    SendMessageW(edit_, EM_SETLIMITTEXT, static_cast<WPARAM>(properties.maximum_length), 0);
    auto next_font = host_.render_runtime->native_peer_resources().AcquireFont(style().font, host_.dpi);
    auto next_brush = host_.render_runtime->native_peer_resources().AcquireBrush(
        rendering::ToColorRef(background));
    if (next_font && next_brush) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(next_font.get()), TRUE);
        native_foreground_ = rendering::ToColorRef(foreground);
        native_background_ = rendering::ToColorRef(background);
        font_lease_ = std::move(next_font);
        brush_lease_ = std::move(next_brush);
    }
    EnableWindow(edit_, enabled());
}

MeasuredSize InputComponent::Measure(HDC dc, int available_width, int available_height) {
    (void)dc;
    const SIZE metrics = host_.render_runtime->MeasureText(
        L"Mg", style().font, host_.dpi, available_width, DT_SINGLELINE | DT_NOPREFIX);
    const int content_height = metrics.cy +
                               ScaleDip(style().content_padding.top + style().content_padding.bottom,
                                        host_.dpi);
    const int height = std::max(ScaleDip(style().minimum_height, host_.dpi), content_height);
    return ApplyConstraints({std::min(available_width, ScaleDip(320, host_.dpi)), height},
                            available_width, available_height);
}

void InputComponent::Arrange(const RECT& bounds) {
    Component::Arrange(bounds);
    if (!edit_) return;
    const int border = ScaleDip(logically_focused_ && window_active_ ? style().focus_width
                                                                    : style().border_width,
                                host_.dpi);
    const int left = bounds.left + border + ScaleDip(style().content_padding.left, host_.dpi);
    const int top = bounds.top + border + ScaleDip(style().content_padding.top, host_.dpi);
    int right = bounds.right - border - ScaleDip(style().content_padding.right, host_.dpi);
    const int bottom = bounds.bottom - border - ScaleDip(style().content_padding.bottom, host_.dpi);
    RECT reserved{};
    if (scrollbar_) {
        const int thickness = ScaleDip(
            std::get<config::ScrollbarProperties>(scrollbar_definition_.properties).thickness,
            host_.dpi);
        reserved = {std::max(left, right - thickness), top, right, bottom};
        right = reserved.left;
        scrollbar_->Arrange(reserved);
    }
    native_peer_content_rect_ = {left, top, right, bottom};
    const std::array<RECT, 1> reserved_regions{reserved};
    geometry_valid_ = ValidateNativePeerGeometry(
        bounds_, native_peer_content_rect_, scrollbar_ ? std::span<const RECT>(reserved_regions)
                                                       : std::span<const RECT>{});
    if (!geometry_valid_) {
        ShowWindow(edit_, SW_HIDE);
        return;
    }
    MoveWindow(edit_, left, top, right - left, bottom - top, TRUE);
    SyncScrollbarFromPeer();
    if (!scrollbar_visible_ && scrollbar_) {
        const int full_right = bounds.right - border -
                               ScaleDip(style().content_padding.right, host_.dpi);
        native_peer_content_rect_.right = full_right;
        MoveWindow(edit_, left, top, full_right - left, bottom - top, TRUE);
    }
}

void InputComponent::Paint(HDC dc) {
    PaintStyleBox(dc, geometry_valid_ ? State() : config::VisualState::Disabled, bounds_);
    if (suspended_) PaintSuspendedSnapshot(dc);
    if (scrollbar_visible_) scrollbar_->Paint(dc);
}

Component* InputComponent::HitTest(POINT point) {
    if (!visible() || !PointInRectInclusive(bounds_, point)) return nullptr;
    if (scrollbar_visible_ && scrollbar_->HitTest(point)) return scrollbar_.get();
    return this;
}

bool InputComponent::PointerDown(POINT point) {
    if (!enabled() || !edit_ || !PointInRectInclusive(bounds_, point)) return false;
    SetFocus(edit_);
    return true;
}

bool InputComponent::HandleCommand(HWND source, WORD notification) {
    if (source != edit_) return false;
    if (notification == EN_SETFOCUS) {
        native_focused_ = true;
        if (host_.native_focus_changed) host_.native_focus_changed(this, true);
        Arrange(bounds_);
        Invalidate();
    } else if (notification == EN_KILLFOCUS) {
        native_focused_ = false;
        if (host_.native_focus_changed) host_.native_focus_changed(this, false);
        Arrange(bounds_);
        Invalidate();
    } else if (notification == EN_CHANGE) {
        draft_.Update(ReadPeerText());
        Arrange(bounds_);
        const auto event = definition_.events.find("changed");
        if (event != definition_.events.end() && host_.dispatch_event) host_.dispatch_event(event->second);
    }
    return true;
}

HBRUSH InputComponent::HandleControlColor(HDC dc, HWND source) {
    if (source != edit_ || !brush_lease_) return nullptr;
    SetTextColor(dc, native_foreground_);
    SetBkColor(dc, native_background_);
    SetBkMode(dc, OPAQUE);
    return brush_lease_.get();
}

bool InputComponent::OwnsNativePeer(HWND source) const noexcept {
    return source == edit_;
}

bool InputComponent::CanFocus() const noexcept {
    return visible() && enabled() && Properties().tab_stop && edit_ && IsWindow(edit_);
}

bool InputComponent::FocusNativePeer() {
    if (!CanFocus()) return false;
    SetFocus(edit_);
    return GetFocus() == edit_;
}

void InputComponent::SetLogicalFocus(bool focused, bool window_active) {
    if (logically_focused_ == focused && window_active_ == window_active) return;
    logically_focused_ = focused;
    window_active_ = window_active;
    Arrange(bounds_);
    Invalidate();
}

void InputComponent::OnDpiChanged() {
    ApplyNativeStyle();
    Arrange(bounds_);
    Invalidate();
}

bool InputComponent::PrepareResources(COLORREF parent_background) {
    if (!Component::PrepareResources(parent_background)) return false;
    return !scrollbar_ || scrollbar_->PrepareResources(parent_background);
}

bool InputComponent::SuspendNativePeers(std::wstring& diagnostic) {
    if (suspended_ || !edit_ || !IsWindow(edit_)) {
        diagnostic.clear();
        return true;
    }
    if (!CompleteActiveIme(diagnostic)) return false;
    draft_.Update(ReadPeerText());
    SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&suspended_selection_start_),
                 reinterpret_cast<LPARAM>(&suspended_selection_end_));
    suspended_first_visible_line_ = static_cast<int>(SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0));
    restore_focus_after_resume_ = GetFocus() == edit_;
    if (Properties().password) {
        suspended_display_text_.assign(draft_.value().size(), L'\x2022');
    } else {
        suspended_display_text_ = draft_.value();
    }
    suspended_ = true;
    ShowWindow(edit_, SW_HIDE);
    Invalidate();
    diagnostic.clear();
    return true;
}

void InputComponent::ResumeNativePeers() {
    if (!suspended_ || !edit_ || !IsWindow(edit_) || !geometry_valid_) return;
    ApplyNativeStyle();
    Arrange(bounds_);
    WriteDraftToPeer();
    SendMessageW(edit_, EM_SETSEL, suspended_selection_start_, suspended_selection_end_);
    const int current_line = static_cast<int>(SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0));
    SendMessageW(edit_, EM_LINESCROLL, 0, suspended_first_visible_line_ - current_line);
    suspended_ = false;
    Invalidate();
    RedrawWindow(host_.window, &bounds_, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    ShowWindow(edit_, SW_SHOWNA);
    if (restore_focus_after_resume_) FocusNativePeer();
    restore_focus_after_resume_ = false;
    suspended_display_text_.clear();
}

bool InputComponent::RequiresNativePeerSuppression() const noexcept { return true; }

void InputComponent::CollectEditableParticipants(std::vector<EditableParticipant*>& participants) {
    participants.push_back(this);
}

void InputComponent::CollectAutomationElements(std::vector<Component*>& elements) {
    Component::CollectAutomationElements(elements);
    if (scrollbar_) scrollbar_->CollectAutomationElements(elements);
}

AutomationRole InputComponent::automation_role() const noexcept { return AutomationRole::Edit; }

std::wstring InputComponent::automation_name() const {
    std::wstring fallback = Utf8ToWide(Properties().placeholder);
    if (fallback.empty()) fallback = Utf8ToWide(definition_.id);
    return ResolveAutomationName(definition_, std::move(fallback));
}

HWND InputComponent::automation_native_peer() const noexcept { return edit_; }
bool InputComponent::automation_is_password() const noexcept { return Properties().password; }

bool InputComponent::IsDirty() const noexcept {
    return draft_.is_dirty();
}

bool InputComponent::StageDiscard() {
    if (!draft_.StageDiscard()) return false;
    WriteDraftToPeer();
    return true;
}

void InputComponent::CommitDiscard() noexcept {
    draft_.CommitDiscard();
}

void InputComponent::RollbackDiscard() {
    draft_.RollbackDiscard();
    WriteDraftToPeer();
}

void InputComponent::ApplySaveResult(bool success) {
    draft_.ApplySaveResult(success);
}

LRESULT CALLBACK InputComponent::EditSubclassProcedure(HWND window, UINT message, WPARAM wparam,
                                                        LPARAM lparam, UINT_PTR subclass_id,
                                                        DWORD_PTR reference_data) {
    auto* owner = reinterpret_cast<InputComponent*>(reference_data);
    if (message == WM_KEYDOWN && wparam == VK_TAB && owner &&
        owner->host_.request_focus_traversal) {
        owner->host_.request_focus_traversal((GetKeyState(VK_SHIFT) & 0x8000) != 0);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, EditSubclassProcedure, subclass_id);
        if (owner && owner->edit_ == window) owner->edit_ = nullptr;
        return DefSubclassProc(window, message, wparam, lparam);
    }
    const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    if (owner && message == WM_PAINT) owner->PaintPlaceholder();
    if (owner && (message == WM_MOUSEWHEEL || message == WM_VSCROLL || message == WM_KEYUP ||
                  message == WM_CHAR)) {
        owner->SyncScrollbarFromPeer();
    }
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
    HFONT font = font_lease_.get();
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

void InputComponent::PaintSuspendedSnapshot(HDC dc) {
    std::wstring text = suspended_display_text_;
    bool placeholder = false;
    if (text.empty() && !Properties().placeholder.empty()) {
        text = Utf8ToWide(Properties().placeholder);
        placeholder = true;
    }
    const config::VisualState state = placeholder ? config::VisualState::Disabled
                                                   : config::VisualState::Normal;
    const config::ResolvedVisualState& visual = style().states[static_cast<std::size_t>(state)];
    const rendering::RgbaColor foreground = host_.render_runtime->ResolveColor(visual.foreground);
    HGDIOBJ previous = font_lease_ ? SelectObject(dc, font_lease_.get()) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, rendering::ToColorRef(foreground));
    RECT text_bounds = native_peer_content_rect_;
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &text_bounds,
              DT_LEFT | (Properties().mode == config::InputMode::SingleLine ? DT_VCENTER | DT_SINGLELINE
                                                                             : DT_TOP | DT_WORDBREAK) |
                  DT_END_ELLIPSIS | DT_NOPREFIX);
    if (previous) SelectObject(dc, previous);
}

std::wstring InputComponent::ReadPeerText() const {
    if (!edit_) return draft_.value();
    const int length = GetWindowTextLengthW(edit_);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(edit_, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(std::max(0, copied)));
    return value;
}

void InputComponent::WriteDraftToPeer() {
    if (edit_ && IsWindow(edit_)) SetWindowTextW(edit_, draft_.value().c_str());
    suspended_display_text_ = Properties().password
                                  ? std::wstring(draft_.value().size(), L'\x2022')
                                  : draft_.value();
    Invalidate();
}

bool InputComponent::CompleteActiveIme(std::wstring& diagnostic) {
    if (!edit_ || GetFocus() != edit_) return true;
    HIMC context = ImmGetContext(edit_);
    if (!context) return true;
    const LONG composition_length = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
    if (composition_length > 0 && !ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_COMPLETE, 0)) {
        ImmReleaseContext(edit_, context);
        diagnostic = L"IME composition tidak dapat diselesaikan sebelum Input disuspend.";
        return false;
    }
    ImmNotifyIME(context, NI_CLOSECANDIDATE, 0, 0);
    ImmReleaseContext(edit_, context);
    return true;
}

config::VisualState InputComponent::State() const noexcept {
    if (!enabled()) return config::VisualState::Disabled;
    if (logically_focused_ && window_active_) return config::VisualState::Focus;
    return config::VisualState::Normal;
}

void InputComponent::EnsureScrollbar() {
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
    properties.orientation = config::Orientation::Vertical;
    scrollbar_definition_.properties = properties;
    scrollbar_ = std::make_unique<ScrollbarComponent>(scrollbar_definition_, host_);
    scrollbar_->Bind(this);
}

void InputComponent::SyncScrollbarFromPeer() {
    if (!scrollbar_ || !edit_ || !IsWindow(edit_)) return;
    scroll_line_count_ = std::max(1, static_cast<int>(SendMessageW(edit_, EM_GETLINECOUNT, 0, 0)));
    const int content_width = std::max(
        1, static_cast<int>(native_peer_content_rect_.right - native_peer_content_rect_.left));
    const SIZE metrics = host_.render_runtime->MeasureText(
        L"Mg", style().font, host_.dpi, content_width,
        DT_SINGLELINE | DT_NOPREFIX);
    const int line_height = std::max(1, static_cast<int>(metrics.cy));
    scroll_visible_lines_ = std::max(
        1, static_cast<int>(native_peer_content_rect_.bottom - native_peer_content_rect_.top) /
               line_height);
    scroll_value_ = std::clamp(
        static_cast<int>(SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0)), 0, ScrollMaximum());
    const bool visible = ScrollMaximum() > 0;
    if (visible != scrollbar_visible_) {
        scrollbar_visible_ = visible;
        Invalidate();
    } else if (visible) {
        scrollbar_->Refresh();
    }
}

int InputComponent::ScrollMinimum() const noexcept { return 0; }
int InputComponent::ScrollMaximum() const noexcept {
    return std::max(0, scroll_line_count_ - scroll_visible_lines_);
}
int InputComponent::ScrollPageSize() const noexcept { return scroll_visible_lines_; }
int InputComponent::ScrollValue() const noexcept { return scroll_value_; }

void InputComponent::SetScrollValue(int value) {
    if (!edit_ || !IsWindow(edit_)) return;
    const int clamped = std::clamp(value, ScrollMinimum(), ScrollMaximum());
    const int current = static_cast<int>(SendMessageW(edit_, EM_GETFIRSTVISIBLELINE, 0, 0));
    if (clamped != current) SendMessageW(edit_, EM_LINESCROLL, 0, clamped - current);
    scroll_value_ = clamped;
    if (scrollbar_) scrollbar_->Refresh();
}

}  // namespace ui::components
