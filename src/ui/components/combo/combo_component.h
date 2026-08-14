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

ComboPopupPlacement CalculateComboPopupPlacement(const RECT& trigger_screen,
                                                  const RECT& work_area,
                                                  SIZE popup_size, int gap) noexcept;

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
    AutomationRole automation_role() const noexcept override;
    std::wstring automation_name() const override;
    std::optional<bool> automation_expanded() const noexcept override;
    bool AutomationExpand() override;
    bool AutomationCollapse() override;

private:
    static LRESULT CALLBACK PopupProcedure(HWND window, UINT message, WPARAM wparam,
                                           LPARAM lparam);
    LRESULT HandlePopupMessage(UINT message, WPARAM wparam, LPARAM lparam);
    bool EnsurePopup();
    void OpenPopup();
    void ClosePopup(bool dispatch_event = true);
    void RefreshItems();
    void PositionAndRenderPopup();
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
