#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

#include "platform/single_instance.h"
#include "platform/updater.h"
#include "rendering/gdi_renderer.h"

namespace {

constexpr wchar_t kWindowTitle[] = L"Open Terminal Native";

rendering::GdiRenderer g_renderer;

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

    if (!g_renderer.Paint(window_dc, width, height, paint.rcPaint)) {
        FillRect(window_dc, &paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
    }

    EndPaint(window, &paint);
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
    updater::RunStartupHooks();

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

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = platform::MainWindowClassName();

    if (!RegisterClassExW(&window_class)) {
        return 1;
    }

    HWND window = CreateWindowExW(WS_EX_APPWINDOW, platform::MainWindowClassName(), kWindowTitle,
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 760,
                                  520, nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 2;
    }

    ShowWindow(window, show_command == 0 ? SW_SHOWNORMAL : show_command);
    UpdateWindow(window);

    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return result == -1 ? 3 : static_cast<int>(message.wParam);
}
