#include "ui/components/list/list_component.h"

#include <algorithm>
#include <cmath>

#include "rendering/window_render_context.h"

namespace ui::components {
namespace {

std::size_t StateIndex(config::VisualState state) noexcept {
    return static_cast<std::size_t>(state);
}

}  // namespace

ListRealizationRange CalculateListRealizationRange(std::size_t item_count, int row_height,
                                                    int scroll_value, int viewport_height,
                                                    int overscan_rows) noexcept {
    if (item_count == 0 || row_height <= 0 || viewport_height <= 0) return {};
    const int scroll = std::max(0, scroll_value);
    const std::size_t visible_first = std::min(
        item_count, static_cast<std::size_t>(scroll / row_height));
    const std::size_t visible_end = std::min(
        item_count, static_cast<std::size_t>((scroll + viewport_height + row_height - 1) /
                                             row_height));
    const std::size_t overscan = static_cast<std::size_t>(std::max(0, overscan_rows));
    const std::size_t first = visible_first > overscan ? visible_first - overscan : 0;
    const std::size_t end = std::min(item_count, visible_end + overscan);
    return {first, end > first ? end - first : 0};
}

ListComponent::ListComponent(const config::ResolvedComponent& definition, ComponentHost& host)
    : Component(definition, host) {
    RefreshItems();
    if (Properties().scrollbar == config::ScrollbarMode::Auto) EnsureScrollbar();
}

const config::ListProperties& ListComponent::Properties() const {
    return std::get<config::ListProperties>(definition_.properties);
}

void ListComponent::EnsureScrollbar() {
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
    properties.line_step = std::max(1, RowHeight());
    scrollbar_definition_.properties = properties;
    scrollbar_ = std::make_unique<ScrollbarComponent>(scrollbar_definition_, host_);
    scrollbar_->Bind(this);
}

void ListComponent::RefreshItems() {
    items_ = host_.resolve_string_items
                 ? host_.resolve_string_items(Properties().items_binding.path)
                 : std::vector<std::wstring>{};
    selected_index_.reset();
    if (Properties().selected_id_binding && host_.resolve_string_value) {
        const auto selected = host_.resolve_string_value(Properties().selected_id_binding->path);
        if (selected) {
            const auto found = std::find(items_.begin(), items_.end(), *selected);
            if (found != items_.end()) {
                selected_index_ = static_cast<std::size_t>(found - items_.begin());
            }
        }
    }
}

MeasuredSize ListComponent::Measure(HDC dc, int available_width, int available_height) {
    (void)dc;
    const int rows = std::max(1, std::min(6, static_cast<int>(items_.size())));
    const int desired_width = std::min(available_width, ScaleDip(320, host_.dpi));
    return ApplyConstraints({desired_width, rows * RowHeight()}, available_width, available_height);
}

void ListComponent::Arrange(const RECT& bounds) {
    Component::Arrange(bounds);
    viewport_ = bounds;
    viewport_extent_ = std::max(0, static_cast<int>(bounds.bottom - bounds.top));
    content_extent_ = static_cast<int>(items_.size()) * RowHeight();
    scroll_value_ = std::clamp(scroll_value_, ScrollMinimum(), ScrollMaximum());
    scrollbar_visible_ = scrollbar_ && ScrollMaximum() > ScrollMinimum();
    if (scrollbar_visible_) {
        const auto& scrollbar_properties =
            std::get<config::ScrollbarProperties>(scrollbar_definition_.properties);
        const int thickness = ScaleDip(scrollbar_properties.thickness, host_.dpi);
        viewport_.right = std::max(viewport_.left, viewport_.right - thickness);
        scrollbar_->Arrange({viewport_.right, bounds.top, bounds.right, bounds.bottom});
    }
    RealizeVisibleRows();
}

void ListComponent::RealizeVisibleRows() {
    realized_rows_.clear();
    const ListRealizationRange range = CalculateListRealizationRange(
        items_.size(), RowHeight(), scroll_value_, viewport_extent_, Properties().overscan_rows);
    realized_rows_.reserve(range.count);
    for (std::size_t index = range.first; index < range.first + range.count; ++index) {
        const int top = viewport_.top + static_cast<int>(index) * RowHeight() - scroll_value_;
        realized_rows_.push_back({index, {viewport_.left, top, viewport_.right, top + RowHeight()}});
    }
}

void ListComponent::Paint(HDC dc) {
    config::VisualState state = config::VisualState::Normal;
    if (!enabled()) state = config::VisualState::Disabled;
    else if (focused_ && window_active_) state = config::VisualState::Focus;
    PaintStyleBox(dc, state, bounds_);

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, viewport_.left, viewport_.top, viewport_.right, viewport_.bottom);
    if (items_.empty()) {
        RECT empty_bounds = viewport_;
        empty_bounds.left += ScaleDip(12, host_.dpi);
        empty_bounds.right -= ScaleDip(12, host_.dpi);
        const auto& visual = style().states[StateIndex(enabled() ? config::VisualState::Normal
                                                                : config::VisualState::Disabled)];
        const auto foreground = host_.render_runtime->ResolveColor(visual.foreground);
        host_.render_runtime->DrawTextRun(
            dc, Utf8ToWide(Properties().empty_text), style().font, host_.dpi, empty_bounds,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
            rendering::ToColorRef(foreground));
    } else {
        for (const RealizedRow& row : realized_rows_) {
            config::VisualState row_state = config::VisualState::Normal;
            if (!enabled()) row_state = config::VisualState::Disabled;
            else if (pressed_index_ == static_cast<int>(row.item_index)) {
                row_state = config::VisualState::Pressed;
            } else if (selected_index_ && *selected_index_ == row.item_index) {
                row_state = config::VisualState::Selected;
            } else if (hovered_index_ == static_cast<int>(row.item_index)) {
                row_state = config::VisualState::Hover;
            }
            PaintRow(dc, row, row_state);
        }
    }
    if (saved != 0) RestoreDC(dc, saved);
    if (scrollbar_visible_) scrollbar_->Paint(dc);
}

