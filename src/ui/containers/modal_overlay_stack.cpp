#include "ui/containers/modal_overlay_stack.h"

#include <algorithm>

#include "rendering/window_render_context.h"
#include "ui/containers/logical_focus_coordinator.h"

namespace ui::containers {

bool ModalOverlayStack::Push(components::Component& dialog, components::Component& root,
                             LogicalFocusCoordinator& focus, std::wstring& diagnostic) {
    if (!dialog.IsModalOverlay() || dialog.IsModalActive()) {
        diagnostic = L"Target modal tidak valid atau sudah aktif.";
        return false;
    }
    if (components::Component* current = top();
        current && !dialog.IsDescendantOrSelfOf(current)) {
        diagnostic = L"Nested Dialog harus berada dalam component ancestry Dialog aktif.";
        return false;
    }

    std::vector<components::Component*> components;
    root.CollectComponents(components);
    for (components::Component* component : components) {
        if (component != &dialog && component->HasOpenPopup() &&
            !component->IsDescendantOrSelfOf(&dialog)) {
            component->DismissOwnedPopup();
        }
    }

    Entry entry;
    entry.dialog = &dialog;
    entry.prior_focus = focus.focused();
    for (components::Component* component : components) {
        if (!component->RequiresNativePeerSuppression() ||
            component->IsDescendantOrSelfOf(&dialog) || InsideInactiveDialog(component)) {
            continue;
        }
        std::size_t& depth = suppression_depths_[component];
        if (depth == 0) {
            if (!component->SuspendNativePeers(diagnostic)) {
                suppression_depths_.erase(component);
                RollbackSuppression(entry.suppressed);
                return false;
            }
        }
        ++depth;
        entry.suppressed.push_back(component);
    }

    if (!dialog.ActivateModal(diagnostic)) {
        RollbackSuppression(entry.suppressed);
        return false;
    }
    dialog.ArrangeModal(root.bounds());
    entries_.push_back(std::move(entry));
    focus.Rebuild(root);
    focus.SetScope(&dialog);
    if (!focus.Move(false) && dialog.CanFocus()) focus.RequestFocus(&dialog);
    diagnostic.clear();
    return true;
}

bool ModalOverlayStack::Pop(components::ModalResult result, components::Component& root,
                            LogicalFocusCoordinator& focus, std::wstring& diagnostic) {
    if (entries_.empty()) {
        diagnostic = L"Modal stack kosong.";
        return false;
    }
    Entry& active_entry = entries_.back();
    if (!active_entry.dialog->CanCompleteModal(result)) {
        diagnostic = L"Dismiss policy Dialog menolak explicit action.";
        return false;
    }
    if (!active_entry.dialog->DeactivateModal(diagnostic)) return false;

    Entry entry = std::move(active_entry);
    entries_.pop_back();
    RollbackSuppression(entry.suppressed);
    focus.Rebuild(root);
    focus.SetScope(top());
    if (!entry.prior_focus || !focus.RequestFocus(entry.prior_focus)) focus.Move(false);
    entry.dialog->CompleteModal(result);
    diagnostic.clear();
    return true;
}

bool ModalOverlayStack::Drain(components::Component& root, LogicalFocusCoordinator& focus,
                              std::wstring& diagnostic) {
    while (!entries_.empty()) {
        if (!Pop(components::ModalResult::Dismiss, root, focus, diagnostic)) return false;
    }
    diagnostic.clear();
    return suppression_depths_.empty();
}

void ModalOverlayStack::Arrange(const RECT& client_bounds) {
    for (const Entry& entry : entries_) entry.dialog->ArrangeModal(client_bounds);
}

void ModalOverlayStack::Paint(rendering::WindowRenderContext& context,
                              const RECT& invalid_region) {
    for (const Entry& entry : entries_) entry.dialog->PaintModalOverlay(context, invalid_region);
}

components::Component* ModalOverlayStack::HitTest(POINT point) const {
    components::Component* current = top();
    return current ? current->HitTest(point) : nullptr;
}

bool ModalOverlayStack::HandleKeyDown(UINT virtual_key) {
    components::Component* current = top();
    return current && current->HandleKeyDown(virtual_key);
}

components::Component* ModalOverlayStack::top() const noexcept {
    return entries_.empty() ? nullptr : entries_.back().dialog;
}

bool ModalOverlayStack::active() const noexcept { return !entries_.empty(); }
std::size_t ModalOverlayStack::size() const noexcept { return entries_.size(); }

std::size_t ModalOverlayStack::suppression_depth(
    const components::Component* component) const noexcept {
    const auto found = suppression_depths_.find(const_cast<components::Component*>(component));
    return found == suppression_depths_.end() ? 0 : found->second;
}

bool ModalOverlayStack::ContainsTopScope(const components::Component* component) const noexcept {
    return component && (!top() || component->IsDescendantOrSelfOf(top()));
}

bool ModalOverlayStack::InsideInactiveDialog(const components::Component* component) noexcept {
    for (const components::Component* current = component ? component->parent() : nullptr; current;
         current = current->parent()) {
        if (current->IsModalOverlay() && !current->IsModalActive()) return true;
    }
    return false;
}

void ModalOverlayStack::RollbackSuppression(
    const std::vector<components::Component*>& components) noexcept {
    for (auto item = components.rbegin(); item != components.rend(); ++item) {
        auto found = suppression_depths_.find(*item);
        if (found == suppression_depths_.end()) continue;
        if (--found->second == 0) {
            (*item)->ResumeNativePeers();
            suppression_depths_.erase(found);
        }
    }
}

}  // namespace ui::containers
