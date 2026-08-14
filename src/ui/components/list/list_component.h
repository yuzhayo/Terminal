#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "ui/components/component.h"
#include "ui/components/scrollbar/scrollbar_component.h"

namespace ui::components {

struct ListRealizationRange {
    std::size_t first = 0;
    std::size_t count = 0;
};

ListRealizationRange CalculateListRealizationRange(std::size_t item_count, int row_height,
                                                    int scroll_value, int viewport_height,
                                                    int overscan_rows) noexcept;

class ListComponent final : public Component, public ScrollModel {
public:
    ListComponent(const config::ResolvedComponent& definition, ComponentHost& host);

    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Arrange(const RECT& bounds) override;
    void Paint(HDC dc) override;
    Component* HitTest(POINT point) override;
    bool PointerMove(POINT point) override;
    bool PointerDown(POINT point) override;
    bool PointerUp(POINT point) override;
    bool PointerWheel(int delta) override;
    bool CanFocus() const noexcept override;
    bool FocusNativePeer() override;
    void SetLogicalFocus(bool focused, bool window_active) override;
    bool HandleKeyDown(UINT virtual_key) override;
    void CollectFocusable(std::vector<Component*>& focusable) override;
    void CollectAutomationElements(std::vector<Component*>& elements) override;
    void CaptureRuntimeState(ComponentRuntimeStateMap& states) const override;
    void RestoreRuntimeState(const ComponentRuntimeStateMap& states) override;
    void OnDpiChanged() override;
    bool PrepareResources(COLORREF parent_background) override;
    AutomationRole automation_role() const noexcept override;
    bool automation_supports_item_container() const noexcept override;
    bool automation_supports_selection() const noexcept override;
    std::size_t automation_item_count() const noexcept override;
    std::wstring automation_item_name(std::size_t index) const override;
    std::optional<RECT> automation_item_screen_bounds(
        std::size_t index) const noexcept override;
    bool automation_item_realized(std::size_t index) const noexcept override;
    bool automation_item_selected(std::size_t index) const noexcept override;
    bool AutomationSelectItem(std::size_t index) override;
    bool AutomationRealizeItem(std::size_t index) override;
    std::optional<AutomationScrollState> automation_scroll_state() const noexcept override;
    bool AutomationScrollVertical(AutomationScrollAmount amount) override;
    bool AutomationSetVerticalScrollPercent(double percent) override;

    int ScrollMinimum() const noexcept override;
    int ScrollMaximum() const noexcept override;
    int ScrollPageSize() const noexcept override;
    int ScrollValue() const noexcept override;
    void SetScrollValue(int value) override;

    std::size_t realized_row_count() const noexcept;
    ListRealizationRange realized_range() const noexcept;
    std::optional<std::size_t> selected_index() const noexcept;

private:
    struct RealizedRow {
        std::size_t item_index = 0;
        RECT bounds{};
    };

    const config::ListProperties& Properties() const;
    void EnsureScrollbar();
    void RefreshItems();
    void RealizeVisibleRows();
    void PaintRow(HDC dc, const RealizedRow& row, config::VisualState state);
    int ItemAt(POINT point) const noexcept;
    void Select(std::size_t index, bool dispatch_event);
    void EnsureSelectedVisible();
    void Dispatch(std::string_view name);
    int RowHeight() const noexcept;

    config::ResolvedComponent scrollbar_definition_;
    std::unique_ptr<ScrollbarComponent> scrollbar_;
    std::vector<std::wstring> items_;
    std::vector<RealizedRow> realized_rows_;
    RECT viewport_{};
    std::optional<std::size_t> selected_index_;
    int hovered_index_ = -1;
    int pressed_index_ = -1;
    int content_extent_ = 0;
    int viewport_extent_ = 0;
    int scroll_value_ = 0;
    bool scrollbar_visible_ = false;
    bool focused_ = false;
    bool window_active_ = true;
};

}  // namespace ui::components