void ListComponent::PaintRow(HDC dc, const RealizedRow& row, config::VisualState state) {
    const config::ResolvedComponent& item_template = *Properties().item_template;
    const config::ResolvedStyle& row_style = host_.theme->styles[item_template.style_index];
    const config::ResolvedVisualState& visual = row_style.states[StateIndex(state)];
    const auto background = host_.render_runtime->ResolveColor(visual.background);
    const auto border = host_.render_runtime->ResolveColor(visual.border);
    const int radius = ScaleDip(row_style.radius, host_.dpi);
    const int border_width = ScaleDip(row_style.border_width, host_.dpi);
    if ((background.alpha < 255 || border.alpha < 255) && host_.render_context) {
        host_.render_context->SourceOverRounded(row.bounds, radius, border_width, background, border);
    } else {
        COLORREF parent_background = GetPixel(dc, row.bounds.left, row.bounds.top);
        if (parent_background == CLR_INVALID) parent_background = GetSysColor(COLOR_WINDOW);
        host_.render_runtime->PaintRoundedStyleBox(
            dc, row.bounds, radius, border_width, background, border, parent_background,
            host_.dpi, static_cast<unsigned int>(state));
    }
    RECT text_bounds = row.bounds;
    text_bounds.left += ScaleDip(row_style.content_padding.left + 10, host_.dpi);
    text_bounds.right -= ScaleDip(row_style.content_padding.right + 10, host_.dpi);
    const auto foreground = host_.render_runtime->ResolveColor(visual.foreground);
    host_.render_runtime->DrawTextRun(
        dc, items_[row.item_index], row_style.font, host_.dpi, text_bounds,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
        rendering::ToColorRef(foreground));
}

