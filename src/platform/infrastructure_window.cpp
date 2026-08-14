#include "platform/infrastructure_window.h"

#include <utility>

namespace platform {

InfrastructureWindow::~InfrastructureWindow() {
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
}

bool InfrastructureWindow::Create(HINSTANCE instance, IpcHandler ipc_handler,
                                  SignalHandler theme_handler, std::wstring& diagnostic) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = InfrastructureWindowClassName();
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        diagnostic = L"Infrastructure window class tidak dapat diregistrasikan.";
        return false;
    }
    ipc_handler_ = std::move(ipc_handler);
    theme_handler_ = std::move(theme_handler);
    window_ = CreateWindowExW(0, InfrastructureWindowClassName(), nullptr, 0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, instance, this);
    if (!window_) {
        diagnostic = L"Infrastructure message-only window tidak dapat dibuat.";
        return false;
    }
    diagnostic.clear();
    return true;
}

void InfrastructureWindow::BeginShutdown() noexcept { shutdown_in_progress_ = true; }
HWND InfrastructureWindow::hwnd() const noexcept { return window_; }
UINT InfrastructureWindow::theme_signal_message() const noexcept { return kThemeSignalMessage; }

LRESULT CALLBACK InfrastructureWindow::WindowProcedure(HWND window, UINT message, WPARAM wparam,
                                                         LPARAM lparam) {
    auto* owner = reinterpret_cast<InfrastructureWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        owner = static_cast<InfrastructureWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
        owner->window_ = window;
    }
    return owner ? owner->HandleMessage(message, wparam, lparam)
                 : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT InfrastructureWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_COPYDATA) return ReceiveCopyData(lparam);
    if (message == kDrainQueueMessage) {
        DrainQueue();
        return 0;
    }
    if (message == kThemeSignalMessage) {
        if (theme_handler_) theme_handler_();
        return 0;
    }
    if (message == WM_NCDESTROY) {
        window_ = nullptr;
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

LRESULT InfrastructureWindow::ReceiveCopyData(LPARAM lparam) {
    if (shutdown_in_progress_) {
        return PackIpcResult(IpcStatus::Busy, IpcError::ShutdownInProgress);
    }
    const auto* payload = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
    if (!payload || payload->dwData != kIpcCopyDataId || !payload->lpData) {
        return PackIpcResult(IpcStatus::Rejected, IpcError::InvalidPayload);
    }
    const IpcParseResult parsed = ParseIpcPayload(payload->lpData, payload->cbData);
    if (!parsed.request) return PackIpcResult(IpcStatus::Rejected, parsed.error);

    const ULONGLONG now = GetTickCount64();
    if (IsRecentRequest(parsed.request->request_id, now)) {
        return PackIpcResult(IpcStatus::Accepted, IpcError::None);
    }
    if (queue_.size() >= kMaximumQueueDepth) {
        return PackIpcResult(IpcStatus::Busy, IpcError::QueueFull);
    }
    queue_.push_back(*parsed.request);
    recent_requests_.emplace_back(parsed.request->request_id, now);
    while (recent_requests_.size() > kMaximumRecentRequests) recent_requests_.pop_front();
    if (!PostMessageW(window_, kDrainQueueMessage, 0, 0)) {
        queue_.pop_back();
        recent_requests_.pop_back();
        return PackIpcResult(IpcStatus::Busy, IpcError::QueueFull);
    }
    return PackIpcResult(IpcStatus::Accepted, IpcError::None);
}

void InfrastructureWindow::DrainQueue() {
    while (!queue_.empty() && !shutdown_in_progress_) {
        IpcRequest request = std::move(queue_.front());
        queue_.pop_front();
        if (ipc_handler_) ipc_handler_(request);
    }
}

bool InfrastructureWindow::IsRecentRequest(const std::string& request_id, ULONGLONG now) {
    while (!recent_requests_.empty() && now - recent_requests_.front().second > kRecentRequestLifetimeMs) {
        recent_requests_.pop_front();
    }
    for (const auto& [recent_id, timestamp] : recent_requests_) {
        (void)timestamp;
        if (recent_id == request_id) return true;
    }
    return false;
}

}  // namespace platform
