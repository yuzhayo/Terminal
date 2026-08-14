#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/components/component.h"

namespace rendering {
class WindowRenderContext;
}

namespace ui::containers {

class LogicalFocusCoordinator;

class ModalOverlayStack final {
public:
    bool Push(components::Component& dialog, components::Component& root,
              LogicalFocusCoordinator& focus, std::wstring& diagnostic);
    bool Pop(components::ModalResult result, components::Component& root,
             LogicalFocusCoordinator& focus, std::wstring& diagnostic);
    bool Drain(components::Component& root, LogicalFocusCoordinator& focus,
               std::wstring& diagnostic);

    void Arrange(const RECT& client_bounds);
    void Paint(rendering::WindowRenderContext& context, const RECT& invalid_region);
    components::Component* HitTest(POINT point) const;
    bool HandleKeyDown(UINT virtual_key);

    components::Component* top() const noexcept;
    bool active() const noexcept;
    std::size_t size() const noexcept;
    std::size_t suppression_depth(const components::Component* component) const noexcept;
    bool ContainsTopScope(const components::Component* component) const noexcept;

private:
    struct Entry {
        components::Component* dialog = nullptr;
        components::Component* prior_focus = nullptr;
        std::vector<components::Component*> suppressed;
    };

    static bool InsideInactiveDialog(const components::Component* component) noexcept;
    void RollbackSuppression(const std::vector<components::Component*>& components) noexcept;

    std::vector<Entry> entries_;
    std::unordered_map<components::Component*, std::size_t> suppression_depths_;
};

}  // namespace ui::containers
