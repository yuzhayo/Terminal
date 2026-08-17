#include "ui/components/tabs/tabs_component.h"

#include <algorithm>

namespace ui::components {

void TabsComponent::RefreshItems(HDC dc) {
    const std::vector<RouteTabDefinition> routes =
        host_.resolve_route_tabs ? host_.resolve_route_tabs()
                                 : std::vector<RouteTabDefinition>{};
    bool unchanged = routes.size() == items_.size();
    if (unchanged) {
        for (std::size_t index = 0; index < routes.size(); ++index) {
            if (routes[index].route_id != items_[index].route_id ||
                routes[index].label != items_[index].label) {
                unchanged = false;
                break;
            }
        }
    }
    if (!unchanged) {
        items_.clear();
        items_.reserve(routes.size());
        for (const RouteTabDefinition& route : routes) {
            items_.push_back({route.route_id, route.label});
        }
        hovered_.reset();
        pressed_.reset();
    }

    const int horizontal_padding = ScaleDip(24, host_.dpi);
    const int minimum_width = ScaleDip(88, host_.dpi);
    for (Item& item : items_) {
        const SIZE text = host_.render_runtime->MeasureText(
            item.label, style().font, host_.dpi, 8192, DT_SINGLELINE | DT_NOPREFIX);
        item.natural_width = std::max(minimum_width,
                                      static_cast<int>(text.cx) + horizontal_padding);
    }
    (void)dc;
}

int TabsComponent::PreferredWidth(HDC dc) {
    RefreshItems(dc);
    int width = 0;
    for (const Item& item : items_) width += item.natural_width;
    return width;
}

MeasuredSize TabsComponent::Measure(HDC dc, int available_width, int available_height) {
    const int width = PreferredWidth(dc);
    const int height = std::max(ScaleDip(style().minimum_height, host_.dpi),
                                ScaleDip(36, host_.dpi));
    return ApplyConstraints({width, height}, available_width, available_height);
}

void TabsComponent::Arrange(const RECT& bounds) {
    Component::Arrange(bounds);
    RefreshItems(host_.layout_dc);
    if (items_.empty()) return;

    const int available = std::max(0, static_cast<int>(bounds.right - bounds.left));
    int natural_total = 0;
    for (const Item& item : items_) natural_total += item.natural_width;
    int cursor = bounds.left;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const int remaining = bounds.right - cursor;
        const int remaining_items = static_cast<int>(items_.size() - index);
        int width = natural_total <= available ? items_[index].natural_width
                                               : remaining / remaining_items;
        if (index + 1 == items_.size()) width = remaining;
        items_[index].bounds = {cursor, bounds.top, cursor + std::max(0, width), bounds.bottom};
        cursor = items_[index].bounds.right;
    }
}

void TabsComponent::Paint(HDC dc) {
    PaintStyleBox(dc, enabled() ? config::VisualState::Normal
                                : config::VisualState::Disabled,
                  bounds_);
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const config::VisualState state = ItemState(index);
        PaintStyleBox(dc, state, items_[index].bounds);
        const config::ResolvedVisualState& visual =
            style().states[static_cast<std::size_t>(state)];
        const rendering::RgbaColor foreground =
            host_.render_runtime->ResolveColor(visual.foreground);
        host_.render_runtime->DrawTextRun(
            dc, items_[index].label, style().font, host_.dpi, items_[index].bounds,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS,
            rendering::ToColorRef(foreground));
    }
}

bool TabsComponent::PointerMove(POINT point) {
    const std::optional<std::size_t> next = enabled() ? ItemAt(point) : std::nullopt;
    if (next == hovered_) return false;
    hovered_ = next;
    Invalidate();
    return true;
}

bool TabsComponent::PointerDown(POINT point) {
    if (!enabled()) return false;
    pressed_ = ItemAt(point);
    if (!pressed_) return false;
    SetCapture(host_.window);
    Invalidate();
    return true;
}

bool TabsComponent::PointerUp(POINT point) {
    if (!pressed_) return false;
    const std::optional<std::size_t> released = ItemAt(point);
    const std::size_t pressed = *pressed_;
    pressed_.reset();
    if (GetCapture() == host_.window) ReleaseCapture();
    Invalidate();
    if (released && *released == pressed && pressed < items_.size() && host_.request_route) {
        return host_.request_route(items_[pressed].route_id);
    }
    return true;
}

AutomationRole TabsComponent::automation_role() const noexcept {
    return AutomationRole::Group;
}

std::optional<std::size_t> TabsComponent::ItemAt(POINT point) const noexcept {
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (PointInRectInclusive(items_[index].bounds, point)) return index;
    }
    return std::nullopt;
}

config::VisualState TabsComponent::ItemState(std::size_t index) const noexcept {
    if (!enabled()) return config::VisualState::Disabled;
    if (pressed_ && *pressed_ == index) return config::VisualState::Pressed;
    const std::string_view active =
        host_.resolve_active_route ? host_.resolve_active_route() : std::string_view{};
    if (index < items_.size() && items_[index].route_id == active) {
        return config::VisualState::Selected;
    }
    if (hovered_ && *hovered_ == index) return config::VisualState::Hover;
    return config::VisualState::Normal;
}

}  // namespace ui::components
