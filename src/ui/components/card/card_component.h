#pragma once

#include "ui/components/component.h"

namespace ui::components {

class CardComponent final : public Component {
public:
    CardComponent(const config::ResolvedComponent& definition, ComponentHost& host);

    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Arrange(const RECT& bounds) override;
    void Paint(HDC dc) override;
    bool PointerMove(POINT point) override;
    bool PointerDown(POINT point) override;
    bool PointerUp(POINT point) override;
    bool CanFocus() const noexcept override;
    bool FocusNativePeer() override;
    void SetLogicalFocus(bool focused, bool window_active) override;
    bool HandleKeyDown(UINT virtual_key) override;
    AutomationRole automation_role() const noexcept override;
    bool automation_supports_invoke() const noexcept override;
    bool AutomationInvoke() override;
    void CaptureRuntimeState(ComponentRuntimeStateMap& states) const override;
    void RestoreRuntimeState(const ComponentRuntimeStateMap& states) override;

private:
    config::VisualState State() const noexcept;
    void Activate();

    bool selected_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
    bool window_active_ = true;
};

}  // namespace ui::components
