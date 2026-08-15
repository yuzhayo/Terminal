#include "ui/components/combo/combo_component.h"

#include <windowsx.h>
#include <ole2.h>
#include <oleacc.h>
#include <UIAutomation.h>

#include <algorithm>

namespace ui::components {
namespace {

constexpr wchar_t kComboPopupClass[] = L"Yuzha.Terminal.ComboPopup";

std::size_t StateIndex(config::VisualState state) noexcept {
    return static_cast<std::size_t>(state);
}

}  // namespace

ComboPopupPlacement CalculateComboPopupPlacement(const RECT& trigger, const RECT& work,
                                                  SIZE popup, int gap) noexcept {
    ComboPopupPlacement result{};
    const int below = work.bottom - trigger.bottom - gap;
    const int above = trigger.top - work.top - gap;
    result.opens_above = below < popup.cy && above > below;
    result.origin.x = std::clamp(trigger.left, work.left, std::max(work.left, work.right - popup.cx));
    result.origin.y = result.opens_above ? trigger.top - gap - popup.cy : trigger.bottom + gap;
    result.origin.y = std::clamp(result.origin.y, work.top, std::max(work.top, work.bottom - popup.cy));
    return result;
}

ComboPopupMetrics CalculateComboPopupMetrics(int trigger_width, std::size_t item_count,
                                              int maximum_visible_items,
                                              int popup_maximum_height, SIZE work_area,
                                              UINT dpi) noexcept {
    ComboPopupMetrics result;
    const UINT effective_dpi = dpi == 0 ? 96 : dpi;
    result.shadow_margin = ScaleDip(8, effective_dpi);
    result.item_height = std::max(1, ScaleDip(32, effective_dpi));
    const int available_width = std::max(1, static_cast<int>(work_area.cx));
    const int available_height = std::max(1, static_cast<int>(work_area.cy));
    const int configured_height = std::max(
        result.item_height, ScaleDip(popup_maximum_height, effective_dpi));
    const int content_height_limit = std::max(
        1, std::min(configured_height, available_height) - result.shadow_margin * 2);
    const int height_limited_rows = std::max(1, content_height_limit / result.item_height);
    result.visible_rows = std::max(
        1, std::min({maximum_visible_items, height_limited_rows,
                     static_cast<int>(std::max<std::size_t>(1, item_count))}));
    const int body_width = std::max(trigger_width, ScaleDip(180, effective_dpi));
    result.surface.cx = std::min(available_width, body_width + result.shadow_margin * 2);
    result.surface.cy = std::min(
        available_height, result.visible_rows * result.item_height + result.shadow_margin * 2);
    return result;
}

ComboComponent::ComboComponent(const config::ResolvedComponent& definition, ComponentHost& host)
    : Component(definition, host), popup_render_context_(*host.render_runtime) {
    popup_render_context_.SetRedrawRequest([this] {
        if (popup_open_) PositionAndRenderPopup();
    });
    RefreshItems();
}

ComboComponent::~ComboComponent() {
    ClosePopup(false);
    if (popup_) DestroyWindow(popup_);
}

const config::ComboProperties& ComboComponent::Properties() const {
    return std::get<config::ComboProperties>(definition_.properties);
}

MeasuredSize ComboComponent::Measure(HDC, int available_width, int available_height) {
    std::wstring widest = Utf8ToWide(Properties().placeholder);
    for (const auto& item : items_) if (item.size() > widest.size()) widest = item;
    const SIZE text = host_.render_runtime->MeasureText(
        widest, style().font, host_.dpi, available_width, DT_SINGLELINE | DT_NOPREFIX);
    const int padding = ScaleDip(style().content_padding.left + style().content_padding.right,
                                 host_.dpi);
    const int arrow = ScaleDip(24, host_.dpi);
    const int height = std::max(ScaleDip(style().minimum_height, host_.dpi),
                                static_cast<int>(text.cy) +
                                    ScaleDip(style().content_padding.top +
                                                 style().content_padding.bottom,
                                             host_.dpi));
    return ApplyConstraints({text.cx + padding + arrow, height}, available_width, available_height);
}

void ComboComponent::Arrange(const RECT& bounds) {
    Component::Arrange(bounds);
    if (popup_open_) PositionAndRenderPopup();
}

void ComboComponent::Paint(HDC dc) {
    const config::VisualState state = State();
    PaintStyleBox(dc, state, bounds_);
    const config::ResolvedVisualState& visual = style().states[StateIndex(state)];
    const rendering::RgbaColor foreground = host_.render_runtime->ResolveColor(visual.foreground);
    std::wstring label = selected_index_ && *selected_index_ < items_.size()
                             ? items_[*selected_index_]
                             : Utf8ToWide(Properties().placeholder);
    RECT label_bounds = bounds_;
    label_bounds.left += ScaleDip(style().content_padding.left, host_.dpi);
    label_bounds.right -= ScaleDip(style().content_padding.right + 24, host_.dpi);
    host_.render_runtime->DrawTextRun(dc, label, style().font, host_.dpi, label_bounds,
                                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                                          DT_NOPREFIX,
                                      rendering::ToColorRef(foreground));
    RECT arrow_bounds{bounds_.right - ScaleDip(28, host_.dpi), bounds_.top,
                      bounds_.right - ScaleDip(6, host_.dpi), bounds_.bottom};
    host_.render_runtime->DrawTextRun(dc, popup_open_ ? L"\x25B4" : L"\x25BE", style().font,
                                      host_.dpi, arrow_bounds,
                                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                      rendering::ToColorRef(foreground));
}

bool ComboComponent::PointerMove(POINT point) {
    const bool next = enabled() && PointInRectInclusive(bounds_, point);
    if (next == hovered_) return false;
    hovered_ = next;
    Invalidate();
    return true;
}

bool ComboComponent::PointerDown(POINT point) {
    if (!enabled() || !PointInRectInclusive(bounds_, point)) return false;
    pressed_ = true;
    SetCapture(host_.window);
    Invalidate();
    return true;
}

bool ComboComponent::PointerUp(POINT point) {
    if (!pressed_) return false;
    const bool activate = enabled() && PointInRectInclusive(bounds_, point);
    pressed_ = false;
    if (GetCapture() == host_.window) ReleaseCapture();
    Invalidate();
    if (activate) popup_open_ ? ClosePopup() : OpenPopup();
    return true;
}

bool ComboComponent::CanFocus() const noexcept {
    return visible() && enabled() && Properties().tab_stop;
}

bool ComboComponent::FocusNativePeer() {
    SetFocus(host_.window);
    return GetFocus() == host_.window;
}

void ComboComponent::SetLogicalFocus(bool focused, bool window_active) {
    const bool changed = focused_ != focused || window_active_ != window_active;
    focused_ = focused;
    window_active_ = window_active;
    if ((!focused || !window_active) && popup_open_) ClosePopup();
    if (changed) Invalidate();
}

bool ComboComponent::HandleKeyDown(UINT key) {
    if (!focused_ || !window_active_ || !enabled()) return false;
    if (key == VK_ESCAPE && popup_open_) {
        ClosePopup();
        return true;
    }
    if (key == VK_F4 || ((key == VK_RETURN || key == VK_SPACE) && !popup_open_)) {
        popup_open_ ? ClosePopup() : OpenPopup();
        return true;
    }
    if (!popup_open_) {
        if (key == VK_DOWN || key == VK_UP) {
            OpenPopup();
            MoveHighlight(key == VK_DOWN ? 1 : -1);
            return true;
        }
        return false;
    }
    if (key == VK_DOWN || key == VK_UP || key == VK_HOME || key == VK_END) {
        if (key == VK_HOME) highlighted_index_ = items_.empty() ? -1 : 0;
        else if (key == VK_END) highlighted_index_ = static_cast<int>(items_.size()) - 1;
        else MoveHighlight(key == VK_DOWN ? 1 : -1);
        EnsureHighlightVisible();
        RenderPopup();
        return true;
    }
    if (key == VK_RETURN || key == VK_SPACE) {
        SelectHighlighted();
        return true;
    }
    return false;
}

bool ComboComponent::HasOpenPopup() const noexcept { return popup_open_; }
HWND ComboComponent::OwnedPopupHwnd() const noexcept { return popup_; }

bool ComboComponent::OwnsPopupScopePoint(POINT screen_point) const noexcept {
    RECT popup_bounds{};
    if (popup_open_ && popup_ && GetWindowRect(popup_, &popup_bounds) &&
        PtInRect(&popup_bounds, screen_point)) return true;
    POINT trigger_origin{bounds_.left, bounds_.top};
    ClientToScreen(host_.window, &trigger_origin);
    RECT trigger{trigger_origin.x, trigger_origin.y,
                 trigger_origin.x + bounds_.right - bounds_.left,
                 trigger_origin.y + bounds_.bottom - bounds_.top};
    return PtInRect(&trigger, screen_point) != FALSE;
}

void ComboComponent::DismissOwnedPopup() { ClosePopup(); }

bool ComboComponent::SuspendNativePeers(std::wstring& diagnostic) {
    ClosePopup();
    diagnostic.clear();
    return true;
}

void ComboComponent::OnDpiChanged() {
    popup_dpi_ = host_.dpi;
    shadow_margin_ = ScaleDip(8, popup_dpi_);
    item_height_ = ScaleDip(32, popup_dpi_);
    if (popup_open_) PositionAndRenderPopup();
}

void ComboComponent::CaptureRuntimeState(ComponentRuntimeStateMap& states) const {
    ComponentRuntimeState state;
    state.type = definition_.type;
    state.selected_index = selected_index_;
    states.insert_or_assign(definition_.id, std::move(state));
    Component::CaptureRuntimeState(states);
}

void ComboComponent::RestoreRuntimeState(const ComponentRuntimeStateMap& states) {
    const auto found = states.find(definition_.id);
    if (found != states.end() && found->second.type == definition_.type) {
        selected_index_ = found->second.selected_index;
        if (selected_index_ && *selected_index_ >= items_.size()) selected_index_.reset();
        highlighted_index_ = selected_index_ ? static_cast<int>(*selected_index_) : -1;
        EnsureHighlightVisible();
    }
    Component::RestoreRuntimeState(states);
}

AutomationRole ComboComponent::automation_role() const noexcept { return AutomationRole::Combo; }

std::wstring ComboComponent::automation_name() const {
    std::wstring fallback = selected_index_ && *selected_index_ < items_.size()
                                ? items_[*selected_index_]
                                : Utf8ToWide(Properties().placeholder);
    return ResolveAutomationName(definition_, std::move(fallback));
}

std::optional<bool> ComboComponent::automation_expanded() const noexcept { return popup_open_; }
bool ComboComponent::AutomationExpand() {
    if (!enabled()) return false;
    OpenPopup();
    return popup_open_;
}
bool ComboComponent::AutomationCollapse() {
    if (!popup_open_) return true;
    ClosePopup();
    return !popup_open_;
}

bool ComboComponent::automation_has_popup_fragment() const noexcept { return true; }
bool ComboComponent::automation_popup_visible() const noexcept {
    return popup_open_ && popup_ && IsWindowVisible(popup_);
}
HWND ComboComponent::automation_popup_hwnd() const noexcept { return popup_; }
std::size_t ComboComponent::automation_popup_item_count() const noexcept { return items_.size(); }
std::wstring ComboComponent::automation_popup_item_name(std::size_t index) const {
    return index < items_.size() ? items_[index] : std::wstring{};
}
std::optional<RECT> ComboComponent::automation_popup_item_screen_bounds(
    std::size_t index) const noexcept {
    if (!automation_popup_item_realized(index)) return std::nullopt;
    RECT popup_bounds{};
    if (!GetWindowRect(popup_, &popup_bounds)) return std::nullopt;
    const int row = static_cast<int>(index) - first_visible_index_;
    return RECT{popup_bounds.left + shadow_margin_,
                popup_bounds.top + shadow_margin_ + row * item_height_,
                popup_bounds.right - shadow_margin_,
                popup_bounds.top + shadow_margin_ + (row + 1) * item_height_};
}
bool ComboComponent::automation_popup_item_realized(std::size_t index) const noexcept {
    return automation_popup_visible() && index >= static_cast<std::size_t>(first_visible_index_) &&
           index < static_cast<std::size_t>(first_visible_index_ + visible_rows_) &&
           index < items_.size();
}
bool ComboComponent::automation_popup_item_selected(std::size_t index) const noexcept {
    return selected_index_ && *selected_index_ == index;
}
bool ComboComponent::AutomationSelectPopupItem(std::size_t index) {
    if (!enabled() || index >= items_.size()) return false;
    if (!popup_open_) OpenPopup();
    highlighted_index_ = static_cast<int>(index);
    EnsureHighlightVisible();
    SelectHighlighted();
    return selected_index_ && *selected_index_ == index;
}
bool ComboComponent::AutomationRealizePopupItem(std::size_t index) {
    if (!enabled() || index >= items_.size()) return false;
    if (!popup_open_) OpenPopup();
    highlighted_index_ = static_cast<int>(index);
    EnsureHighlightVisible();
    PositionAndRenderPopup();
    return automation_popup_item_realized(index);
}

bool ComboComponent::EnsurePopup() {
    if (popup_) return true;
    WNDCLASSEXW popup_class{};
    popup_class.cbSize = sizeof(popup_class);
    popup_class.lpfnWndProc = PopupProcedure;
    popup_class.hInstance = GetModuleHandleW(nullptr);
    popup_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    popup_class.lpszClassName = kComboPopupClass;
    if (!RegisterClassExW(&popup_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    popup_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                             kComboPopupClass, L"", WS_POPUP, 0, 0, 1, 1, host_.window, nullptr,
                             popup_class.hInstance, this);
    return popup_ != nullptr;
}

void ComboComponent::OpenPopup() {
    if (popup_open_ || !enabled() || !EnsurePopup()) return;
    RefreshItems();
    highlighted_index_ = selected_index_ ? static_cast<int>(*selected_index_)
                                         : (items_.empty() ? -1 : 0);
    first_visible_index_ = 0;
    popup_open_ = true;
    if (host_.popup_state_changed) host_.popup_state_changed(this, true);
    PositionAndRenderPopup();
    ShowWindow(popup_, SW_SHOWNOACTIVATE);
    Dispatch("opened");
    Invalidate();
}

void ComboComponent::ClosePopup(bool dispatch_event) {
    if (!popup_open_) return;
    popup_open_ = false;
    if (popup_) ShowWindow(popup_, SW_HIDE);
    if (GetCapture() == popup_) ReleaseCapture();
    if (host_.popup_state_changed) host_.popup_state_changed(this, false);
    if (dispatch_event) Dispatch("closed");
    Invalidate();
}

void ComboComponent::RefreshItems() {
    items_ = host_.resolve_string_items
                 ? host_.resolve_string_items(Properties().items_binding.path)
                 : std::vector<std::wstring>{};
    if (host_.resolve_string_value) {
        const auto selected = host_.resolve_string_value(Properties().selected_value_binding.path);
        if (selected) {
            const auto found = std::find(items_.begin(), items_.end(), *selected);
            selected_index_ = found == items_.end()
                                  ? std::nullopt
                                  : std::optional<std::size_t>(found - items_.begin());
        }
    }
}

void ComboComponent::PositionAndRenderPopup(std::optional<UINT> dpi_override) {
    if (!popup_open_ || !popup_) return;
    popup_dpi_ = dpi_override.value_or(host_.dpi == 0 ? 96 : host_.dpi);
    POINT trigger_origin{bounds_.left, bounds_.top};
    ClientToScreen(host_.window, &trigger_origin);
    const RECT trigger{trigger_origin.x, trigger_origin.y,
                       trigger_origin.x + bounds_.right - bounds_.left,
                       trigger_origin.y + bounds_.bottom - bounds_.top};
    HMONITOR monitor = MonitorFromRect(&trigger, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &info.rcWork, 0);
    }
    const SIZE work_area{
        static_cast<LONG>(std::max(1L, info.rcWork.right - info.rcWork.left)),
        static_cast<LONG>(std::max(1L, info.rcWork.bottom - info.rcWork.top))};
    const ComboPopupMetrics metrics = CalculateComboPopupMetrics(
        bounds_.right - bounds_.left, items_.size(), Properties().maximum_visible_items,
        Properties().popup_maximum_height, work_area, popup_dpi_);
    shadow_margin_ = metrics.shadow_margin;
    item_height_ = metrics.item_height;
    visible_rows_ = metrics.visible_rows;
    EnsureHighlightVisible();
    const ComboPopupPlacement placement = CalculateComboPopupPlacement(
        trigger, info.rcWork, metrics.surface, ScaleDip(2, popup_dpi_));
    if (!popup_render_context_.EnsureSize(metrics.surface.cx, metrics.surface.cy)) {
        ClosePopup();
        return;
    }
    SetWindowPos(popup_, nullptr, placement.origin.x, placement.origin.y,
                 metrics.surface.cx, metrics.surface.cy, SWP_NOACTIVATE | SWP_NOZORDER);
    RenderPopup();
}

void ComboComponent::RenderPopup() {
    if (!popup_open_ || !popup_ || !popup_render_context_.valid()) return;
    popup_render_context_.Clear();
    const config::ResolvedVisualState& normal = style().states[StateIndex(config::VisualState::Normal)];
    const config::ResolvedVisualState& selected = style().states[StateIndex(config::VisualState::Selected)];
    const rendering::RgbaColor background = host_.render_runtime->ResolveColor(normal.background);
    const rendering::RgbaColor border = host_.render_runtime->ResolveColor(normal.border);
    const rendering::RgbaColor foreground = host_.render_runtime->ResolveColor(normal.foreground);
    const rendering::RgbaColor selection = host_.render_runtime->ResolveColor(selected.background);
    popup_render_context_.SourceOverRounded(
        {shadow_margin_ + ScaleDip(2, popup_dpi_), shadow_margin_ + ScaleDip(3, popup_dpi_),
         popup_render_context_.width() - shadow_margin_ + ScaleDip(2, popup_dpi_),
         popup_render_context_.height() - shadow_margin_ + ScaleDip(3, popup_dpi_)},
        ScaleDip(style().radius, popup_dpi_), 0, {0, 0, 0, 55}, {0, 0, 0, 0});
    const RECT body{shadow_margin_, shadow_margin_, popup_render_context_.width() - shadow_margin_,
                    popup_render_context_.height() - shadow_margin_};
    popup_render_context_.SourceOverRounded(body, ScaleDip(style().radius, popup_dpi_),
                                            std::max(1, ScaleDip(style().border_width, popup_dpi_)),
                                            background, border);
    const int count = std::min(visible_rows_,
                               static_cast<int>(items_.size()) - first_visible_index_);
    for (int row_index = 0; row_index < count; ++row_index) {
        const int item_index = first_visible_index_ + row_index;
        RECT row{body.left + 1, body.top + row_index * item_height_ + 1,
                 body.right - 1, body.top + (row_index + 1) * item_height_ + 1};
        if (item_index == highlighted_index_) {
            popup_render_context_.SourceOverRounded(row, ScaleDip(4, popup_dpi_), 0, selection,
                                                    {0, 0, 0, 0});
        }
        row.left += ScaleDip(style().content_padding.left, popup_dpi_);
        row.right -= ScaleDip(style().content_padding.right, popup_dpi_);
        popup_render_context_.DrawTextMask(items_[static_cast<std::size_t>(item_index)],
                                           style().font, popup_dpi_, row,
                                           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                                               DT_NOPREFIX,
                                           foreground);
    }
    if (items_.empty()) {
        RECT row = body;
        row.left += ScaleDip(style().content_padding.left, popup_dpi_);
        popup_render_context_.DrawTextMask(L"Tidak ada pilihan", style().font, popup_dpi_, row,
                                           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                                           foreground);
    }
    RECT window_bounds{};
    GetWindowRect(popup_, &window_bounds);
    popup_render_context_.Present(popup_, {window_bounds.left, window_bounds.top});
}

int ComboComponent::ItemAt(POINT point) const noexcept {
    const int x = point.x - shadow_margin_;
    const int y = point.y - shadow_margin_;
    if (x < 0 || y < 0 || x >= popup_render_context_.width() - shadow_margin_ * 2) return -1;
    const int row = y / item_height_;
    const int index = first_visible_index_ + row;
    return row >= 0 && row < visible_rows_ && index < static_cast<int>(items_.size())
               ? index
               : -1;
}

void ComboComponent::SelectHighlighted() {
    if (highlighted_index_ < 0 || highlighted_index_ >= static_cast<int>(items_.size())) return;
    selected_index_ = static_cast<std::size_t>(highlighted_index_);
    Dispatch("changed");
    ClosePopup();
}

void ComboComponent::MoveHighlight(int delta) {
    if (items_.empty()) {
        highlighted_index_ = -1;
        return;
    }
    highlighted_index_ = std::clamp(highlighted_index_ + delta, 0,
                                    static_cast<int>(items_.size()) - 1);
    EnsureHighlightVisible();
}

void ComboComponent::EnsureHighlightVisible() {
    if (highlighted_index_ < 0) {
        first_visible_index_ = 0;
        return;
    }
    if (highlighted_index_ < first_visible_index_) first_visible_index_ = highlighted_index_;
    if (highlighted_index_ >= first_visible_index_ + visible_rows_) {
        first_visible_index_ = highlighted_index_ - visible_rows_ + 1;
    }
    first_visible_index_ = std::clamp(
        first_visible_index_, 0,
        std::max(0, static_cast<int>(items_.size()) - visible_rows_));
}

void ComboComponent::Dispatch(std::string_view name) {
    config::EventPayloadValue selected_index;
    selected_index.value = selected_index_ ? static_cast<std::int64_t>(*selected_index_) : -1;
    config::EventPayloadValue selected_value;
    selected_value.value = selected_index_ && *selected_index_ < items_.size()
                               ? WideToUtf8(items_[*selected_index_])
                               : std::string{};
    EmitEvent(name, {{"selectedIndex", std::move(selected_index)},
                     {"selectedValue", std::move(selected_value)}});
}

config::VisualState ComboComponent::State() const noexcept {
    if (!enabled()) return config::VisualState::Disabled;
    if (pressed_ || popup_open_) return config::VisualState::Pressed;
    if (focused_ && window_active_) return config::VisualState::Focus;
    if (hovered_) return config::VisualState::Hover;
    return config::VisualState::Normal;
}

LRESULT CALLBACK ComboComponent::PopupProcedure(HWND window, UINT message, WPARAM wparam,
                                                 LPARAM lparam) {
    auto* owner = reinterpret_cast<ComboComponent*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        owner = static_cast<ComboComponent*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
        if (owner) owner->popup_ = window;
    }
    return owner ? owner->HandlePopupMessage(message, wparam, lparam)
                 : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT ComboComponent::HandlePopupMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_GETOBJECT:
            if (host_.return_popup_automation_provider &&
                static_cast<LONG>(lparam) == UiaRootObjectId) {
                return host_.return_popup_automation_provider(this, popup_, wparam, lparam);
            }
            break;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_NCHITTEST: return HTCLIENT;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, popup_, 0};
            TrackMouseEvent(&tracking);
            const int next = ItemAt({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            if (next != highlighted_index_) {
                highlighted_index_ = next;
                RenderPopup();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(popup_);
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == popup_) ReleaseCapture();
            highlighted_index_ = ItemAt({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            SelectHighlighted();
            return 0;
        case WM_MOUSEWHEEL:
            if (GET_WHEEL_DELTA_WPARAM(wparam) > 0) MoveHighlight(-1);
            else MoveHighlight(1);
            RenderPopup();
            return 0;
        case WM_DPICHANGED:
            PositionAndRenderPopup(HIWORD(wparam));
            return 0;
        default:
            break;
    }
    return DefWindowProcW(popup_, message, wparam, lparam);
}

}  // namespace ui::components
