#pragma once

#include "ui/components/component.h"
#include "ui/components/scrollbar/scrollbar_component.h"

namespace ui::components {

class ContainerComponent final : public Component, public ScrollModel {
public:
    ContainerComponent(const config::ResolvedComponent& definition, ComponentHost& host);

    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Arrange(const RECT& bounds) override;
    void Paint(HDC dc) override;
    Component* HitTest(POINT point) override;
    bool PointerWheel(int delta) override;
    void CollectFocusable(std::vector<Component*>& focusable) override;
    void CollectAutomationElements(std::vector<Component*>& elements) override;
    void OnDpiChanged() override;
    bool PrepareResources(COLORREF parent_background) override;

    int ScrollMinimum() const noexcept override;
    int ScrollMaximum() const noexcept override;
    int ScrollPageSize() const noexcept override;
    int ScrollValue() const noexcept override;
    void SetScrollValue(int value) override;

private:
    void EnsureScrollbar();

    config::ResolvedComponent scrollbar_definition_;
    std::unique_ptr<ScrollbarComponent> scrollbar_;
    RECT viewport_{};
    int content_extent_ = 0;
    int viewport_extent_ = 0;
    int scroll_value_ = 0;
    bool scrollbar_visible_ = false;
};

}  // namespace ui::components
