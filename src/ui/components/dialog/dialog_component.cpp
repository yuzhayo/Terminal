#include "ui/components/dialog/dialog_component.h"

#include <algorithm>

#include "rendering/window_render_context.h"

namespace ui::components {
namespace {

constexpr int kPanelMarginDip = 24;
constexpr int kTitleGapDip = 12;
constexpr int kChildGapDip = 8;

}  // namespace

DialogComponent::DialogComponent(const config::ResolvedComponent& definition, ComponentHost& host)
    : Component(definition, host) {}

const config::DialogProperties& DialogComponent::Properties() const {
    return std::get<config::DialogProperties>(definition_.properties);
}

MeasuredSize DialogComponent::Measure(HDC dc, int available_width, int available_height) {
    if (!active_) return {0, 0};
    return MeasurePanel(dc, available_width, available_height);
}

MeasuredSize DialogComponent::MeasurePanel(HDC dc, int available_width, int available_height) {
    const int margin = ScaleDip(kPanelMarginDip, host_.dpi);
    const int width = std::min(ScaleDip(Properties().width, host_.dpi),
                               std::max(0, available_width - margin * 2));
    const int horizontal_padding = ScaleDip(
        style().content_padding.left + style().content_padding.right, host_.dpi);
    const int vertical_padding = ScaleDip(
        style().content_padding.top + style().content_padding.bottom, host_.dpi);
    const int inner_width = std::max(0, width - horizontal_padding);
    const std::wstring title = ResolveText(Properties().title);
    const SIZE title_size = host_.render_runtime->MeasureText(
        title, style().font, host_.dpi, inner_width, DT_SINGLELINE | DT_NOPREFIX);
    int content_height = static_cast<int>(title_size.cy);
    if (!children_.empty()) content_height += ScaleDip(kTitleGapDip, host_.dpi);
    for (std::size_t index = 0; index < children_.size(); ++index) {
        const MeasuredSize child = children_[index]->Measure(dc, inner_width, available_height);
        content_height += child.height;
        if (index > 0) content_height += ScaleDip(kChildGapDip, host_.dpi);
    }
    const int maximum = std::min(ScaleDip(Properties().maximum_height, host_.dpi),
                                 std::max(0, available_height - margin * 2));
    return {width, std::min(content_height + vertical_padding, maximum)};
}

void DialogComponent::Arrange(const RECT& bounds) {
    bounds_ = bounds;
    if (active_) ArrangeModal(bounds);
}

void DialogComponent::ArrangeModal(const RECT& client_bounds) {
    bounds_ = client_bounds;
    if (!active_) return;
    HDC dc = host_.layout_dc ? host_.layout_dc
                             : (host_.render_context ? host_.render_context->dc() : nullptr);
    const bool release_dc = !dc;
    if (!dc) dc = GetDC(host_.window);
    const MeasuredSize panel = MeasurePanel(
        dc, client_bounds.right - client_bounds.left, client_bounds.bottom - client_bounds.top);
    const int left = client_bounds.left +
                     std::max(0, static_cast<int>(client_bounds.right - client_bounds.left - panel.width) / 2);
    const int top = client_bounds.top +
                    std::max(0, static_cast<int>(client_bounds.bottom - client_bounds.top - panel.height) / 2);
    panel_bounds_ = {left, top, left + panel.width, top + panel.height};
    ArrangePanelChildren(dc);
    Component::ResumeNativePeers();
    if (release_dc && dc) ReleaseDC(host_.window, dc);
}

void DialogComponent::ArrangePanelChildren(HDC dc) {
    const int left = panel_bounds_.left + ScaleDip(style().content_padding.left, host_.dpi);
    const int right = panel_bounds_.right - ScaleDip(style().content_padding.right, host_.dpi);
    int cursor = panel_bounds_.top + ScaleDip(style().content_padding.top, host_.dpi);
    const std::wstring title = ResolveText(Properties().title);
    const SIZE title_size = host_.render_runtime->MeasureText(
        title, style().font, host_.dpi, std::max(0, right - left), DT_SINGLELINE | DT_NOPREFIX);
    title_bounds_ = {left, cursor, right, cursor + title_size.cy};
    cursor = title_bounds_.bottom + (children_.empty() ? 0 : ScaleDip(kTitleGapDip, host_.dpi));
    const int available_height = std::max(0, static_cast<int>(panel_bounds_.bottom - cursor) -
                                                ScaleDip(style().content_padding.bottom, host_.dpi));
    for (const auto& child : children_) {
        const MeasuredSize measured = child->Measure(dc, std::max(0, right - left), available_height);
        const int width = child->definition().layout.width.kind == config::DimensionKind::Fill
                              ? std::max(0, right - left)
                              : measured.width;
        child->Arrange({left, cursor, left + width,
                        std::min<LONG>(panel_bounds_.bottom, cursor + measured.height)});
        cursor += measured.height + ScaleDip(kChildGapDip, host_.dpi);
    }
}

void DialogComponent::Paint(HDC) {
    // Dialog is painted only through PaintModalOverlay after the root surface.
}

void DialogComponent::PaintModalOverlay(rendering::WindowRenderContext& context,
                                        const RECT& invalid_region) {
    if (!active_) return;
    const auto scrim = host_.theme->tokens.find("scrim");
    const rendering::RgbaColor scrim_color =
        scrim == host_.theme->tokens.end()
            ? rendering::RgbaColor{0, 0, 0, 128}
            : host_.render_runtime->ResolveColor(scrim->second);
    RECT scrim_region{};
    if (IntersectRect(&scrim_region, &bounds_, &invalid_region)) {
        context.SourceOver(scrim_region, scrim_color);
    }

    const int shadow_offset = ScaleDip(4, host_.dpi);
    RECT shadow = panel_bounds_;
    OffsetRect(&shadow, shadow_offset, shadow_offset);
    context.SourceOverRounded(shadow, ScaleDip(style().radius, host_.dpi), 0,
                              {0, 0, 0, 72}, {0, 0, 0, 0});
    const config::ResolvedVisualState& visual =
        style().states[static_cast<std::size_t>(config::VisualState::Normal)];
    context.SourceOverRounded(panel_bounds_, ScaleDip(style().radius, host_.dpi),
                              std::max(1, ScaleDip(style().border_width, host_.dpi)),
                              host_.render_runtime->ResolveColor(visual.background),
                              host_.render_runtime->ResolveColor(visual.border));

    HDC dc = context.dc();
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, panel_bounds_.left, panel_bounds_.top, panel_bounds_.right,
                      panel_bounds_.bottom);
    host_.render_runtime->DrawTextRun(
        dc, ResolveText(Properties().title), style().font, host_.dpi, title_bounds_,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
        rendering::ToColorRef(host_.render_runtime->ResolveColor(visual.foreground)));
    PaintChildren(dc);
    if (saved != 0) RestoreDC(dc, saved);
}