Component* ListComponent::HitTest(POINT point) {
    if (!visible() || !PointInRectInclusive(bounds_, point)) return nullptr;
    if (scrollbar_visible_ && scrollbar_->HitTest(point)) return scrollbar_.get();
    return this;
}

int ListComponent::ItemAt(POINT point) const noexcept {
    if (!PointInRectInclusive(viewport_, point) || items_.empty()) return -1;
    const int index = (point.y - viewport_.top + scroll_value_) / RowHeight();
    return index >= 0 && index < static_cast<int>(items_.size()) ? index : -1;
}

bool ListComponent::PointerMove(POINT point) {
    const int hovered = enabled() ? ItemAt(point) : -1;
    if (hovered == hovered_index_) return false;
    hovered_index_ = hovered;
    Invalidate();
    return true;
}

bool ListComponent::PointerDown(POINT point) {
    if (!enabled()) return false;
    pressed_index_ = ItemAt(point);
    if (pressed_index_ < 0) return false;
    SetCapture(host_.window);
    Invalidate();
    return true;
}

bool ListComponent::PointerUp(POINT point) {
    if (pressed_index_ < 0) return false;
    const int pressed = pressed_index_;
    pressed_index_ = -1;
    if (GetCapture() == host_.window) ReleaseCapture();
    if (pressed == ItemAt(point) && Properties().selection == config::SelectionMode::Single) {
        Select(static_cast<std::size_t>(pressed), true);
    } else {
        Invalidate();
    }
    return true;
}

bool ListComponent::PointerWheel(int delta) {
    if (ScrollMaximum() <= ScrollMinimum() || delta == 0) return false;
    const int notches = std::max(1, std::abs(delta) / WHEEL_DELTA);
    SetScrollValue(scroll_value_ + (delta > 0 ? -1 : 1) * RowHeight() * 3 * notches);
    return true;
}

bool ListComponent::CanFocus() const noexcept {
    return visible() && enabled() && Properties().tab_stop;
}

bool ListComponent::FocusNativePeer() {
    SetFocus(host_.window);
    return GetFocus() == host_.window;
}

void ListComponent::SetLogicalFocus(bool focused, bool window_active) {
    if (focused_ == focused && window_active_ == window_active) return;
    focused_ = focused;
    window_active_ = window_active;
    Invalidate();
}

bool ListComponent::HandleKeyDown(UINT key) {
    if (!focused_ || !window_active_ || !enabled() || items_.empty()) return false;
    if (key == VK_RETURN && selected_index_) {
        Dispatch("activate");
        return true;
    }
    if (key != VK_UP && key != VK_DOWN && key != VK_HOME && key != VK_END &&
        key != VK_PRIOR && key != VK_NEXT) return false;

    if (Properties().selection == config::SelectionMode::None) {
        int delta = RowHeight();
        if (key == VK_UP) delta = -delta;
        else if (key == VK_PRIOR) delta = -std::max(RowHeight(), viewport_extent_);
        else if (key == VK_NEXT) delta = std::max(RowHeight(), viewport_extent_);
        else if (key == VK_HOME) {
            SetScrollValue(ScrollMinimum());
            return true;
        } else if (key == VK_END) {
            SetScrollValue(ScrollMaximum());
            return true;
        }
        SetScrollValue(scroll_value_ + delta);
        return true;
    }

    std::size_t next = selected_index_.value_or(key == VK_UP || key == VK_END
                                                    ? items_.size() - 1
                                                    : 0);
    const std::size_t page_rows = static_cast<std::size_t>(
        std::max(1, viewport_extent_ / std::max(1, RowHeight())));
    if (selected_index_) {
        if (key == VK_UP && next > 0) --next;
        else if (key == VK_DOWN && next + 1 < items_.size()) ++next;
        else if (key == VK_HOME) next = 0;
        else if (key == VK_END) next = items_.size() - 1;
        else if (key == VK_PRIOR) next = next > page_rows ? next - page_rows : 0;
        else if (key == VK_NEXT) next = std::min(items_.size() - 1, next + page_rows);
    }
    Select(next, true);
    return true;
}

