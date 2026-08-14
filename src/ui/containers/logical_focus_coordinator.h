#pragma once

#include <cstddef>
#include <vector>

namespace ui::components {
class Component;
}

namespace ui::containers {

class LogicalFocusCoordinator final {
public:
    void Rebuild(components::Component& root);
    void Clear() noexcept;
    bool RequestFocus(components::Component* target, bool synchronize_native = true);
    bool Move(bool reverse);
    void NotifyNativeFocus(components::Component* target, bool focused);
    void SetWindowActive(bool active);
    bool HandleKeyDown(unsigned int virtual_key);

    components::Component* focused() const noexcept;
    std::size_t focusable_count() const noexcept;

private:
    bool Contains(const components::Component* target) const noexcept;

    std::vector<components::Component*> focusable_;
    components::Component* focused_ = nullptr;
    bool window_active_ = true;
    bool synchronizing_ = false;
};

}  // namespace ui::containers
