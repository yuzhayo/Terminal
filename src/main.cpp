#include <windows.h>

namespace {

constexpr wchar_t kWindowClass[] = L"OpenTerminalNative.Phase0";
constexpr wchar_t kWindowTitle[] = L"Open Terminal Native";

struct BackBuffer {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous_bitmap = nullptr;
    int width = 0;
    int height = 0;

    void Reset() {
        if (dc && previous_bitmap) {
            SelectObject(dc, previous_bitmap);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (dc) {
            DeleteDC(dc);
        }
        dc = nullptr;
        bitmap = nullptr;
        previous_bitmap = nullptr;
        width = 0;
        height = 0;
    }

    bool Ensure(HDC reference, int new_width, int new_height) {
        if (new_width <= 0 || new_height <= 0) {
            return false;
        }
        if (dc && width == new_width && height == new_height) {
            return true;
        }

        HDC candidate_dc = CreateCompatibleDC(reference);
        if (!candidate_dc) {
            return false;
        }

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = new_width;
        info.bmiHeader.biHeight = -new_height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        HBITMAP candidate_bitmap = CreateDIBSection(reference, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!candidate_bitmap || !pixels) {
            if (candidate_bitmap) {
                DeleteObject(candidate_bitmap);
            }
            DeleteDC(candidate_dc);
            return false;
        }

        HGDIOBJ candidate_previous = SelectObject(candidate_dc, candidate_bitmap);
        if (!candidate_previous || candidate_previous == HGDI_ERROR) {
            DeleteObject(candidate_bitmap);
            DeleteDC(candidate_dc);
            return false;
        }

        Reset();
        dc = candidate_dc;
        bitmap = candidate_bitmap;
        previous_bitmap = candidate_previous;
        width = new_width;
        height = new_height;
        return true;
    }

    void PaintBackground() const {
        RECT bounds{0, 0, width, height};
        FillRect(dc, &bounds, GetSysColorBrush(COLOR_WINDOW));
    }
};

BackBuffer g_back_buffer;

void PaintWindow(HWND window) {
    PAINTSTRUCT paint{};
    HDC window_dc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    if (g_back_buffer.Ensure(window_dc, width, height)) {
        g_back_buffer.PaintBackground();
        const int paint_width = paint.rcPaint.right - paint.rcPaint.left;
        const int paint_height = paint.rcPaint.bottom - paint.rcPaint.top;
        BitBlt(window_dc, paint.rcPaint.left, paint.rcPaint.top, paint_width, paint_height, g_back_buffer.dc,
               paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
    } else {
        FillRect(window_dc, &paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
    }

    EndPaint(window, &paint);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
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
            g_back_buffer.Reset();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wparam, lparam);
    }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show_command) {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&window_class)) {
        return 1;
    }

    HWND window = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, kWindowTitle,
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
