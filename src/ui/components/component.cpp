#include "ui/components/component.h"

#include <algorithm>
#include <stdexcept>

#include "rendering/window_render_context.h"

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

bool Component::PointerWheel(int) {
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

bool Component::HasOpenPopup() const noexcept { return false; }
HWND Component::OwnedPopupHwnd() const noexcept { return nullptr; }
bool Component::OwnsPopupScopePoint(POINT) const noexcept { return false; }
void Component::DismissOwnedPopup() {}
bool Component::RequiresNativePeerSuppression() const noexcept { return false; }
bool Component::IsModalOverlay() const noexcept { return false; }
bool Component::IsModalActive() const noexcept { return false; }
bool Component::ActivateModal(std::wstring& diagnostic) {
    diagnostic = L"Component bukan modal overlay.";
    return false;
}
bool Component::DeactivateModal(std::wstring& diagnostic) {
    diagnostic = L"Component bukan modal overlay.";
    return false;
}
void Component::ArrangeModal(const RECT&) {}
void Component::PaintModalOverlay(rendering::WindowRenderContext&, const RECT&) {}
bool Component::CanCompleteModal(ModalResult) const noexcept { return true; }
void Component::CompleteModal(ModalResult) {}

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

void Component::CollectAutomationElements(std::vector<Component*>& elements) {
    if (automation_role() != AutomationRole::None) elements.push_back(this);
    for (const auto& child : children_) child->CollectAutomationElements(elements);
}

AutomationRole Component::automation_role() const noexcept { return AutomationRole::None; }

std::wstring Component::automation_name() const {
    return ResolveAutomationName(definition_, Utf8ToWide(definition_.id));
}
RECT Component::automation_bounds() const noexcept { return bounds_; }

bool Component::automation_supports_invoke() const noexcept { return false; }
bool Component::AutomationInvoke() { return false; }
std::optional<bool> Component::automation_toggle_state() const noexcept { return std::nullopt; }
bool Component::AutomationToggle() { return false; }
std::optional<bool> Component::automation_expanded() const noexcept { return std::nullopt; }
bool Component::AutomationExpand() { return false; }
bool Component::AutomationCollapse() { return false; }
std::optional<AutomationRangeValue> Component::automation_range_value() const noexcept {
    return std::nullopt;
}
bool Component::AutomationSetRangeValue(double) { return false; }
bool Component::automation_is_dialog() const noexcept { return false; }
bool Component::automation_is_modal() const noexcept { return false; }
bool Component::AutomationClose() { return false; }
bool Component::automation_supports_item_container() const noexcept { return false; }
bool Component::automation_supports_selection() const noexcept { return false; }
bool Component::automation_selection_required() const noexcept { return false; }
std::size_t Component::automation_item_count() const noexcept { return 0; }
std::wstring Component::automation_item_name(std::size_t) const { return {}; }
std::optional<RECT> Component::automation_item_screen_bounds(std::size_t) const noexcept {
    return std::nullopt;
}
bool Component::automation_item_realized(std::size_t) const noexcept { return false; }
bool Component::automation_item_selected(std::size_t) const noexcept { return false; }
bool Component::AutomationSelectItem(std::size_t) { return false; }
bool Component::AutomationRealizeItem(std::size_t) { return false; }
std::optional<AutomationScrollState> Component::automation_scroll_state() const noexcept {
    return std::nullopt;
}
bool Component::AutomationScrollVertical(AutomationScrollAmount) { return false; }
bool Component::AutomationSetVerticalScrollPercent(double) { return false; }
bool Component::automation_has_popup_fragment() const noexcept { return false; }
bool Component::automation_popup_visible() const noexcept { return false; }
HWND Component::automation_popup_hwnd() const noexcept { return nullptr; }
std::size_t Component::automation_popup_item_count() const noexcept { return 0; }
std::wstring Component::automation_popup_item_name(std::size_t) const { return {}; }
std::optional<RECT> Component::automation_popup_item_screen_bounds(std::size_t) const noexcept {
    return std::nullopt;
}
bool Component::automation_popup_item_realized(std::size_t) const noexcept { return false; }
bool Component::automation_popup_item_selected(std::size_t) const noexcept { return false; }
bool Component::AutomationSelectPopupItem(std::size_t) { return false; }
bool Component::AutomationRealizePopupItem(std::size_t) { return false; }
bool Component::RequestAutomationFocus() {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::Focus, this, 0.0)
               : FocusNativePeer();
}
bool Component::RequestAutomationInvoke() {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::Invoke, this, 0.0)
               : AutomationInvoke();
}
bool Component::RequestAutomationToggle() {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::Toggle, this, 0.0)
               : AutomationToggle();
}
bool Component::RequestAutomationExpand() {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::Expand, this, 0.0)
               : AutomationExpand();
}
bool Component::RequestAutomationCollapse() {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::Collapse, this, 0.0)
               : AutomationCollapse();
}
bool Component::RequestAutomationSetRangeValue(double value) {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::SetRangeValue, this, value)
               : AutomationSetRangeValue(value);
}
bool Component::RequestAutomationClose() {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::Close, this, 0.0)
               : AutomationClose();
}
bool Component::RequestAutomationSelectItem(std::size_t index) {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::SelectItem, this,
                                                  static_cast<double>(index))
               : AutomationSelectItem(index);
}
bool Component::RequestAutomationRealizeItem(std::size_t index) {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::RealizeItem, this,
                                                  static_cast<double>(index))
               : AutomationRealizeItem(index);
}
bool Component::RequestAutomationScrollVertical(AutomationScrollAmount amount) {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::ScrollVertical, this,
                                                  static_cast<double>(amount))
               : AutomationScrollVertical(amount);
}
bool Component::RequestAutomationSetVerticalScrollPercent(double percent) {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::SetVerticalScrollPercent,
                                                  this, percent)
               : AutomationSetVerticalScrollPercent(percent);
}
bool Component::RequestAutomationSelectPopupItem(std::size_t index) {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::SelectPopupItem, this,
                                                  static_cast<double>(index))
               : AutomationSelectPopupItem(index);
}
bool Component::RequestAutomationRealizePopupItem(std::size_t index) {
    return host_.request_automation_action
               ? host_.request_automation_action(AutomationAction::RealizePopupItem, this,
                                                  static_cast<double>(index))
               : AutomationRealizePopupItem(index);
}
HWND Component::automation_native_peer() const noexcept { return nullptr; }
bool Component::automation_is_password() const noexcept { return false; }