void ListComponent::Select(std::size_t index, bool dispatch_event) {
    if (index >= items_.size() || Properties().selection == config::SelectionMode::None) return;
    const bool changed = !selected_index_ || *selected_index_ != index;
    selected_index_ = index;
    EnsureSelectedVisible();
    Invalidate();
    if (changed && dispatch_event) Dispatch("selectionChanged");
}

void ListComponent::EnsureSelectedVisible() {
    if (!selected_index_) return;
    const int top = static_cast<int>(*selected_index_) * RowHeight();
    const int bottom = top + RowHeight();
    if (top < scroll_value_) SetScrollValue(top);
    else if (bottom > scroll_value_ + viewport_extent_) {
        SetScrollValue(bottom - viewport_extent_);
    }
}

void ListComponent::Dispatch(std::string_view name) {
    config::EventPayloadValue selected_index;
    selected_index.value = selected_index_ ? static_cast<std::int64_t>(*selected_index_) : -1;
    config::EventPayloadValue selected_value;
    selected_value.value = selected_index_ && *selected_index_ < items_.size()
                               ? WideToUtf8(items_[*selected_index_])
                               : std::string{};
    EmitEvent(name, {{"selectedIndex", std::move(selected_index)},
                     {"selectedValue", std::move(selected_value)}});
}

void ListComponent::CollectFocusable(std::vector<Component*>& focusable) {
    Component::CollectFocusable(focusable);
    if (scrollbar_ && scrollbar_visible_) focusable.push_back(scrollbar_.get());
}

void ListComponent::CollectAutomationElements(std::vector<Component*>& elements) {
    Component::CollectAutomationElements(elements);
    if (scrollbar_ && scrollbar_visible_) scrollbar_->CollectAutomationElements(elements);
}

void ListComponent::OnDpiChanged() {
    if (scrollbar_) {
        auto& properties = std::get<config::ScrollbarProperties>(scrollbar_definition_.properties);
        properties.line_step = std::max(1, RowHeight());
        scrollbar_->OnDpiChanged();
    }
    Arrange(bounds_);
}

bool ListComponent::PrepareResources(COLORREF parent_background) {
    if (!Component::PrepareResources(parent_background)) return false;
    const auto normal = host_.render_runtime->ResolveColor(
        style().states[StateIndex(config::VisualState::Normal)].background);
    const COLORREF row_parent = rendering::CompositeOverOpaque(parent_background, normal);
    const config::ResolvedStyle& row_style =
        host_.theme->styles[Properties().item_template->style_index];
    if (!host_.render_runtime->PrepareStyleResources(row_style, host_.dpi, row_parent)) return false;
    return !scrollbar_ || scrollbar_->PrepareResources(row_parent);
}

AutomationRole ListComponent::automation_role() const noexcept {
    return AutomationRole::List;
}

