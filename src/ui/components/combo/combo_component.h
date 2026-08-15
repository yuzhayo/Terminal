#pragma once

#include <optional>
#include <vector>

#include "rendering/layered_popup_render_context.h"
#include "ui/components/component.h"

namespace ui::components {

struct ComboPopupPlacement {
    POINT origin{};
    bool opens_above = false;
};

struct ComboPopupMetrics {
    SIZE surface{};
    int shadow_margin = 0;
    int item_height = 1;
    int visible_rows = 1;
};

ComboPopupPlacement CalculateComboPopupPlacement(const RECT& trigger_screen,
                                                  const RECT& work_area,
                                                  SIZE popup_size, int gap) noexcept;
ComboPopupMetrics CalculateComboPopupMetrics(int trigger_width, std::size_t item_count,
                                              int maximum_visible_items,
                                              int popup_maximum_height, SIZE work_area,
                                              UINT dpi) noexcept;

class ComboComponent final : public Component {
public:
    ComboComponent(const config::ResolvedComponent& definition, ComponentHost& host);
    ~ComboComponent() override;

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
    bool HasOpenPopup() const noexcept override;
    HWND OwnedPopupHwnd() const noexcept override;
    bool OwnsPopupScopePoint(POINT screen_point) const noexcept override;
    void DismissOwnedPopup() override;
    bool SuspendNativePeers(std::wstring& diagnostic) override;
    void OnDpiChanged() override;
    void CaptureRuntimeState(ComponentRuntimeStateMap& states) const override;
    void RestoreRuntimeState(const ComponentRuntimeStateMap& states) override;
    AutomationRole automation_role() const noexcept override;
    std::wstring automation_name() const override;
    std::optional<bool> automation_expanded() const noexcept override;
    bool AutomationExpand() override;
    bool AutomationCollapse() override;
    bool automation_has_popup_fragment() const noexcept override;
    bool automation_popup_visible() const noexcept override;
    HWND automation_popup_hwnd() const noexcept override;
    std::size_t automation_popup_item_count() const noexcept override;
    std::wstring automation_popup_item_name(std::size_t index) const override;
    std::optional<RECT> automation_popup_item_screen_bounds(
        std::size_t index) const noexcept override;
    bool automation_popup_item_realized(std::size_t index) const noexcept override;
    bool automation_popup_item_selected(std::size_t index) const noexcept override;
    bool AutomationSelectPopupItem(std::size_t index) override;
    bool AutomationRealizePopupItem(std::size_t index) override;

private:
    static LRESULT CALLBACK PopupProcedure(HWND window, UINT message, WPARAM wparam,
                                           LPARAM lparam);
    LRESULT HandlePopupMessage(UINT message, WPARAM wparam, LPARAM lparam);
    bool EnsurePopup();
    void OpenPopup();
    void ClosePopup(bool dispatch_event = true);
    void RefreshItems();
    void PositionAndRenderPopup(std::optional<UINT> dpi_override = std::nullopt);
    void RenderPopup();
    int ItemAt(POINT point) const noexcept;
    void SelectHighlighted();
    void MoveHighlight(int delta);
    void EnsureHighlightVisible();
    void Dispatch(std::string_view name);
    config::VisualState State() const noexcept;
    const config::ComboProperties& Properties() const;

    rendering::LayeredPopupRenderContext popup_render_context_;
    HWND popup_ = nullptr;
    std::vector<std::wstring> items_;
    std::optional<std::size_t> selected_index_;
    int highlighted_index_ = -1;
    int first_visible_index_ = 0;
    int visible_rows_ = 1;
    UINT popup_dpi_ = 96;
    int shadow_margin_ = 8;
    int item_height_ = 32;
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
    bool window_active_ = true;
    bool popup_open_ = false;
};

}  // namespace ui::components