Component* DialogComponent::HitTest(POINT point) {
    if (!active_ || !PointInRectInclusive(bounds_, point)) return nullptr;
    if (PointInRectInclusive(panel_bounds_, point)) {
        for (auto item = children_.rbegin(); item != children_.rend(); ++item) {
            if (Component* hit = (*item)->HitTest(point)) return hit;
        }
    }
    return this;
}

bool DialogComponent::PointerDown(POINT point) {
    if (!active_) return false;
    if (!PointInRectInclusive(panel_bounds_, point) && Properties().dismiss_outside_click &&
        host_.request_modal_close) {
        return host_.request_modal_close(this, ModalResult::Dismiss);
    }
    return true;
}

bool DialogComponent::HandleKeyDown(UINT virtual_key) {
    return active_ && virtual_key == VK_ESCAPE && Properties().dismiss_escape &&
           host_.request_modal_close && host_.request_modal_close(this, ModalResult::Dismiss);
}

void DialogComponent::CollectFocusable(std::vector<Component*>& focusable) {
    if (active_) Component::CollectFocusable(focusable);
}

bool DialogComponent::SuspendNativePeers(std::wstring& diagnostic) {
    return Component::SuspendNativePeers(diagnostic);
}

void DialogComponent::ResumeNativePeers() {
    if (active_) Component::ResumeNativePeers();
}

void DialogComponent::AddChild(std::unique_ptr<Component> child) {
    Component* child_pointer = child.get();
    Component::AddChild(std::move(child));
    if (!active_ && child_pointer) {
        std::wstring ignored;
        child_pointer->SuspendNativePeers(ignored);
    }
}

bool DialogComponent::IsModalOverlay() const noexcept { return true; }
bool DialogComponent::IsModalActive() const noexcept { return active_; }

bool DialogComponent::ActivateModal(std::wstring& diagnostic) {
    if (active_) {
        diagnostic = L"Dialog sudah aktif.";
        return false;
    }
    active_ = true;
    diagnostic.clear();
    return true;
}

bool DialogComponent::DeactivateModal(std::wstring& diagnostic) {
    if (!active_) {
        diagnostic = L"Dialog tidak aktif.";
        return false;
    }
    if (!Component::SuspendNativePeers(diagnostic)) return false;
    active_ = false;
    diagnostic.clear();
    return true;
}

bool DialogComponent::CanCompleteModal(ModalResult result) const noexcept {
    return result == ModalResult::Dismiss || Properties().dismiss_explicit_action;
}

void DialogComponent::CompleteModal(ModalResult result) {
    switch (result) {
        case ModalResult::Accept: Dispatch("accept"); break;
        case ModalResult::Cancel: Dispatch("cancel"); break;
        case ModalResult::Discard:
        case ModalResult::Dismiss: Dispatch("dismiss"); break;
    }
}

AutomationRole DialogComponent::automation_role() const noexcept {
    return AutomationRole::Dialog;
}

std::wstring DialogComponent::automation_name() const {
    return ResolveAutomationName(definition_, ResolveText(Properties().title));
}
RECT DialogComponent::automation_bounds() const noexcept { return panel_bounds_; }

bool DialogComponent::automation_is_dialog() const noexcept { return true; }
bool DialogComponent::automation_is_modal() const noexcept { return active_; }
bool DialogComponent::AutomationClose() {
    return active_ && host_.request_modal_close &&
           host_.request_modal_close(this, ModalResult::Dismiss);
}

void DialogComponent::Dispatch(std::string_view event_name) {
    EmitEvent(event_name);
}

const RECT& DialogComponent::panel_bounds() const noexcept { return panel_bounds_; }

}  // namespace ui::components
