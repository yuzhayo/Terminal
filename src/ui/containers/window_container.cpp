#include "ui/containers/window_container.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cwchar>
#include <exception>

#include "app/app_identity.h"
#include "platform/single_instance.h"

namespace ui::containers {
namespace {

std::wstring ResolveWindowTitle(const config::ResolvedComponent& definition) {
    const auto& properties = std::get<config::WindowProperties>(definition.properties);
    return components::ResolveText(properties.title);
}

bool HasSwitch(const std::wstring& command_line, const wchar_t* expected) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(command_line.c_str(), &count);
    if (!arguments) return false;
    bool found = false;
    for (int index = 1; index < count; ++index) {
        if (_wcsicmp(arguments[index], expected) == 0) {
            found = true;
            break;
        }
    }
    LocalFree(arguments);
    return found;
}

}  // namespace

WindowContainer::WindowContainer(HINSTANCE instance, rendering::RenderRuntime& render_runtime,
                                 std::shared_ptr<const config::ResolvedUiDocument> document,
                                 config::ThemeKind theme_kind)
    : instance_(instance), render_runtime_(render_runtime), document_(std::move(document)),
      theme_kind_(theme_kind) {}

WindowContainer::~WindowContainer() {
    root_.reset();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
}

bool WindowContainer::Create(const std::string& window_id, std::wstring& diagnostic) {
    if (!document_) {
        diagnostic = L"Resolved UI document tidak tersedia.";
        return false;
    }
    const auto definition = document_->windows.find(window_id);
    if (definition == document_->windows.end()) {
        diagnostic = L"Window JSON tidak ditemukan.";
        return false;
    }
    window_definition_ = &definition->second;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
    window_class.hIconSm = static_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_SHARED));
    window_class.lpszClassName = platform::MainWindowClassName();
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        diagnostic = L"Window class tidak dapat diregistrasikan.";
        return false;
    }

    const auto& properties = std::get<config::WindowProperties>(window_definition_->properties);
    RECT window_bounds{0, 0, components::ScaleDip(properties.initial_width, 96),
                       components::ScaleDip(properties.initial_height, 96)};
    AdjustWindowRectExForDpi(&window_bounds, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE,
                             WS_EX_APPWINDOW, 96);
    const std::wstring title = ResolveWindowTitle(*window_definition_);
    window_ = CreateWindowExW(WS_EX_APPWINDOW, platform::MainWindowClassName(),
                              title.empty() ? app_identity::kProductName : title.c_str(),
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                              window_bounds.right - window_bounds.left,
                              window_bounds.bottom - window_bounds.top, nullptr, nullptr, instance_, this);
    if (!window_) {
        diagnostic = L"Main window tidak dapat dibuat.";
        return false;
    }
    dpi_ = GetDpiForWindow(window_);
    const int minimum_width = components::ScaleDip(properties.minimum_width, dpi_);
    const int minimum_height = components::ScaleDip(properties.minimum_height, dpi_);
    SetPropW(window_, L"Terminal.MinimumWidth", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(minimum_width)));
    SetPropW(window_, L"Terminal.MinimumHeight", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(minimum_height)));
    return BuildComponentTree(diagnostic);
}

bool WindowContainer::BuildComponentTree(std::wstring& diagnostic) {
    try {
        component_host_ = std::make_unique<components::ComponentHost>();
        component_host_->window = window_;
        component_host_->dpi = dpi_;
        component_host_->render_runtime = &render_runtime_;
        component_host_->theme = &document_->theme(theme_kind_);
        component_host_->invalidate = [this](const RECT& bounds) {
            if (window_) InvalidateRect(window_, &bounds, FALSE);
        };
        component_host_->dispatch_event =
            [this](const config::EventDefinition& event) { DispatchStubEvent(event); };
        root_ = registry_.CreateTree(*window_definition_, *component_host_);
        diagnostic.clear();
        return true;
    } catch (const std::exception&) {
        diagnostic = L"Component tree JSON tidak dapat dibuat oleh registry.";
        return false;
    }
}

bool WindowContainer::PrepareFirstFrame(std::wstring& diagnostic) {
    if (!window_ || !root_) {
        diagnostic = L"Main window belum siap dirender.";
        return false;
    }
    HDC dc = GetDC(window_);
    const bool rendered = dc && RenderCompleteFrame(dc);
    if (dc) ReleaseDC(window_, dc);
    if (!rendered) {
        diagnostic = L"Persistent DIB untuk first frame tidak dapat dibuat.";
        return false;
    }
    diagnostic.clear();
    return true;
}

