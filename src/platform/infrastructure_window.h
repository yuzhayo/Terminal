#pragma once

#include <windows.h>

#include <deque>
#include <functional>
#include <string>
#include <utility>

#include "platform/single_instance.h"

namespace platform {

class InfrastructureWindow final {
public:
    using IpcHandler = std::function<void(const IpcRequest&)>;
    using SignalHandler = std::function<void()>;

    InfrastructureWindow() = default;
    ~InfrastructureWindow();

    InfrastructureWindow(const InfrastructureWindow&) = delete;
    InfrastructureWindow& operator=(const InfrastructureWindow&) = delete;

    bool Create(HINSTANCE instance, IpcHandler ipc_handler, SignalHandler theme_handler,
                std::wstring& diagnostic);
    void BeginShutdown() noexcept;
    HWND hwnd() const noexcept;
    UINT theme_signal_message() const noexcept;

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT ReceiveCopyData(LPARAM lparam);
    void DrainQueue();
    bool IsRecentRequest(const std::string& request_id, ULONGLONG now);

    static constexpr UINT kDrainQueueMessage = WM_APP + 0x120;
    static constexpr UINT kThemeSignalMessage = WM_APP + 0x121;
    static constexpr std::size_t kMaximumQueueDepth = 64;
    static constexpr std::size_t kMaximumRecentRequests = 128;
    static constexpr ULONGLONG kRecentRequestLifetimeMs = 120000;

    HWND window_ = nullptr;
    IpcHandler ipc_handler_;
    SignalHandler theme_handler_;
    std::deque<IpcRequest> queue_;
    std::deque<std::pair<std::string, ULONGLONG>> recent_requests_;
    bool shutdown_in_progress_ = false;
};

}  // namespace platform
