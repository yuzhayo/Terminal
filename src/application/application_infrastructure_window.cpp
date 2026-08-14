#include "application/application_infrastructure_window.h"

#include <shellapi.h>
#include <windowsx.h>

#include <utility>

namespace application {

ApplicationInfrastructureWindow::~ApplicationInfrastructureWindow() {
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
}

bool ApplicationInfrastructureWindow::Create(
    HINSTANCE instance, IpcHandler ipc_handler, RouteValidator route_validator,
    ProcessSignalHandler process_signal_handler, TrayHandler tray_handler,
    TaskbarCreatedHandler taskbar_created_handler,
    ApplicationWorkHandler application_work_handler, std::wstring& diagnostic) {
    if (window_) {
        diagnostic = L"Application infrastructure window sudah dibuat.";
        return false;
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = platform::InfrastructureWindowClassName();
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        diagnostic = L"Application infrastructure window class tidak dapat diregistrasikan.";
        return false;
    }

    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbar_created_message_ == 0) {
        diagnostic = L"TaskbarCreated message tidak dapat diregistrasikan.";
        return false;
    }

    ipc_handler_ = std::move(ipc_handler);
    route_validator_ = std::move(route_validator);
    process_signal_handler_ = std::move(process_signal_handler);
    tray_handler_ = std::move(tray_handler);
    taskbar_created_handler_ = std::move(taskbar_created_handler);
    application_work_handler_ = std::move(application_work_handler);

    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              platform::InfrastructureWindowClassName(), nullptr, WS_POPUP,
                              0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!window_) {
        diagnostic = L"Hidden application infrastructure window tidak dapat dibuat.";
        return false;
    }
    diagnostic.clear();
    return true;
}

void ApplicationInfrastructureWindow::BeginShutdown() noexcept {
    shutdown_in_progress_ = true;
    ipc_queue_.clear();
}

bool ApplicationInfrastructureWindow::PostApplicationWork() noexcept {
    return window_ && PostMessageW(window_, kApplicationWorkMessage, 0, 0) != FALSE;
}

HWND ApplicationInfrastructureWindow::hwnd() const noexcept { return window_; }

UINT ApplicationInfrastructureWindow::theme_signal_message() const noexcept {
    return kThemeSignalMessage;
}

UINT ApplicationInfrastructureWindow::tray_callback_message() const noexcept {
    return kTrayCallbackMessage;
}

UINT ApplicationInfrastructureWindow::taskbar_created_message() const noexcept {
    return taskbar_created_message_;
}

LRESULT CALLBACK ApplicationInfrastructureWindow::WindowProcedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* owner = reinterpret_cast<ApplicationInfrastructureWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        owner = static_cast<ApplicationInfrastructureWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
        owner->window_ = window;
    }
    return owner ? owner->HandleMessage(message, wparam, lparam)
                 : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT ApplicationInfrastructureWindow::HandleMessage(UINT message, WPARAM wparam,
                                                        LPARAM lparam) {
    if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
        if (taskbar_created_handler_) taskbar_created_handler_();
        return 0;
    }
    if (message == WM_COPYDATA) return ReceiveCopyData(lparam);
    if (message == kDrainIpcQueueMessage) {
        DrainIpcQueue();
        return 0;
    }
    if (message == kThemeSignalMessage) {
        QueueProcessSignal(ProcessGlobalSignal::Theme);
        return 0;
    }
    if (message == kDrainProcessSignalsMessage) {
        DrainProcessSignals();
        return 0;
    }
    if (message == kApplicationWorkMessage) {
        if (application_work_handler_) application_work_handler_();
        return 0;
    }
    if (message == kTrayCallbackMessage) {
        const UINT event = LOWORD(static_cast<DWORD_PTR>(lparam));
        POINT point{};
        if (event == WM_CONTEXTMENU) {
            point.x = GET_X_LPARAM(wparam);
            point.y = GET_Y_LPARAM(wparam);
        } else {
            GetCursorPos(&point);
        }
        if (tray_handler_ &&
            (event == NIN_SELECT || event == NIN_KEYSELECT || event == WM_LBUTTONUP)) {
            tray_handler_(TrayInteraction::ActivateDefault, point);
        } else if (tray_handler_ &&
                   (event == WM_CONTEXTMENU || event == WM_RBUTTONUP)) {
            tray_handler_(TrayInteraction::OpenContextMenu, point);
        }
        return 0;
    }

    switch (message) {
        case WM_SYSCOLORCHANGE:
            QueueProcessSignal(ProcessGlobalSignal::SystemColors);
            return 0;
        case WM_SETTINGCHANGE:
            QueueProcessSignal(ProcessGlobalSignal::Settings);
            return 0;
        case WM_THEMECHANGED:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            QueueProcessSignal(ProcessGlobalSignal::Theme);
            return 0;
        case WM_DISPLAYCHANGE:
            QueueProcessSignal(ProcessGlobalSignal::Display);
            return 0;
        case WM_CLOSE:
            return 0;
        case WM_NCDESTROY:
            window_ = nullptr;
            return 0;
        default:
            return DefWindowProcW(window_, message, wparam, lparam);
    }
}

