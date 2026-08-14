#pragma once

#include <windows.h>

#include <functional>
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

enum class AutomationRole { None, Button, Checkbox, ToggleButton, Edit, Combo, Scrollbar, Group };
enum class AutomationAction { Focus, Invoke, Toggle, Expand, Collapse, SetRangeValue };

struct AutomationRangeValue {
    double value = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double large_change = 0.0;
    double small_change = 0.0;
};

struct MeasuredSize {
    int width = 0;
    int height = 0;
};

struct ComponentHost {
    HWND window = nullptr;
    UINT dpi = 96;
    HDC layout_dc = nullptr;
    rendering::RenderRuntime* render_runtime = nullptr;
    rendering::WindowRenderContext* render_context = nullptr;
    const config::ResolvedTheme* theme = nullptr;
    std::function<void(const RECT&)> invalidate;
    std::function<void(const config::EventDefinition&)> dispatch_event;
    std::function<void(Component*, bool)> native_focus_changed;
    std::function<void(Component*, bool)> popup_state_changed;
    std::function<std::vector<std::wstring>(std::string_view)> resolve_string_items;
    std::function<std::optional<std::wstring>(std::string_view)> resolve_string_value;
    std::function<bool(AutomationAction, Component*, double)> request_automation_action;
    std::function<void(bool)> request_focus_traversal;
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
    virtual void CollectFocusable(std::vector<Component*>& focusable);
    virtual bool SuspendNativePeers(std::wstring& diagnostic);
    virtual void ResumeNativePeers();
    virtual void CollectEditableParticipants(std::vector<EditableParticipant*>& participants);
    virtual void CollectAutomationElements(std::vector<Component*>& elements);
    virtual AutomationRole automation_role() const noexcept;
    virtual std::wstring automation_name() const;
    virtual bool automation_supports_invoke() const noexcept;
    virtual bool AutomationInvoke();
    virtual std::optional<bool> automation_toggle_state() const noexcept;
    virtual bool AutomationToggle();
    virtual std::optional<bool> automation_expanded() const noexcept;
    virtual bool AutomationExpand();
    virtual bool AutomationCollapse();
    virtual std::optional<AutomationRangeValue> automation_range_value() const noexcept;
    virtual bool AutomationSetRangeValue(double value);
    bool RequestAutomationFocus();
    bool RequestAutomationInvoke();
    bool RequestAutomationToggle();
    bool RequestAutomationExpand();
    bool RequestAutomationCollapse();
    bool RequestAutomationSetRangeValue(double value);
    virtual HWND automation_native_peer() const noexcept;
    virtual bool automation_is_password() const noexcept;
    virtual void OnDpiChanged();
    virtual bool PrepareResources(COLORREF parent_background);
    virtual void AddChild(std::unique_ptr<Component> child);

    const RECT& bounds() const noexcept;
    const config::ResolvedComponent& definition() const noexcept;
    const config::ResolvedStyle& style() const;
    bool visible() const noexcept;
    bool enabled() const noexcept;
    Component* parent() const noexcept;

protected:
    void PaintStyleBox(HDC dc, config::VisualState state, const RECT& bounds) const;
    void PaintChildren(HDC dc);
    MeasuredSize ApplyConstraints(MeasuredSize measured, int available_width,
                                  int available_height) const noexcept;
    void Invalidate() const;

    const config::ResolvedComponent& definition_;
    ComponentHost& host_;
    RECT bounds_{};
    Component* parent_ = nullptr;
    std::vector<std::unique_ptr<Component>> children_;
};

int ScaleDip(int value, UINT dpi) noexcept;
std::wstring ResolveText(const config::TextValue& value);
std::wstring ResolveAutomationName(const config::ResolvedComponent& definition,
                                   std::wstring fallback);
std::wstring Utf8ToWide(std::string_view value);
bool PointInRectInclusive(const RECT& bounds, POINT point) noexcept;

}  // namespace ui::components
