#pragma once

#include "ui/components/component.h"

namespace ui::components {

class DialogComponent final : public Component {
public:
    DialogComponent(const config::ResolvedComponent& definition, ComponentHost& host);

    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Arrange(const RECT& bounds) override;
    void Paint(HDC dc) override;
    Component* HitTest(POINT point) override;
    bool PointerDown(POINT point) override;
    bool HandleKeyDown(UINT virtual_key) override;
    void CollectFocusable(std::vector<Component*>& focusable) override;
    bool SuspendNativePeers(std::wstring& diagnostic) override;
    void ResumeNativePeers() override;
    void AddChild(std::unique_ptr<Component> child) override;

    bool IsModalOverlay() const noexcept override;
    bool IsModalActive() const noexcept override;
    bool ActivateModal(std::wstring& diagnostic) override;
    bool DeactivateModal(std::wstring& diagnostic) override;
    void ArrangeModal(const RECT& client_bounds) override;
    void PaintModalOverlay(rendering::WindowRenderContext& context,
                           const RECT& invalid_region) override;
    bool CanCompleteModal(ModalResult result) const noexcept override;
    void CompleteModal(ModalResult result) override;
    AutomationRole automation_role() const noexcept override;
    std::wstring automation_name() const override;
    RECT automation_bounds() const noexcept override;
    bool automation_is_dialog() const noexcept override;
    bool automation_is_modal() const noexcept override;
    bool AutomationClose() override;

    const RECT& panel_bounds() const noexcept;

private:
    const config::DialogProperties& Properties() const;
    MeasuredSize MeasurePanel(HDC dc, int available_width, int available_height);
    void ArrangePanelChildren(HDC dc);
    void Dispatch(std::string_view event_name);

    RECT panel_bounds_{};
    RECT title_bounds_{};
    bool active_ = false;
};

}  // namespace ui::components
