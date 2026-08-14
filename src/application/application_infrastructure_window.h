#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "platform/single_instance.h"

namespace application {

enum class ProcessGlobalSignal : std::uint32_t {
    SystemColors = 1u << 0,
    Settings = 1u << 1,
    Display = 1u << 2,
    Theme = 1u << 3,
};

constexpr std::uint32_t SignalMask(ProcessGlobalSignal signal) noexcept {
    return static_cast<std::uint32_t>(signal);
}

enum class TrayInteraction { ActivateDefault, OpenContextMenu };

class ApplicationInfrastructureWindow final {
public:
    using IpcHandler = std::function<void(const platform::IpcRequest&)>;
    using RouteValidator = std::function<bool(std::string_view)>;
    using ProcessSignalHandler = std::function<void(std::uint32_t)>;
    using TrayHandler = std::function<void(TrayInteraction, POINT)>;
    using TaskbarCreatedHandler = std::function<void()>;
    using ApplicationWorkHandler = std::function<void()>;

    ApplicationInfrastructureWindow() = default;
    ~ApplicationInfrastructureWindow();

    ApplicationInfrastructureWindow(const ApplicationInfrastructureWindow&) = delete;
    ApplicationInfrastructureWindow& operator=(const ApplicationInfrastructureWindow&) = delete;

    bool Create(HINSTANCE instance, IpcHandler ipc_handler, RouteValidator route_validator,
                ProcessSignalHandler process_signal_handler, TrayHandler tray_handler,
                TaskbarCreatedHandler taskbar_created_handler,
                ApplicationWorkHandler application_work_handler,
                std::wstring& diagnostic);
    void BeginShutdown() noexcept;
    bool PostApplicationWork() noexcept;

    HWND hwnd() const noexcept;
    UINT theme_signal_message() const noexcept;
    UINT tray_callback_message() const noexcept;
    UINT taskbar_created_message() const noexcept;

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam,
                                            LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT ReceiveCopyData(LPARAM lparam);
    void DrainIpcQueue();
    void QueueProcessSignal(ProcessGlobalSignal signal);
    void DrainProcessSignals();
    bool IsRecentRequest(const std::string& request_id, ULONGLONG now);

    static constexpr UINT kDrainIpcQueueMessage = WM_APP + 0x120;
    static constexpr UINT kThemeSignalMessage = WM_APP + 0x121;
    static constexpr UINT kTrayCallbackMessage = WM_APP + 0x122;
    static constexpr UINT kDrainProcessSignalsMessage = WM_APP + 0x123;
    static constexpr UINT kApplicationWorkMessage = WM_APP + 0x124;
    static constexpr std::size_t kMaximumQueueDepth = 64;
    static constexpr std::size_t kMaximumRecentRequests = 128;
    static constexpr ULONGLONG kRecentRequestLifetimeMs = 120000;

    HWND window_ = nullptr;
    UINT taskbar_created_message_ = 0;
    IpcHandler ipc_handler_;
    RouteValidator route_validator_;
    ProcessSignalHandler process_signal_handler_;
    TrayHandler tray_handler_;
    TaskbarCreatedHandler taskbar_created_handler_;
    ApplicationWorkHandler application_work_handler_;
    std::deque<platform::IpcRequest> ipc_queue_;
    std::deque<std::pair<std::string, ULONGLONG>> recent_requests_;
    std::uint32_t pending_process_signals_ = 0;
    bool process_signal_posted_ = false;
    bool shutdown_in_progress_ = false;
};

}  // namespace application