bool ListComponent::automation_supports_item_container() const noexcept { return true; }
bool ListComponent::automation_supports_selection() const noexcept {
    return Properties().selection == config::SelectionMode::Single;
}
std::size_t ListComponent::automation_item_count() const noexcept { return items_.size(); }
std::wstring ListComponent::automation_item_name(std::size_t index) const {
    return index < items_.size() ? items_[index] : std::wstring{};
}
std::optional<RECT> ListComponent::automation_item_screen_bounds(
    std::size_t index) const noexcept {
    const auto found = std::find_if(realized_rows_.begin(), realized_rows_.end(),
                                    [index](const RealizedRow& row) {
                                        return row.item_index == index;
                                    });
    if (found == realized_rows_.end()) return std::nullopt;
    RECT clipped{};
    if (!IntersectRect(&clipped, &found->bounds, &viewport_)) return std::nullopt;
    POINT origin{clipped.left, clipped.top};
    if (!ClientToScreen(host_.window, &origin)) return std::nullopt;
    return RECT{origin.x, origin.y, origin.x + clipped.right - clipped.left,
                origin.y + clipped.bottom - clipped.top};
}
bool ListComponent::automation_item_realized(std::size_t index) const noexcept {
    return std::any_of(realized_rows_.begin(), realized_rows_.end(),
                       [index](const RealizedRow& row) { return row.item_index == index; });
}
bool ListComponent::automation_item_selected(std::size_t index) const noexcept {
    return selected_index_ && *selected_index_ == index;
}
bool ListComponent::AutomationSelectItem(std::size_t index) {
    if (!enabled() || index >= items_.size() ||
        Properties().selection != config::SelectionMode::Single) return false;
    Select(index, true);
    RequestAutomationFocus();
    return true;
}
bool ListComponent::AutomationRealizeItem(std::size_t index) {
    if (index >= items_.size()) return false;
    const int top = static_cast<int>(index) * RowHeight();
    const int bottom = top + RowHeight();
    if (top < scroll_value_) SetScrollValue(top);
    else if (bottom > scroll_value_ + viewport_extent_) {
        SetScrollValue(bottom - viewport_extent_);
    }
    return automation_item_realized(index);
}
std::optional<AutomationScrollState> ListComponent::automation_scroll_state() const noexcept {
    const int maximum = ScrollMaximum();
    const int content = std::max(1, content_extent_);
    return AutomationScrollState{
        false, -1.0, 100.0, maximum > 0,
        maximum > 0 ? 100.0 * scroll_value_ / maximum : -1.0,
        std::min(100.0, 100.0 * viewport_extent_ / content)};
}
bool ListComponent::AutomationScrollVertical(AutomationScrollAmount amount) {
    if (ScrollMaximum() <= 0 || amount == AutomationScrollAmount::NoAmount) return false;
    int delta = RowHeight();
    if (amount == AutomationScrollAmount::LargeDecrement ||
        amount == AutomationScrollAmount::LargeIncrement) {
        delta = std::max(RowHeight(), viewport_extent_);
    }
    if (amount == AutomationScrollAmount::LargeDecrement ||
        amount == AutomationScrollAmount::SmallDecrement) delta = -delta;
    const int previous = scroll_value_;
    SetScrollValue(scroll_value_ + delta);
    return scroll_value_ != previous;
}
bool ListComponent::AutomationSetVerticalScrollPercent(double percent) {
    if (!std::isfinite(percent) || percent < 0.0 || percent > 100.0 ||
        ScrollMaximum() <= 0) return false;
    SetScrollValue(static_cast<int>(std::lround(ScrollMaximum() * percent / 100.0)));
    return true;
}

int ListComponent::ScrollMinimum() const noexcept { return 0; }
int ListComponent::ScrollMaximum() const noexcept {
    return std::max(0, content_extent_ - viewport_extent_);
}
int ListComponent::ScrollPageSize() const noexcept { return viewport_extent_; }
int ListComponent::ScrollValue() const noexcept { return scroll_value_; }

void ListComponent::SetScrollValue(int value) {
    const int clamped = std::clamp(value, ScrollMinimum(), ScrollMaximum());
    if (clamped == scroll_value_) return;
    scroll_value_ = clamped;
    RealizeVisibleRows();
    if (scrollbar_) scrollbar_->Refresh();
    Invalidate();
}

std::size_t ListComponent::realized_row_count() const noexcept {
    return realized_rows_.size();
}

ListRealizationRange ListComponent::realized_range() const noexcept {
    return realized_rows_.empty()
               ? ListRealizationRange{}
               : ListRealizationRange{realized_rows_.front().item_index, realized_rows_.size()};
}

std::optional<std::size_t> ListComponent::selected_index() const noexcept {
    return selected_index_;
}

int ListComponent::RowHeight() const noexcept {
    return std::max(1, ScaleDip(Properties().row_height, host_.dpi));
}

}  // namespace ui::components
