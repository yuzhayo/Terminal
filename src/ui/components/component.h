#pragma once

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rendering/render_runtime.h"
#include "ui/components/editable_draft_state.h"
#include "ui/config/resolved_ui_document.h"

namespace ui::components {

class Component;

struct MeasuredSize {
    int width = 0;
    int height = 0;
};

struct ComponentHost {
    HWND window = nullptr;
    UINT dpi = 96;
    HDC layout_dc = nullptr;
    rendering::RenderRuntime* render_runtime = nullptr;
    const config::ResolvedTheme* theme = nullptr;
    std::function<void(const RECT&)> invalidate;
    std::function<void(const config::EventDefinition&)> dispatch_event;
    std::function<void(Component*, bool)> native_focus_changed;
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
    virtual bool HandleCommand(HWND source, WORD notification);
    virtual HBRUSH HandleControlColor(HDC dc, HWND source);
    virtual bool OwnsNativePeer(HWND source) const noexcept;
    virtual bool CanFocus() const noexcept;
    virtual bool FocusNativePeer();
    virtual void SetLogicalFocus(bool focused, bool window_active);
    virtual bool HandleKeyDown(UINT virtual_key);
    virtual void CollectFocusable(std::vector<Component*>& focusable);
    virtual bool SuspendNativePeers(std::wstring& diagnostic);
    virtual void ResumeNativePeers();
    virtual void CollectEditableParticipants(std::vector<EditableParticipant*>& participants);
    virtual void OnDpiChanged();
    virtual void AddChild(std::unique_ptr<Component> child);

    const RECT& bounds() const noexcept;
    const config::ResolvedComponent& definition() const noexcept;
    const config::ResolvedStyle& style() const;
    bool visible() const noexcept;
    bool enabled() const noexcept;

protected:
    void PaintStyleBox(HDC dc, config::VisualState state, const RECT& bounds) const;
    void PaintChildren(HDC dc);
    MeasuredSize ApplyConstraints(MeasuredSize measured, int available_width,
                                  int available_height) const noexcept;
    void Invalidate() const;

    const config::ResolvedComponent& definition_;
    ComponentHost& host_;
    RECT bounds_{};
    std::vector<std::unique_ptr<Component>> children_;
};

int ScaleDip(int value, UINT dpi) noexcept;
std::wstring ResolveText(const config::TextValue& value);
bool PointInRectInclusive(const RECT& bounds, POINT point) noexcept;

}  // namespace ui::components
