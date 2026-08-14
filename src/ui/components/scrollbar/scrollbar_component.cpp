#include "ui/components/scrollbar/scrollbar_component.h"

#include <algorithm>
#include <cmath>

namespace ui::components {

ScrollbarMetrics CalculateScrollbarMetrics(int track_length, int minimum_thumb_length,
                                            int minimum, int maximum, int page_size,
                                            int value) noexcept {
    ScrollbarMetrics metrics{};
    metrics.track_length = std::max(0, track_length);
    if (metrics.track_length == 0) return metrics;
    const int range = std::max(0, maximum - minimum);
    const int page = std::max(0, page_size);
    if (range == 0) {
        metrics.thumb_length = metrics.track_length;
        return metrics;
    }
    const int content = range + page;
    const double fraction = content > 0 ? static_cast<double>(page) / content : 0.0;
    metrics.thumb_length = std::clamp(
        static_cast<int>(std::lround(metrics.track_length * fraction)),
        std::min(metrics.track_length, std::max(1, minimum_thumb_length)), metrics.track_length);
    const int travel = metrics.track_length - metrics.thumb_length;
    const int clamped = std::clamp(value, minimum, maximum);
    metrics.thumb_start = travel == 0 ? 0 :
        static_cast<int>(std::lround(static_cast<double>(clamped - minimum) * travel / range));
    return metrics;
}

void ScrollbarComponent::Bind(ScrollModel* model) noexcept {
    model_ = model;
}

void ScrollbarComponent::Refresh() {
    Invalidate();
}

const config::ScrollbarProperties& ScrollbarComponent::Properties() const {
    return std::get<config::ScrollbarProperties>(definition_.properties);
}

MeasuredSize ScrollbarComponent::Measure(HDC dc, int available_width, int available_height) {
    (void)dc;
    const int thickness = ScaleDip(Properties().thickness, host_.dpi);
    const bool vertical = Properties().orientation == config::Orientation::Vertical;
    return ApplyConstraints(vertical ? MeasuredSize{thickness, available_height}
                                     : MeasuredSize{available_width, thickness},
                            available_width, available_height);
}

void ScrollbarComponent::Paint(HDC dc) {
    config::VisualState track_state = config::VisualState::Normal;
    if (!enabled() || !model_ || model_->ScrollMaximum() <= model_->ScrollMinimum()) {
        track_state = config::VisualState::Disabled;
    } else if (focused_ && window_active_) {
        track_state = config::VisualState::Focus;
    } else if (hovered_) {
        track_state = config::VisualState::Hover;
    }
    PaintStyleBox(dc, track_state, bounds_);
    const RECT thumb = ThumbBounds();
    if (thumb.right <= thumb.left || thumb.bottom <= thumb.top) return;
    config::VisualState thumb_state = config::VisualState::Selected;
    if (pressed_) thumb_state = config::VisualState::Pressed;
    else if (thumb_hovered_) thumb_state = config::VisualState::Hover;
    const auto& visual = style().states[static_cast<std::size_t>(thumb_state)];
    const COLORREF color = rendering::ToColorRef(host_.render_runtime->ResolveColor(visual.foreground));
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ previous_brush = SelectObject(dc, brush);
    HGDIOBJ previous_pen = SelectObject(dc, pen);
    const int radius = std::max(2, ScaleDip(style().radius, host_.dpi));
    RoundRect(dc, thumb.left, thumb.top, thumb.right, thumb.bottom, radius * 2, radius * 2);
    SelectObject(dc, previous_pen);
    SelectObject(dc, previous_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

bool ScrollbarComponent::PointerMove(POINT point) {
    if (dragging_ && model_) {
        const ScrollbarMetrics metrics = CalculateScrollbarMetrics(
            PrimaryLength(), ScaleDip(Properties().minimum_thumb_length, host_.dpi),
            model_->ScrollMinimum(), model_->ScrollMaximum(), model_->ScrollPageSize(),
            drag_value_origin_);
        const int travel = metrics.track_length - metrics.thumb_length;
        const int range = model_->ScrollMaximum() - model_->ScrollMinimum();
        if (travel > 0 && range > 0) {
            const int pointer_delta = PrimaryCoordinate(point) - drag_pointer_origin_;
            model_->SetScrollValue(drag_value_origin_ + MulDiv(pointer_delta, range, travel));
        }
        return true;
    }
    const bool hovered = enabled() && PointInRectInclusive(bounds_, point);
    const bool thumb_hovered = hovered && PointInRectInclusive(ThumbBounds(), point);
    if (hovered == hovered_ && thumb_hovered == thumb_hovered_) return false;
    hovered_ = hovered;
    thumb_hovered_ = thumb_hovered;
    Invalidate();
    return true;
}

bool ScrollbarComponent::PointerDown(POINT point) {
    if (!enabled() || !model_ || !PointInRectInclusive(bounds_, point) ||
        model_->ScrollMaximum() <= model_->ScrollMinimum()) return false;
    pressed_ = true;
    const RECT thumb = ThumbBounds();
    dragging_ = PointInRectInclusive(thumb, point);
    if (dragging_) {
        drag_pointer_origin_ = PrimaryCoordinate(point);
        drag_value_origin_ = model_->ScrollValue();
    } else {
        const int page = Properties().page_step.value_or(std::max(1, model_->ScrollPageSize()));
        StepBy(PrimaryCoordinate(point) <
                       (Properties().orientation == config::Orientation::Vertical ? thumb.top : thumb.left)
                   ? -page
                   : page);
    }
    SetCapture(host_.window);
    Invalidate();
    return true;
}

bool ScrollbarComponent::PointerUp(POINT point) {
    if (!pressed_) return false;
    pressed_ = false;
    dragging_ = false;
    if (GetCapture() == host_.window) ReleaseCapture();
    PointerMove(point);
    Invalidate();
    return true;
}

bool ScrollbarComponent::PointerWheel(int delta) {
    if (!model_ || delta == 0) return false;
    const int notches = std::max(1, std::abs(delta) / WHEEL_DELTA);
    StepBy((delta > 0 ? -1 : 1) * Properties().line_step * 3 * notches);
    return true;
}

bool ScrollbarComponent::CanFocus() const noexcept {
    return visible() && enabled() && model_ && model_->ScrollMaximum() > model_->ScrollMinimum();
}

bool ScrollbarComponent::FocusNativePeer() {
    SetFocus(host_.window);
    return GetFocus() == host_.window;
}

void ScrollbarComponent::SetLogicalFocus(bool focused, bool window_active) {
    if (focused_ == focused && window_active_ == window_active) return;
    focused_ = focused;
    window_active_ = window_active;
    Invalidate();
}

bool ScrollbarComponent::HandleKeyDown(UINT virtual_key) {
    if (!focused_ || !window_active_ || !model_) return false;
    const bool vertical = Properties().orientation == config::Orientation::Vertical;
    if (virtual_key == static_cast<UINT>(vertical ? VK_UP : VK_LEFT)) {
        StepBy(-Properties().line_step);
    } else if (virtual_key == static_cast<UINT>(vertical ? VK_DOWN : VK_RIGHT)) {
        StepBy(Properties().line_step);
    }
    else if (virtual_key == VK_PRIOR) StepBy(-Properties().page_step.value_or(model_->ScrollPageSize()));
    else if (virtual_key == VK_NEXT) StepBy(Properties().page_step.value_or(model_->ScrollPageSize()));
    else if (virtual_key == VK_HOME) model_->SetScrollValue(model_->ScrollMinimum());
    else if (virtual_key == VK_END) model_->SetScrollValue(model_->ScrollMaximum());
    else return false;
    return true;
}

AutomationRole ScrollbarComponent::automation_role() const noexcept {
    return AutomationRole::Scrollbar;
}

std::optional<AutomationRangeValue> ScrollbarComponent::automation_range_value() const noexcept {
    if (!model_) return std::nullopt;
    return AutomationRangeValue{static_cast<double>(model_->ScrollValue()),
                                static_cast<double>(model_->ScrollMinimum()),
                                static_cast<double>(model_->ScrollMaximum()),
                                static_cast<double>(Properties().page_step.value_or(
                                    std::max(1, model_->ScrollPageSize()))),
                                static_cast<double>(Properties().line_step)};
}

bool ScrollbarComponent::AutomationSetRangeValue(double value) {
    if (!model_ || !enabled() || !std::isfinite(value)) return false;
    model_->SetScrollValue(static_cast<int>(std::lround(value)));
    return true;
}

RECT ScrollbarComponent::ThumbBounds() const noexcept {
    if (!model_) return {};
    const ScrollbarMetrics metrics = CalculateScrollbarMetrics(
        PrimaryLength(), ScaleDip(Properties().minimum_thumb_length, host_.dpi),
        model_->ScrollMinimum(), model_->ScrollMaximum(), model_->ScrollPageSize(),
        model_->ScrollValue());
    if (Properties().orientation == config::Orientation::Vertical) {
        return {bounds_.left, bounds_.top + metrics.thumb_start, bounds_.right,
                bounds_.top + metrics.thumb_start + metrics.thumb_length};
    }
    return {bounds_.left + metrics.thumb_start, bounds_.top,
            bounds_.left + metrics.thumb_start + metrics.thumb_length, bounds_.bottom};
}

int ScrollbarComponent::PrimaryCoordinate(POINT point) const noexcept {
    return Properties().orientation == config::Orientation::Vertical ? point.y : point.x;
}

int ScrollbarComponent::PrimaryLength() const noexcept {
    return Properties().orientation == config::Orientation::Vertical
               ? static_cast<int>(bounds_.bottom - bounds_.top)
               : static_cast<int>(bounds_.right - bounds_.left);
}

void ScrollbarComponent::StepBy(int delta) {
    if (model_) model_->SetScrollValue(model_->ScrollValue() + delta);
}

}  // namespace ui::components