void WindowContainer::Show(int show_command) {
    if (!window_ || !frame_ready_) return;
    ShowWindow(window_, show_command == 0 ? SW_SHOWNORMAL : show_command);
    UpdateWindow(window_);
}

HWND WindowContainer::hwnd() const noexcept {
    return window_;
}

LRESULT CALLBACK WindowContainer::WindowProcedure(HWND window, UINT message, WPARAM wparam,
                                                   LPARAM lparam) {
    WindowContainer* owner = reinterpret_cast<WindowContainer*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        owner = static_cast<WindowContainer*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));
        owner->window_ = window;
    }
    return owner ? owner->HandleMessage(message, wparam, lparam)
                 : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT WindowContainer::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_COPYDATA: {
            const auto command = platform::ReadForwardedCommand(lparam);
            if (!command) return FALSE;
            if (HasSwitch(*command, L"--exit")) DestroyWindow(window_);
            else platform::ActivateMainWindow(window_);
            return TRUE;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = static_cast<LONG>(reinterpret_cast<INT_PTR>(GetPropW(window_, L"Terminal.MinimumWidth")));
            info->ptMinTrackSize.y = static_cast<LONG>(reinterpret_cast<INT_PTR>(GetPropW(window_, L"Terminal.MinimumHeight")));
            return 0;
        }
        case WM_ERASEBKGND:
            return render_context_.valid() ? 1 : DefWindowProcW(window_, message, wparam, lparam);
        case WM_SIZE:
            frame_ready_ = false;
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wparam);
            if (component_host_) component_host_->dpi = dpi_;
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            frame_ready_ = false;
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window_, &paint);
            const bool rendered = RenderCompleteFrame(dc);
            const bool presented = rendered && render_context_.Present(dc, paint.rcPaint);
            if (!presented) FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
            TrackMouseEvent(&tracking);
            TrackPointer({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            return 0;
        }
        case WM_MOUSELEAVE:
            if (pointer_target_) pointer_target_->PointerMove({-1, -1});
            pointer_target_ = nullptr;
            return 0;
        case WM_LBUTTONDOWN: {
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            components::Component* target = root_ ? root_->HitTest(point) : nullptr;
            if (target && target->PointerDown(point)) pointer_target_ = target;
            return 0;
        }
        case WM_LBUTTONUP: {
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (pointer_target_) pointer_target_->PointerUp(point);
            pointer_target_ = nullptr;
            TrackPointer(point);
            return 0;
        }
        case WM_COMMAND:
            if (root_ && root_->HandleCommand(reinterpret_cast<HWND>(lparam), HIWORD(wparam))) return 0;
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            if (root_) {
                if (HBRUSH brush = root_->HandleControlColor(reinterpret_cast<HDC>(wparam),
                                                              reinterpret_cast<HWND>(lparam))) {
                    return reinterpret_cast<LRESULT>(brush);
                }
            }
            break;
        case WM_DESTROY:
            root_.reset();
            render_context_.Reset();
            RemovePropW(window_, L"Terminal.MinimumWidth");
            RemovePropW(window_, L"Terminal.MinimumHeight");
            window_ = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

bool WindowContainer::RenderCompleteFrame(HDC reference) {
    if (!reference || !root_) return false;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (!render_context_.EnsureSize(reference, width, height)) return false;
    Layout();
    root_->Paint(render_context_.dc());
    render_context_.ForceOpaqueAlpha();
    frame_ready_ = true;
    return true;
}

void WindowContainer::Layout() {
    RECT client{};
    GetClientRect(window_, &client);
    component_host_->layout_dc = render_context_.dc();
    root_->Measure(render_context_.dc(), client.right - client.left, client.bottom - client.top);
    root_->Arrange(client);
    component_host_->layout_dc = nullptr;
}

void WindowContainer::TrackPointer(POINT point) {
    components::Component* target = root_ ? root_->HitTest(point) : nullptr;
    if (pointer_target_ && pointer_target_ != target) pointer_target_->PointerMove({-1, -1});
    if (target) target->PointerMove(point);
    pointer_target_ = target;
}

void WindowContainer::DispatchStubEvent(const config::EventDefinition& event) {
    (void)event;
    // Phase 2 intentionally proves semantic dispatch without invoking business behavior.
}

}  // namespace ui::containers
