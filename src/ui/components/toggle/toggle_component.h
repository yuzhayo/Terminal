#pragma once

#include "ui/components/component.h"

namespace ui::components {

class ToggleComponent final : public Component {
public:
    ToggleComponent(const config::ResolvedComponent& definition, ComponentHost& host);

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
    std::optional<bool> automation_toggle_state() const noexcept override;
    bool AutomationToggle() override;
    void CaptureRuntimeState(ComponentRuntimeStateMap& states) const override;
    void RestoreRuntimeState(const ComponentRuntimeStateMap& states) override;

private:
    config::VisualState State() const noexcept;
    void Toggle();

    bool checked_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
    bool window_active_ = true;
};

}  // namespace ui::components
