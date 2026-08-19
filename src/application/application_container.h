#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "application/application_infrastructure_window.h"
#include "platform/jump_list.h"
#include "platform/single_instance.h"
#include "rendering/render_runtime.h"
#include "ui/application/ui_application_bridge.h"
#include "ui/config/resolved_ui_document.h"
#include "ui/containers/window_container.h"
#include "ui/theme/theme_platform_adapter.h"

namespace application {

struct ApplicationContainerTestAccess;

struct ApplicationContainerOptions {
    bool enable_tray = true;
    int created_window_show_command = SW_SHOWNORMAL;
    // Eksperimen perilaku v1 (Open-terminal): setiap launch/jump list membuka
    // WindowContainer baru, bukan mengaktifkan window yang sudah ada. Mematikan
    // registry one-window-per-route beserta invariant-nya.
    bool allow_duplicate_route_windows = false;
};

class ApplicationContainer final {
public:
    ApplicationContainer(
        HINSTANCE instance, rendering::RenderRuntime& render_runtime,
        std::shared_ptr<const ui::config::ResolvedUiDocument> document,
        ui::theme::ThemePlatformAdapter& theme_adapter,
        std::shared_ptr<ui::application::UiApplicationBridge> application_bridge,
        ApplicationContainerOptions options = {});
    ~ApplicationContainer();

    ApplicationContainer(const ApplicationContainer&) = delete;
    ApplicationContainer& operator=(const ApplicationContainer&) = delete;

    bool Initialize(std::string window_id,
                    const std::optional<platform::IpcRequest>& startup_request,
                    std::wstring& diagnostic);
    bool PrepareAndShowInitialWindow(int show_command, std::wstring& diagnostic);
    bool StartThemeMonitoring(std::wstring& diagnostic) noexcept;
    bool OpenExternalRoute(std::string_view route_id, std::wstring& diagnostic);
    void ActivateDefault();
    void HandleIpcRequest(const platform::IpcRequest& request);
    void BeginShutdown() noexcept;

    // Route yang boleh muncul sebagai task jump list, diambil dari resolved UI
    // document (tabLabel + showInTabs) sehingga daftarnya tidak pernah basi.
    std::vector<platform::JumpListRoute> JumpListRoutes() const;

    ui::containers::WindowContainer* initial_window() noexcept;
    ui::containers::WindowContainer* FindRouteWindow(std::string_view route_id) noexcept;
    std::size_t window_count() const noexcept;
    std::size_t visible_window_count() const noexcept;
    ui::containers::WindowContainer* retained_window() noexcept;
    bool route_registry_is_unique() const noexcept;
    bool tray_available() const noexcept;
    HWND infrastructure_hwnd() const noexcept;
    UINT taskbar_created_message() const noexcept;
    const std::wstring& nonfatal_diagnostic() const noexcept;

private:
    friend struct ApplicationContainerTestAccess;

    struct WindowRecord {
        std::unique_ptr<ui::containers::WindowContainer> container;
    };

    enum class CloseOperationKind { None, CloseOne, ReplaceRetained, ExitAll };
    struct CloseOperation {
        CloseOperationKind kind = CloseOperationKind::None;
        std::uint64_t primary_id = 0;
        std::uint64_t secondary_id = 0;
        std::vector<std::uint64_t> window_ids;
        std::vector<std::uint64_t> prepared_ids;
        std::size_t next_index = 0;
        std::optional<std::uint64_t> original_retained_id;
    };

    ui::containers::WindowContainer* CreateRouteWindow(std::string_view route_id,
                                                        bool prepare_and_show,
                                                         int show_command,
                                                         std::wstring& diagnostic);
    bool HandleSameWindowRoute(ui::containers::WindowContainer& source,
                               std::string_view route_id, std::wstring& diagnostic);
    bool RetainRouteWindow(ui::containers::WindowContainer& target,
                           std::wstring& diagnostic);
    bool RestoreRetainedWindow(std::uint64_t registry_id, int show_command,
                               std::wstring& diagnostic);
    std::optional<std::uint64_t> FindWindowId(
        const ui::containers::WindowContainer& target) const noexcept;
    std::optional<std::uint64_t> FindRouteWindowId(
        std::string_view route_id) const noexcept;
    void AssertRouteRegistryInvariant() const noexcept;
    std::size_t reachable_window_count() const noexcept;
    void RequestCloseWindow(ui::containers::WindowContainer& target);
    void BeginCloseOne(std::uint64_t registry_id);
    void CompleteCloseOne(std::uint64_t registry_id,
                          ui::containers::ClosePreparation preparation);
    void BeginRetainedReplacement(std::uint64_t retained_id,
                                  std::uint64_t replacement_id);
    void CompleteRetainedReplacement(
        std::uint64_t retained_id, std::uint64_t replacement_id,
        ui::containers::ClosePreparation preparation);
    void BeginCloseAll();
    void ContinuePrepareCloseAll();
    void CompletePrepareCloseAllWindow(
        std::uint64_t registry_id, ui::containers::ClosePreparation preparation);
    void CancelCloseAll();
    void CommitCloseAll();
    void ClearCloseOperation() noexcept;
    void OnWindowDestroyed(std::uint64_t registry_id) noexcept;
    void DrainApplicationWork();
    void HandleProcessSignals(std::uint32_t signals);
    void HandleTrayInteraction(TrayInteraction interaction, POINT point);
    void HandleTaskbarCreated();
    void ShowTrayMenu(POINT point);
    bool InstallTrayIcon(std::wstring& diagnostic) noexcept;
    void RemoveTrayIcon() noexcept;
    void RequestExit();
    std::string DefaultRoute() const;
    bool IsConfiguredRoute(std::string_view route_id) const noexcept;

    HINSTANCE instance_ = nullptr;
    rendering::RenderRuntime& render_runtime_;
    std::shared_ptr<const ui::config::ResolvedUiDocument> document_;
    ui::theme::ThemePlatformAdapter& theme_adapter_;
    std::shared_ptr<ui::application::UiApplicationBridge> application_bridge_;
    ApplicationContainerOptions options_;
    ApplicationInfrastructureWindow infrastructure_window_;
    std::map<std::uint64_t, WindowRecord> windows_;
    std::vector<std::uint64_t> destroyed_window_ids_;
    std::string window_definition_id_;
    std::uint64_t initial_window_id_ = 0;
    std::optional<std::uint64_t> retained_window_id_;
    std::uint64_t next_window_id_ = 1;
    bool tray_icon_added_ = false;
    bool shutdown_in_progress_ = false;
    bool shutdown_after_drain_ = false;
    CloseOperation close_operation_;
    std::wstring nonfatal_diagnostic_;
};

}  // namespace application
