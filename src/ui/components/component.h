#pragma once

#include <windows.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rendering/render_runtime.h"
#include "ui/components/editable_draft_state.h"
#include "ui/config/resolved_ui_document.h"

namespace ui::components {

class Component;

enum class AutomationRole {
    None,
    Button,
    Checkbox,
    ToggleButton,
    Edit,
    Combo,
    List,
    Scrollbar,
    Group,
    Dialog,
};
enum class AutomationAction {
    Focus,
    Invoke,
    Toggle,
    Expand,
    Collapse,
    SetRangeValue,
    Close,
    SelectItem,
    RealizeItem,
    ScrollVertical,
    SetVerticalScrollPercent,
    SelectPopupItem,
    RealizePopupItem,
};
enum class AutomationScrollAmount {
    LargeDecrement,
    SmallDecrement,
    NoAmount,
    LargeIncrement,
    SmallIncrement,
};
enum class ModalResult { Accept, Discard, Cancel, Dismiss };

struct AutomationRangeValue {
    double value = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double large_change = 0.0;
    double small_change = 0.0;
};

struct AutomationScrollState {
    bool horizontally_scrollable = false;
    double horizontal_scroll_percent = -1.0;
    double horizontal_view_size = 100.0;
    bool vertically_scrollable = false;
    double vertical_scroll_percent = -1.0;
    double vertical_view_size = 100.0;
};

struct MeasuredSize {
    int width = 0;
    int height = 0;
};

struct RouteTabDefinition {
    std::string route_id;
    std::wstring label;
};

struct ComponentRuntimeState {
    config::ComponentType type = config::ComponentType::Container;
    std::optional<std::wstring> draft_baseline;
    std::optional<std::wstring> draft_value;
    std::optional<std::size_t> selected_index;
    std::optional<bool> checked;
    std::optional<bool> selected;
    std::optional<int> scroll_value;
    std::optional<DWORD> selection_start;
    std::optional<DWORD> selection_end;
};

using ComponentRuntimeStateMap =
    std::map<std::string, ComponentRuntimeState, std::less<>>;

struct ComponentHost {
    HWND window = nullptr;
    UINT dpi = 96;
    HDC layout_dc = nullptr;
    rendering::RenderRuntime* render_runtime = nullptr;
    rendering::WindowRenderContext* render_context = nullptr;
    const config::ResolvedTheme* theme = nullptr;
    std::function<void(const RECT&)> invalidate;
    std::function<void(Component&, std::string_view, const config::EventDefinition&)>
        dispatch_event;
    std::function<void(Component*, bool)> native_focus_changed;
    std::function<void(Component*, bool)> popup_state_changed;
    std::function<std::vector<std::wstring>(std::string_view)> resolve_string_items;
    std::function<std::optional<std::wstring>(std::string_view)> resolve_string_value;
    std::function<bool(AutomationAction, Component*, double)> request_automation_action;
    std::function<void(bool)> request_focus_traversal;
    std::function<bool(Component*, ModalResult)> request_modal_close;
    std::function<LRESULT(Component*, HWND, WPARAM, LPARAM)> return_popup_automation_provider;
    std::function<std::vector<RouteTabDefinition>()> resolve_route_tabs;
    std::function<std::string_view()> resolve_active_route;
    std::function<bool(std::string_view)> request_route;
};

class Component {
public:
    Component(const config::ResolvedComponent& definition, ComponentHost& host);
    virtual ~Component() = default;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    virtual MeasuredSize Measure(HDC dc, int available_width, int available_height) = 0;
    virtual void Arrange(const RECT& bounds);
    virtual void Paint(HDC dc) = 0;