void Component::OnDpiChanged() {
    for (const auto& child : children_) child->OnDpiChanged();
}

bool Component::PrepareResources(COLORREF parent_background) {
    const config::ResolvedStyle& resolved_style = style();
    if (!host_.render_runtime->PrepareStyleResources(resolved_style, host_.dpi, parent_background)) {
        return false;
    }
    const rendering::RgbaColor normal = host_.render_runtime->ResolveColor(
        resolved_style.states[StateIndex(config::VisualState::Normal)].background);
    const COLORREF child_background = rendering::CompositeOverOpaque(parent_background, normal);
    for (const auto& child : children_) {
        if (!child->PrepareResources(child_background)) return false;
    }
    return true;
}

void Component::AddChild(std::unique_ptr<Component> child) {
    if (child) {
        child->parent_ = this;
        children_.push_back(std::move(child));
    }
}

void Component::CollectComponents(std::vector<Component*>& components) {
    components.push_back(this);
    for (const auto& child : children_) child->CollectComponents(components);
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

Component* Component::parent() const noexcept {
    return parent_;
}

bool Component::IsDescendantOrSelfOf(const Component* ancestor) const noexcept {
    if (!ancestor) return false;
    for (const Component* current = this; current; current = current->parent_) {
        if (current == ancestor) return true;
    }
    return false;
}

Component* Component::FindById(std::string_view id) noexcept {
    if (definition_.id == id) return this;
    for (const auto& child : children_) {
        if (Component* found = child->FindById(id)) return found;
    }
    return nullptr;
}

void Component::PaintStyleBox(HDC dc, config::VisualState state, const RECT& bounds) const {
    const config::ResolvedStyle& resolved_style = style();
    const config::ResolvedVisualState& visual = resolved_style.states[StateIndex(state)];
    const rendering::RgbaColor background = host_.render_runtime->ResolveColor(visual.background);
    const rendering::RgbaColor border = host_.render_runtime->ResolveColor(visual.border);
    const int radius = ScaleDip(resolved_style.radius, host_.dpi);
    const int border_width = ScaleDip(resolved_style.border_width, host_.dpi);

    if ((background.alpha < 255 || border.alpha < 255) && host_.render_context) {
        host_.render_context->SourceOverRounded(bounds, radius, border_width, background, border);
        return;
    }
    COLORREF opaque_background = GetPixel(dc, bounds.left, bounds.top);
    if (opaque_background == CLR_INVALID) opaque_background = GetSysColor(COLOR_WINDOW);
    host_.render_runtime->PaintRoundedStyleBox(
        dc, bounds, radius, border_width, background, border, opaque_background, host_.dpi,
        static_cast<unsigned int>(state));
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

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::wstring ResolveAutomationName(const config::ResolvedComponent& definition,
                                   std::wstring fallback) {
    if (const auto* literal = std::get_if<std::string>(&definition.automation.name)) {
        const std::wstring resolved = Utf8ToWide(*literal);
        if (!resolved.empty()) return resolved;
    }
    return definition.automation.automatic_name ? std::move(fallback) : std::wstring{};
}

bool PointInRectInclusive(const RECT& bounds, POINT point) noexcept {
    return point.x >= bounds.left && point.x < bounds.right && point.y >= bounds.top &&
           point.y < bounds.bottom;
}

}  // namespace ui::components
