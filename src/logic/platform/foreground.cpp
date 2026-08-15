#include "platform/foreground.h"

#include <windows.h>

namespace platform {

bool ChromeWindowExists() {
    struct Finder {
        static BOOL CALLBACK Proc(HWND hwnd, LPARAM param) {
            if (!IsWindowVisible(hwnd)) return TRUE;
            wchar_t class_name[64]{};
            GetClassNameW(hwnd, class_name, 64);
            if (lstrcmpW(class_name, L"Chrome_WidgetWin_1") != 0) return TRUE;
            if (GetWindowTextLengthW(hwnd) == 0) return TRUE;
            *reinterpret_cast<bool*>(param) = true;
            return FALSE;
        }
    };
    bool found = false;
    EnumWindows(Finder::Proc, reinterpret_cast<LPARAM>(&found));
    return found;
}

void FocusChromeWindow() {
    struct Raiser {
        static BOOL CALLBACK Proc(HWND hwnd, LPARAM) {
            wchar_t class_name[64]{};
            GetClassNameW(hwnd, class_name, 64);
            if (IsWindowVisible(hwnd) && lstrcmpW(class_name, L"Chrome_WidgetWin_1") == 0 &&
                GetWindowTextLengthW(hwnd) > 0) {
                SetForegroundWindow(hwnd);
                return FALSE;
            }
            return TRUE;
        }
    };
    EnumWindows(Raiser::Proc, 0);
}

}  // namespace platform