    virtual Component* HitTest(POINT point);
    virtual bool PointerMove(POINT point);
    virtual bool PointerDown(POINT point);
    virtual bool PointerUp(POINT point);
    virtual bool PointerWheel(int delta);
    virtual bool HandleCommand(HWND source, WORD notification);
    virtual HBRUSH HandleControlColor(HDC dc, HWND source);
    virtual bool OwnsNativePeer(HWND source) const noexcept;
    virtual bool CanFocus() const noexcept;
    virtual bool FocusNativePeer();
    virtual void SetLogicalFocus(bool focused, bool window_active);
    virtual bool HandleKeyDown(UINT virtual_key);
    virtual bool HasOpenPopup() const noexcept;
    virtual HWND OwnedPopupHwnd() const noexcept;
    virtual bool OwnsPopupScopePoint(POINT screen_point) const noexcept;
    virtual void DismissOwnedPopup();
    virtual bool RequiresNativePeerSuppression() const noexcept;
    virtual bool IsModalOverlay() const noexcept;
    virtual bool IsModalActive() const noexcept;
    virtual bool ActivateModal(std::wstring& diagnostic);
    virtual bool DeactivateModal(std::wstring& diagnostic);
    virtual void ArrangeModal(const RECT& client_bounds);
    virtual void PaintModalOverlay(rendering::WindowRenderContext& context,
                                   const RECT& invalid_region);
    virtual bool CanCompleteModal(ModalResult result) const noexcept;
    virtual void CompleteModal(ModalResult result);
    virtual void CollectFocusable(std::vector<Component*>& focusable);
    virtual bool SuspendNativePeers(std::wstring& diagnostic);
    virtual void ResumeNativePeers();
    virtual void CollectEditableParticipants(std::vector<EditableParticipant*>& participants);
    virtual void CaptureRuntimeState(ComponentRuntimeStateMap& states) const;
    virtual void RestoreRuntimeState(const ComponentRuntimeStateMap& states);
    virtual void CollectAutomationElements(std::vector<Component*>& elements);
    virtual AutomationRole automation_role() const noexcept;
    virtual std::wstring automation_name() const;
    virtual RECT automation_bounds() const noexcept;
    virtual bool automation_supports_invoke() const noexcept;
    virtual bool AutomationInvoke();
    virtual std::optional<bool> automation_toggle_state() const noexcept;
    virtual bool AutomationToggle();
    virtual std::optional<bool> automation_expanded() const noexcept;
    virtual bool AutomationExpand();
    virtual bool AutomationCollapse();
    virtual std::optional<AutomationRangeValue> automation_range_value() const noexcept;
    virtual bool AutomationSetRangeValue(double value);
    virtual bool automation_is_dialog() const noexcept;
    virtual bool automation_is_modal() const noexcept;
    virtual bool AutomationClose();
    virtual bool automation_supports_item_container() const noexcept;
    virtual bool automation_supports_selection() const noexcept;
    virtual bool automation_selection_required() const noexcept;
    virtual std::size_t automation_item_count() const noexcept;
    virtual std::wstring automation_item_name(std::size_t index) const;
    virtual std::optional<RECT> automation_item_screen_bounds(std::size_t index) const noexcept;
    virtual bool automation_item_realized(std::size_t index) const noexcept;
    virtual bool automation_item_selected(std::size_t index) const noexcept;
    virtual bool AutomationSelectItem(std::size_t index);
    virtual bool AutomationRealizeItem(std::size_t index);
    virtual std::optional<AutomationScrollState> automation_scroll_state() const noexcept;
    virtual bool AutomationScrollVertical(AutomationScrollAmount amount);
    virtual bool AutomationSetVerticalScrollPercent(double percent);
    virtual bool automation_has_popup_fragment() const noexcept;
    virtual bool automation_popup_visible() const noexcept;
    virtual HWND automation_popup_hwnd() const noexcept;
    virtual std::size_t automation_popup_item_count() const noexcept;
    virtual std::wstring automation_popup_item_name(std::size_t index) const;
    virtual std::optional<RECT> automation_popup_item_screen_bounds(
        std::size_t index) const noexcept;
    virtual bool automation_popup_item_realized(std::size_t index) const noexcept;
    virtual bool automation_popup_item_selected(std::size_t index) const noexcept;
    virtual bool AutomationSelectPopupItem(std::size_t index);
    virtual bool AutomationRealizePopupItem(std::size_t index);
    bool RequestAutomationFocus();
    bool RequestAutomationInvoke();
    bool RequestAutomationToggle();
    bool RequestAutomationExpand();
    bool RequestAutomationCollapse();
    bool RequestAutomationSetRangeValue(double value);
    bool RequestAutomationClose();
    bool RequestAutomationSelectItem(std::size_t index);
    bool RequestAutomationRealizeItem(std::size_t index);
    bool RequestAutomationScrollVertical(AutomationScrollAmount amount);
    bool RequestAutomationSetVerticalScrollPercent(double percent);
    bool RequestAutomationSelectPopupItem(std::size_t index);
    bool RequestAutomationRealizePopupItem(std::size_t index);
    virtual HWND automation_native_peer() const noexcept;
    virtual bool automation_is_password() const noexcept;
    virtual void OnDpiChanged();
    virtual bool PrepareResources(COLORREF parent_background);
    virtual void ReleaseResources() noexcept;
    virtual void AddChild(std::unique_ptr<Component> child);
    std::unique_ptr<Component> DetachChild(Component* child);
    virtual void CollectComponents(std::vector<Component*>& components);

    const RECT& bounds() const noexcept;
    const config::ResolvedComponent& definition() const noexcept;
    std::uint64_t instance_id() const noexcept;
    const config::ResolvedStyle& style() const;
    bool visible() const noexcept;
    bool enabled() const noexcept;
    Component* parent() const noexcept;
    bool IsDescendantOrSelfOf(const Component* ancestor) const noexcept;
    Component* FindById(std::string_view id) noexcept;

protected:
    std::wstring ResolveTextValue(const config::TextValue& value) const;
    bool ResolveBooleanValue(const config::BooleanValue& value) const;
    void PaintStyleBox(HDC dc, config::VisualState state, const RECT& bounds) const;
    void PaintChildren(HDC dc);
    void EmitEvent(std::string_view event_type,
                   config::EventPayloadValue::Object runtime_payload = {});
    MeasuredSize ApplyConstraints(MeasuredSize measured, int available_width,
                                  int available_height) const noexcept;
    void Invalidate() const;

    const config::ResolvedComponent& definition_;
    ComponentHost& host_;
    std::uint64_t instance_id_ = 0;
    RECT bounds_{};
    Component* parent_ = nullptr;
    std::vector<std::unique_ptr<Component>> children_;
};

int ScaleDip(int value, UINT dpi) noexcept;
std::wstring ResolveText(const config::TextValue& value);
std::wstring ResolveAutomationName(const config::ResolvedComponent& definition,
                                   std::wstring fallback);
std::wstring Utf8ToWide(std::string_view value);
std::string WideToUtf8(std::wstring_view value);
bool PointInRectInclusive(const RECT& bounds, POINT point) noexcept;

}  // namespace ui::components
