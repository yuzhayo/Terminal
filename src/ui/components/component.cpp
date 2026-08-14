#include "ui/components/component.h"

#include <algorithm>
#include <stdexcept>

namespace ui::components {
namespace {

std::size_t StateIndex(config::VisualState state) noexcept {
    return static_cast<std::size_t>(state);
}

int ResolveDimension(const config::Dimension& dimension, int measured, int available) noexcept {
    switch (dimension.kind) {
        case config::DimensionKind::Auto: return measured;
        case config::DimensionKind::Fill: return available;
        case config::DimensionKind::Pixels: return dimension.pixels;
    }
    return measured;
}

}  // namespace

Component::Component(const config::ResolvedComponent& definition, ComponentHost& host)
    : definition_(definition), host_(host) {
    if (!host_.render_runtime || !host_.theme) {
        throw std::invalid_argument("Component host requires renderer and theme.");
    }
}

void Component::Arrange(const RECT& bounds) {
    bounds_ = bounds;
}

Component* Component::HitTest(POINT point) {
    if (!visible() || !PointInRectInclusive(bounds_, point)) return nullptr;
    for (auto item = children_.rbegin(); item != children_.rend(); ++item) {
        if (Component* hit = (*item)->HitTest(point)) return hit;
    }
    return this;
}

bool Component::PointerMove(POINT) {
    return false;
}

bool Component::PointerDown(POINT) {
    return false;
}

bool Component::PointerUp(POINT) {
    return false;
}

bool Component::HandleCommand(HWND source, WORD notification) {
    for (const auto& child : children_) {
        if (child->HandleCommand(source, notification)) return true;
    }
    return false;
}

HBRUSH Component::HandleControlColor(HDC dc, HWND source) {
    for (const auto& child : children_) {
        if (HBRUSH brush = child->HandleControlColor(dc, source)) return brush;
    }
    return nullptr;
}

bool Component::OwnsNativePeer(HWND source) const noexcept {
    for (const auto& child : children_) {
        if (child->OwnsNativePeer(source)) return true;
    }
    return false;
}

bool Component::CanFocus() const noexcept {
    return false;
}

bool Component::FocusNativePeer() {
    return false;
}

void Component::SetLogicalFocus(bool focused, bool window_active) {
    (void)focused;
    (void)window_active;
}

bool Component::HandleKeyDown(UINT virtual_key) {
    (void)virtual_key;
    return false;
}

void Component::CollectFocusable(std::vector<Component*>& focusable) {
    if (CanFocus()) focusable.push_back(this);
    for (const auto& child : children_) child->CollectFocusable(focusable);
}

bool Component::SuspendNativePeers(std::wstring& diagnostic) {
    std::size_t suspended = 0;
    for (; suspended < children_.size(); ++suspended) {
        if (!children_[suspended]->SuspendNativePeers(diagnostic)) {
            while (suspended > 0) children_[--suspended]->ResumeNativePeers();
            return false;
        }
    }
    diagnostic.clear();
    return true;
}

void Component::ResumeNativePeers() {
    for (auto child = children_.rbegin(); child != children_.rend(); ++child) {
        (*child)->ResumeNativePeers();
    }
}

void Component::CollectEditableParticipants(std::vector<EditableParticipant*>& participants) {
    for (const auto& child : children_) child->CollectEditableParticipants(participants);
}

void Component::OnDpiChanged() {
    for (const auto& child : children_) child->OnDpiChanged();
}

void Component::AddChild(std::unique_ptr<Component> child) {
    if (child) children_.push_back(std::move(child));
}

const RECT& Component::bounds() const noexcept {
    return bounds_;
}

const config::ResolvedComponent& Component::definition() const noexcept {
    return definition_;
}

const config::ResolvedStyle& Component::style() const {
    if (definition_.style_index >= host_.theme->styles.size()) {
        throw std::out_of_range("Resolved component style index is invalid.");
    }
    return host_.theme->styles[definition_.style_index];
}

bool Component::visible() const noexcept {
    return definition_.visible;
}

bool Component::enabled() const noexcept {
    return definition_.enabled;
}

void Component::PaintStyleBox(HDC dc, config::VisualState state, const RECT& bounds) const {
    const config::ResolvedStyle& resolved_style = style();
    const config::ResolvedVisualState& visual = resolved_style.states[StateIndex(state)];
    const rendering::RgbaColor background = host_.render_runtime->ResolveColor(visual.background);
    const rendering::RgbaColor border = host_.render_runtime->ResolveColor(visual.border);
    const int radius = ScaleDip(resolved_style.radius, host_.dpi);
    const int border_width = ScaleDip(resolved_style.border_width, host_.dpi);

    HGDIOBJ previous_brush = SelectObject(
        dc, background.alpha == 0 ? GetStockObject(HOLLOW_BRUSH)
                                  : host_.render_runtime->Brush(rendering::ToColorRef(background)));
    HGDIOBJ previous_pen = SelectObject(
        dc, border.alpha == 0 || border_width <= 0
                ? GetStockObject(NULL_PEN)
                : host_.render_runtime->Pen(rendering::ToColorRef(border), border_width));
    if (radius > 0) {
        RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, radius * 2, radius * 2);
    } else {
        Rectangle(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    }
    SelectObject(dc, previous_pen);
    SelectObject(dc, previous_brush);
}

void Component::PaintChildren(HDC dc) {
    for (const auto& child : children_) {
        if (child->visible()) child->Paint(dc);
    }
}

MeasuredSize Component::ApplyConstraints(MeasuredSize measured, int available_width,
                                         int available_height) const noexcept {
    const config::LayoutDefinition& layout = definition_.layout;
    measured.width = ResolveDimension(layout.width, measured.width, available_width);
    measured.height = ResolveDimension(layout.height, measured.height, available_height);
    measured.width = std::clamp(measured.width, ScaleDip(layout.minimum_width, host_.dpi),
                                ScaleDip(layout.maximum_width, host_.dpi));
    measured.height = std::clamp(measured.height, ScaleDip(layout.minimum_height, host_.dpi),
                                 ScaleDip(layout.maximum_height, host_.dpi));
    return measured;
}

void Component::Invalidate() const {
    if (host_.invalidate) host_.invalidate(bounds_);
}

int ScaleDip(int value, UINT dpi) noexcept {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

std::wstring ResolveText(const config::TextValue& value) {
    const auto* literal = std::get_if<std::string>(&value);
    if (!literal || literal->empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, literal->data(),
                                          static_cast<int>(literal->size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, literal->data(),
                        static_cast<int>(literal->size()), result.data(), count);
    return result;
}

bool PointInRectInclusive(const RECT& bounds, POINT point) noexcept {
    return point.x >= bounds.left && point.x < bounds.right && point.y >= bounds.top &&
           point.y < bounds.bottom;
}

}  // namespace ui::components
