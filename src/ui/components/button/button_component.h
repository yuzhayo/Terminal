#pragma once

#include <optional>

#include "ui/components/component.h"

namespace ui::components {

class ButtonComponent final : public Component {
public:
    using Component::Component;

    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Paint(HDC dc) override;
    bool PointerMove(POINT point) override;
    bool PointerDown(POINT point) override;
    bool PointerUp(POINT point) override;
    bool CanFocus() const noexcept override;
    bool FocusNativePeer() override;
    void SetLogicalFocus(bool focused, bool window_active) override;
    bool HandleKeyDown(UINT virtual_key) override;
    AutomationRole automation_role() const noexcept override;
    std::wstring automation_name() const override;
    bool automation_supports_invoke() const noexcept override;
    bool AutomationInvoke() override;

    void SetSelectedOverride(bool selected);

private:
    config::VisualState State() const noexcept;
    void Activate();

    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
    bool keyboard_focus_ = false;
    bool window_active_ = true;
    std::optional<bool> press_override_;
};

}  // namespace ui::components
