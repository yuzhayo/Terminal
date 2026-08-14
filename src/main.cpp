#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

#include "app/app_identity.h"
#include "instrumentation/performance_trace.h"
#include "platform/app_paths.h"
#include "platform/single_instance.h"
#include "platform/updater.h"
#include "platform/windows_runtime.h"
#include "rendering/gdi_renderer.h"
#include "resource.h"
#include "ui/config/ui_config_gate.h"

namespace {

rendering::GdiRenderer g_renderer;
bool g_render_buffer_ready_traced = false;
bool g_first_present_traced = false;

void ShowBootstrapError(const std::wstring& diagnostic) {
    MessageBoxW(nullptr, diagnostic.c_str(), app_identity::kProductName,
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

bool HasSwitch(const std::wstring& command_line, const wchar_t* expected) {
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(command_line.c_str(), &argument_count);
    if (!arguments) {
        return false;
    }

    bool found = false;
    for (int index = 1; index < argument_count; ++index) {
        if (_wcsicmp(arguments[index], expected) == 0) {
            found = true;
            break;
        }
    }
    LocalFree(arguments);
    return found;
}

void PaintWindow(HWND window) {
    PAINTSTRUCT paint{};
    HDC window_dc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    const bool presented = g_renderer.Paint(window_dc, width, height, paint.rcPaint);
    if (!presented) {
        FillRect(window_dc, &paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
    }

    EndPaint(window, &paint);

    if (presented && !g_render_buffer_ready_traced) {
        g_render_buffer_ready_traced = true;
        instrumentation::TraceRenderBufferReady();
    }
    if (presented && !g_first_present_traced) {
        g_first_present_traced = true;
        instrumentation::TraceFirstPresentComplete();
    }
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_COPYDATA: {
            const auto command = platform::ReadForwardedCommand(lparam);
            if (!command) {
                return FALSE;
            }
            if (HasSwitch(*command, L"--exit")) {
                DestroyWindow(window);
            } else {
                platform::ActivateMainWindow(window);
            }
            return TRUE;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintWindow(window);
            return 0;
        case WM_SIZE:
        case WM_SYSCOLORCHANGE:
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_DESTROY:
            g_renderer.Reset();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wparam, lparam);
    }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show_command) {
    const updater::StartupHookTiming startup_timing = updater::RunStartupHooks();
    instrumentation::PerformanceTraceSession trace_session(startup_timing.process_entry_qpc,
                                                           startup_timing.hooks_complete_qpc);

    std::wstring diagnostic;
    if (platform::CheckWindowsRuntime(diagnostic) != platform::WindowsRuntimeStatus::Supported) {
        ShowBootstrapError(diagnostic);
        return 11;
    }

    const std::wstring command_line = GetCommandLineW();
    if (HasSwitch(command_line, L"--update-now")) {
        return updater::CheckDownloadAndApply() == updater::UpdateResult::Failed ? 10 : 0;
    }

    platform::SingleInstance single_instance;
    const platform::InstanceClaim claim = single_instance.Claim(command_line);
    if (claim == platform::InstanceClaim::SecondaryNotified) {
        return 0;
    }
    if (claim == platform::InstanceClaim::Error) {
        return 4;
    }
    if (HasSwitch(command_line, L"--exit")) {
        return 0;
    }

    platform::AppPaths paths;
    if (!platform::ResolveAppPaths(paths, diagnostic)) {
        ShowBootstrapError(diagnostic);
        return 12;
    }

    ui::config::UiConfigGate config_gate(instance, paths);
    if (!config_gate.ResolveBootstrap(diagnostic)) {
        ShowBootstrapError(diagnostic);
        return 13;
    }
    if (config_gate.active_diagnostic()) {
        ShowBootstrapError(config_gate.active_diagnostic_text());
    }
    instrumentation::TraceConfigResolved();

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    window_class.hIconSm = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_SHARED));
    window_class.lpszClassName = platform::MainWindowClassName();

    if (!window_class.hIcon || !window_class.hIconSm) {
        ShowBootstrapError(L"Icon aplikasi tidak dapat dimuat dari executable.");
        return 14;
    }

    if (!RegisterClassExW(&window_class)) {
        return 1;
    }

    HWND window = CreateWindowExW(WS_EX_APPWINDOW, platform::MainWindowClassName(),
                                  app_identity::kProductName,
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 760,
                                  520, nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 2;
    }
    instrumentation::TraceFirstLayoutComplete();

    ShowWindow(window, show_command == 0 ? SW_SHOWNORMAL : show_command);
    UpdateWindow(window);
    if (g_first_present_traced && SUCCEEDED(DwmFlush())) {
        instrumentation::TraceFirstFrameVisible();
        instrumentation::TraceResourceSnapshot();
    }

    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return result == -1 ? 3 : static_cast<int>(message.wParam);
}
