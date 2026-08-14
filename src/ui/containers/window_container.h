#pragma once

#include <windows.h>

#include <memory>
#include <map>
#include <optional>
#include <string>

#include "rendering/render_runtime.h"
#include "rendering/window_render_context.h"
#include "platform/single_instance.h"
#include "ui/application/ui_application_bridge.h"
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
                    config::ThemeKind theme_kind,
                    std::shared_ptr<application::UiApplicationBridge> application_bridge);
    ~WindowContainer();

    WindowContainer(const WindowContainer&) = delete;
    WindowContainer& operator=(const WindowContainer&) = delete;

    bool Create(const std::string& window_id, std::wstring& diagnostic);
    bool PrepareFirstFrame(std::wstring& diagnostic);
    bool SuspendNativePeers(std::wstring& diagnostic);
    void ResumeNativePeers();
    void Show(int show_command);
    void ApplyTheme(config::ThemeKind theme_kind);
    bool Navigate(std::string_view route_id, std::wstring& diagnostic);
    bool ReloadDocument(std::shared_ptr<const config::ResolvedUiDocument> document,
                        std::wstring& diagnostic);
    void HandleIpcRequest(const platform::IpcRequest& request);
    HWND hwnd() const noexcept;
    std::string_view active_route() const noexcept;
    std::size_t cached_screen_count() const noexcept;
    bool IsDirty() const;
    std::size_t dirty_participant_count() const;
    std::uint64_t document_generation() const noexcept;

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    bool BuildComponentTree(std::wstring& diagnostic);
    bool ActivateRoute(std::string_view route_id, std::wstring& diagnostic);
    bool NormalizeForReload(std::wstring& diagnostic);
    void CaptureScreenSnapshots();
    bool InstallDocument(std::shared_ptr<const config::ResolvedUiDocument> document,
                         std::string_view preferred_route, std::wstring& diagnostic);
    void ResetAutomationProvider();
    bool RenderCompleteFrame(HDC reference);
    bool RenderFrame(HDC reference, const RECT& requested_region, bool force_full);
    bool PrepareRenderResources();
    void TraceInputStart();
    void Layout();
    void TrackPointer(POINT point);
    void DispatchUiEvent(components::Component& source, std::string_view event_type,
                         const config::EventDefinition& event);
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
    struct ScreenRuntimeSnapshot {
        components::ComponentRuntimeStateMap component_states;
        std::string focused_component_id;
    };
    struct ScreenEntry {
        std::uint64_t instance_id = 0;
        std::unique_ptr<components::Component> root;
        std::string focused_component_id;
        bool suspended = false;
    };
    std::map<std::string, ScreenEntry, std::less<>> screen_cache_;
    std::map<std::string, ScreenRuntimeSnapshot, std::less<>> pending_screen_snapshots_;
    components::Component* root_ = nullptr;
    std::string window_id_;
    std::string active_route_;
    std::uint64_t window_instance_id_ = 0;
    std::uint64_t active_screen_instance_id_ = 0;
    accessibility::AutomationRootProvider* automation_provider_ = nullptr;
    components::Component* pointer_target_ = nullptr;
    components::Component* active_popup_owner_ = nullptr;
    std::shared_ptr<application::UiApplicationBridge> application_bridge_;
    std::optional<std::uint64_t> pending_input_correlation_;
    std::optional<std::uint64_t> pending_resize_correlation_;
    std::optional<std::uint64_t> pending_navigation_correlation_;
    std::uint64_t last_scenario_correlation_ = 0;
    bool resources_prepared_ = false;
    bool frame_ready_ = false;
};

}  // namespace ui::containers