LRESULT ApplicationInfrastructureWindow::ReceiveCopyData(LPARAM lparam) {
    if (shutdown_in_progress_) {
        return platform::PackIpcResult(platform::IpcStatus::Busy,
                                       platform::IpcError::ShutdownInProgress);
    }
    const auto* payload = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
    if (!payload || payload->dwData != platform::kIpcCopyDataId || !payload->lpData) {
        return platform::PackIpcResult(platform::IpcStatus::Rejected,
                                       platform::IpcError::InvalidPayload);
    }
    const platform::IpcParseResult parsed =
        platform::ParseIpcPayload(payload->lpData, payload->cbData);
    if (!parsed.request) {
        return platform::PackIpcResult(platform::IpcStatus::Rejected, parsed.error);
    }
    if (parsed.request->command == platform::IpcCommand::OpenRoute &&
        (!route_validator_ || !route_validator_(parsed.request->route_id))) {
        return platform::PackIpcResult(platform::IpcStatus::Rejected,
                                       platform::IpcError::InvalidRoute);
    }

    const ULONGLONG now = GetTickCount64();
    if (IsRecentRequest(parsed.request->request_id, now)) {
        return platform::PackIpcResult(platform::IpcStatus::Accepted,
                                       platform::IpcError::None);
    }
    if (ipc_queue_.size() >= kMaximumQueueDepth) {
        return platform::PackIpcResult(platform::IpcStatus::Busy,
                                       platform::IpcError::QueueFull);
    }

    ipc_queue_.push_back(*parsed.request);
    recent_requests_.emplace_back(parsed.request->request_id, now);
    while (recent_requests_.size() > kMaximumRecentRequests) recent_requests_.pop_front();
    if (!PostMessageW(window_, kDrainIpcQueueMessage, 0, 0)) {
        ipc_queue_.pop_back();
        recent_requests_.pop_back();
        return platform::PackIpcResult(platform::IpcStatus::Busy,
                                       platform::IpcError::QueueFull);
    }
    return platform::PackIpcResult(platform::IpcStatus::Accepted,
                                   platform::IpcError::None);
}

void ApplicationInfrastructureWindow::DrainIpcQueue() {
    while (!ipc_queue_.empty() && !shutdown_in_progress_) {
        platform::IpcRequest request = std::move(ipc_queue_.front());
        ipc_queue_.pop_front();
        if (ipc_handler_) ipc_handler_(request);
    }
}

void ApplicationInfrastructureWindow::QueueProcessSignal(ProcessGlobalSignal signal) {
    pending_process_signals_ |= SignalMask(signal);
    if (process_signal_posted_) return;
    process_signal_posted_ = true;
    if (!PostMessageW(window_, kDrainProcessSignalsMessage, 0, 0)) {
        process_signal_posted_ = false;
        DrainProcessSignals();
    }
}

void ApplicationInfrastructureWindow::DrainProcessSignals() {
    const std::uint32_t signals = std::exchange(pending_process_signals_, 0);
    process_signal_posted_ = false;
    if (signals != 0 && process_signal_handler_) process_signal_handler_(signals);
}

bool ApplicationInfrastructureWindow::IsRecentRequest(const std::string& request_id,
                                                       ULONGLONG now) {
    while (!recent_requests_.empty() &&
           now - recent_requests_.front().second > kRecentRequestLifetimeMs) {
        recent_requests_.pop_front();
    }
    for (const auto& [recent_id, timestamp] : recent_requests_) {
        (void)timestamp;
        if (recent_id == request_id) return true;
    }
    return false;
}

}  // namespace application
