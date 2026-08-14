#pragma once

#include <windows.h>

#include <memory>
#include <optional>
#include <string>

#include "rendering/render_runtime.h"
#include "rendering/window_render_context.h"
#include "platform/single_instance.h"
#include "ui/application/stub_application_bridge.h"
#include "ui/accessibility/automation_provider.h"
#include "ui/components/component.h"
#include "ui/components/component_registry.h"
#include "ui/config/resolved_ui_document.h"
#include "ui/containers/logical_focus_coordinator.h"
#include "ui/containers/modal_overlay_stack.h"
#include "ui/containers/overlay_plane.h"

namespace ui::containers {

class WindowContainer final {
public:
    WindowContainer(HINSTANCE instance, rendering::RenderRuntime& render_runtime,
                    std::shared_ptr<const config::ResolvedUiDocument> document,
                    config::ThemeKind theme_kind);
    ~WindowContainer();

    WindowContainer(const WindowContainer&) = delete;
    WindowContainer& operator=(const WindowContainer&) = delete;

    bool Create(const std::string& window_id, std::wstring& diagnostic);
    bool PrepareFirstFrame(std::wstring& diagnostic);
    bool SuspendNativePeers(std::wstring& diagnostic);
    void ResumeNativePeers();
    void Show(int show_command);
    void ApplyTheme(config::ThemeKind theme_kind);
    void HandleIpcRequest(const platform::IpcRequest& request);
    HWND hwnd() const noexcept;

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    bool BuildComponentTree(std::wstring& diagnostic);
    bool RenderCompleteFrame(HDC reference);
    bool RenderFrame(HDC reference, const RECT& requested_region, bool force_full);
    bool PrepareRenderResources();
    void TraceInputStart();
    void Layout();
    void TrackPointer(POINT point);
    void DispatchStubEvent(const config::EventDefinition& event);
    bool OpenModal(std::string_view dialog_id, std::wstring& diagnostic);
    bool CloseModal(components::ModalResult result, std::wstring& diagnostic);
    components::Component* HitTestInteractive(POINT point) const;

    HINSTANCE instance_ = nullptr;
    rendering::RenderRuntime& render_runtime_;
    std::shared_ptr<const config::ResolvedUiDocument> document_;
    config::ThemeKind theme_kind_ = config::ThemeKind::Light;
    const config::ResolvedComponent* window_definition_ = nullptr;
    HWND window_ = nullptr;
    UINT dpi_ = 96;
    rendering::WindowRenderContext render_context_;
    components::ComponentRegistry registry_;
    LogicalFocusCoordinator focus_coordinator_;
    ModalOverlayStack modal_stack_;
    OverlayPlane overlay_plane_;
    std::unique_ptr<components::ComponentHost> component_host_;
    std::unique_ptr<components::Component> root_;
    accessibility::AutomationRootProvider* automation_provider_ = nullptr;
    components::Component* pointer_target_ = nullptr;
    components::Component* active_popup_owner_ = nullptr;
    application::StubApplicationBridge application_bridge_;
    std::optional<std::uint64_t> pending_input_correlation_;
    std::optional<std::uint64_t> pending_resize_correlation_;
    std::optional<std::uint64_t> pending_navigation_correlation_;
    std::uint64_t last_scenario_correlation_ = 0;
    bool resources_prepared_ = false;
    bool frame_ready_ = false;
};

}  // namespace ui::containers
