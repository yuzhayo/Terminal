#include "ui/containers/window_container.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <exception>

#include "app/app_identity.h"
#include "instrumentation/performance_trace.h"
#include "platform/single_instance.h"

namespace ui::containers {
namespace {

constexpr UINT kAutomationActionMessage = WM_APP + 0x31;

struct AutomationActionRequest {
    components::AutomationAction action = components::AutomationAction::Focus;
    components::Component* component = nullptr;
    double value = 0.0;
    bool result = false;
};

std::wstring ResolveWindowTitle(const config::ResolvedComponent& definition) {
    const auto& properties = std::get<config::WindowProperties>(definition.properties);
    return components::ResolveText(properties.title);
}

std::uint64_t NextCorrelationId() noexcept {
    static std::atomic_uint64_t next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

WindowContainer::WindowContainer(HINSTANCE instance, rendering::RenderRuntime& render_runtime,
                                 std::shared_ptr<const config::ResolvedUiDocument> document,
                                 config::ThemeKind theme_kind)
    : instance_(instance), render_runtime_(render_runtime), document_(std::move(document)),
      theme_kind_(theme_kind), render_context_(&render_runtime_) {}

WindowContainer::~WindowContainer() {
    if (automation_provider_) {
        automation_provider_->Disconnect();
        automation_provider_->Release();
        automation_provider_ = nullptr;
    }
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
    render_context_.SetRedrawRequest([this] {
        if (window_) InvalidateRect(window_, nullptr, FALSE);
    });
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
        component_host_->render_context = &render_context_;
        component_host_->theme = &document_->theme(theme_kind_);
        component_host_->invalidate = [this](const RECT& bounds) {
            render_context_.Invalidate(bounds);
            if (window_) InvalidateRect(window_, &bounds, FALSE);
        };
        component_host_->dispatch_event =
            [this](const config::EventDefinition& event) { DispatchStubEvent(event); };
        component_host_->native_focus_changed = [this](components::Component* component, bool focused) {
            focus_coordinator_.NotifyNativeFocus(component, focused);
        };
        component_host_->request_automation_action =
            [this](components::AutomationAction action, components::Component* component,
                   double value) {
                AutomationActionRequest request{action, component, value, false};
                const DWORD window_thread = GetWindowThreadProcessId(window_, nullptr);
                if (GetCurrentThreadId() == window_thread) {
                    switch (action) {
                        case components::AutomationAction::Focus:
                            return focus_coordinator_.RequestFocus(component);
                        case components::AutomationAction::Invoke:
                            return component && component->AutomationInvoke();
                        case components::AutomationAction::Toggle:
                            return component && component->AutomationToggle();
                        case components::AutomationAction::SetRangeValue:
                            return component && component->AutomationSetRangeValue(value);
                    }
                    return false;
                }
                SendMessageW(window_, kAutomationActionMessage, 0,
                             reinterpret_cast<LPARAM>(&request));
                return request.result;
            };
        component_host_->request_focus_traversal =
            [this](bool reverse) { focus_coordinator_.Move(reverse); };
        root_ = registry_.CreateTree(*window_definition_, *component_host_);
        focus_coordinator_.Rebuild(*root_);
        automation_provider_ = new accessibility::AutomationRootProvider(
            window_, *root_, [this] { return focus_coordinator_.focused(); });
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
    RECT client{};
    GetClientRect(window_, &client);
    const bool sized = dc && render_context_.EnsureSize(dc, client.right - client.left,
                                                        client.bottom - client.top);
    if (sized) Layout();
    const bool prepared = sized && PrepareRenderResources();
    const bool rendered = prepared && RenderCompleteFrame(dc);
    if (dc) ReleaseDC(window_, dc);
    if (!rendered) {
        diagnostic = L"Persistent DIB untuk first frame tidak dapat dibuat.";
        return false;
    }
    diagnostic.clear();
    return true;
}

bool WindowContainer::SuspendNativePeers(std::wstring& diagnostic) {
    if (!root_) {
        diagnostic = L"Component tree tidak tersedia.";
        return false;
    }
    return root_->SuspendNativePeers(diagnostic);
}

void WindowContainer::ResumeNativePeers() {
    if (root_) root_->ResumeNativePeers();
}

void WindowContainer::Show(int show_command) {
    if (!window_ || !frame_ready_) return;
    ShowWindow(window_, show_command == 0 ? SW_SHOWNORMAL : show_command);
    UpdateWindow(window_);
}

void WindowContainer::ApplyTheme(config::ThemeKind theme_kind) {
    if (!component_host_) return;
    theme_kind_ = theme_kind;
    component_host_->theme = &document_->theme(theme_kind_);
    render_runtime_.AdvanceResourceEpoch();
    if (root_) root_->OnDpiChanged();
    resources_prepared_ = false;
    PrepareRenderResources();
    frame_ready_ = false;
    InvalidateRect(window_, nullptr, FALSE);
}

void WindowContainer::HandleIpcRequest(const platform::IpcRequest& request) {
    if (!window_) return;
    if (request.command == platform::IpcCommand::RequestExit) {
        DestroyWindow(window_);
        return;
    }
    if (request.command == platform::IpcCommand::OpenRoute) {
        const std::uint64_t correlation = NextCorrelationId();
        instrumentation::TraceNavigationRequested(correlation);
        pending_navigation_correlation_ = correlation;
        last_scenario_correlation_ = correlation;
        SetTimer(window_, 1, 10000, nullptr);
    }
    platform::ActivateMainWindow(window_);
    InvalidateRect(window_, nullptr, FALSE);
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
    if (message == kAutomationActionMessage) {
        auto* request = reinterpret_cast<AutomationActionRequest*>(lparam);
        if (!request || !request->component) return 0;
        switch (request->action) {
            case components::AutomationAction::Focus:
                request->result = focus_coordinator_.RequestFocus(request->component);
                break;
            case components::AutomationAction::Invoke:
                request->result = request->component->AutomationInvoke();
                break;
            case components::AutomationAction::Toggle:
                request->result = request->component->AutomationToggle();
                break;
            case components::AutomationAction::SetRangeValue:
                request->result = request->component->AutomationSetRangeValue(request->value);
                break;
        }
        return request->result ? 1 : 0;
    }
    switch (message) {
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = static_cast<LONG>(reinterpret_cast<INT_PTR>(GetPropW(window_, L"Terminal.MinimumWidth")));
            info->ptMinTrackSize.y = static_cast<LONG>(reinterpret_cast<INT_PTR>(GetPropW(window_, L"Terminal.MinimumHeight")));
            return 0;
        }
        case WM_ERASEBKGND:
            return render_context_.valid() ? 1 : DefWindowProcW(window_, message, wparam, lparam);
        case WM_GETOBJECT:
            if (automation_provider_ && static_cast<LONG>(lparam) == UiaRootObjectId) {
                return UiaReturnRawElementProvider(
                    window_, wparam, lparam,
                    static_cast<IRawElementProviderSimple*>(automation_provider_));
            }
            break;
        case WM_SIZE:
            pending_resize_correlation_ = NextCorrelationId();
            last_scenario_correlation_ = *pending_resize_correlation_;
            SetTimer(window_, 1, 10000, nullptr);
            frame_ready_ = false;
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wparam);
            if (component_host_) component_host_->dpi = dpi_;
            if (root_) root_->OnDpiChanged();
            render_runtime_.AdvanceResourceEpoch();
            resources_prepared_ = false;
            PrepareRenderResources();
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
            render_runtime_.AdvanceResourceEpoch();
            if (root_) root_->OnDpiChanged();
            resources_prepared_ = false;
            PrepareRenderResources();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window_, &paint);
            const bool rendered = RenderFrame(dc, paint.rcPaint, false);
            RECT client{};
            GetClientRect(window_, &client);
            const bool presented = rendered ? render_context_.Present(dc, paint.rcPaint)
                                             : render_context_.PresentScaled(dc, client);
            if (!presented) FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
            EndPaint(window_, &paint);
            if (presented && rendered) {
                if (pending_input_correlation_) {
                    instrumentation::TraceInputVisualPresented(*pending_input_correlation_);
                    pending_input_correlation_.reset();
                }
                if (pending_resize_correlation_) {
                    instrumentation::TraceResizeFramePresented(*pending_resize_correlation_);
                    pending_resize_correlation_.reset();
                }
                if (pending_navigation_correlation_) {
                    instrumentation::TraceNavigationPresented(*pending_navigation_correlation_);
                    pending_navigation_correlation_.reset();
                }
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            TraceInputStart();
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
            TrackMouseEvent(&tracking);
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (GetCapture() == window_ && pointer_target_) pointer_target_->PointerMove(point);
            else TrackPointer(point);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (pointer_target_) pointer_target_->PointerMove({-1, -1});
            pointer_target_ = nullptr;
            return 0;
        case WM_ACTIVATE:
            focus_coordinator_.SetWindowActive(LOWORD(wparam) != WA_INACTIVE);
            return 0;
        case WM_KEYDOWN:
            TraceInputStart();
            if (wparam == VK_TAB) {
                if (focus_coordinator_.Move((GetKeyState(VK_SHIFT) & 0x8000) != 0)) return 0;
            } else if (focus_coordinator_.HandleKeyDown(static_cast<UINT>(wparam))) {
                return 0;
            }
            break;
        case WM_LBUTTONDOWN: {
            TraceInputStart();
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            components::Component* target = root_ ? root_->HitTest(point) : nullptr;
            if (target && target->CanFocus()) focus_coordinator_.RequestFocus(target);
            if (target && target->PointerDown(point)) pointer_target_ = target;
            return 0;
        }
        case WM_LBUTTONUP: {
            TraceInputStart();
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (pointer_target_) pointer_target_->PointerUp(point);
            pointer_target_ = nullptr;
            TrackPointer(point);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            TraceInputStart();
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window_, &point);
            components::Component* target = root_ ? root_->HitTest(point) : nullptr;
            while (target && !target->PointerWheel(GET_WHEEL_DELTA_WPARAM(wparam))) {
                target = target->parent();
            }
            return target ? 0 : DefWindowProcW(window_, message, wparam, lparam);
        }
        case WM_COMMAND:
            TraceInputStart();
            if (root_ && root_->HandleCommand(reinterpret_cast<HWND>(lparam), HIWORD(wparam))) return 0;
            break;
        case WM_TIMER:
            if (wparam == 1) {
                KillTimer(window_, 1);
                if (last_scenario_correlation_ != 0) {
                    instrumentation::TraceScenarioSettled(last_scenario_correlation_);
                    instrumentation::TraceResourceSnapshot(render_runtime_.diagnostics());
                }
                return 0;
            }
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
            focus_coordinator_.Clear();
            if (automation_provider_) {
                automation_provider_->Disconnect();
                automation_provider_->Release();
                automation_provider_ = nullptr;
            }
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
    RECT client{};
    GetClientRect(window_, &client);
    return RenderFrame(reference, client, true);
}

bool WindowContainer::RenderFrame(HDC reference, const RECT& requested_region, bool force_full) {
    if (!reference || !root_) return false;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const std::uint64_t previous_generation = render_context_.allocation_generation();
    if (!render_context_.EnsureSize(reference, width, height)) return false;
    const bool resized = render_context_.allocation_generation() != previous_generation;
    if (force_full || resized) {
        Layout();
        render_context_.InvalidateAll();
    } else {
        render_context_.Invalidate(requested_region);
    }

    RECT invalid_region{};
    if (!render_context_.TakeInvalidation(invalid_region)) return true;
    const int saved = SaveDC(render_context_.dc());
    IntersectClipRect(render_context_.dc(), invalid_region.left, invalid_region.top,
                      invalid_region.right, invalid_region.bottom);
    render_runtime_.BeginPaintScope();
    root_->Paint(render_context_.dc());
    overlay_plane_.Paint(render_context_, invalid_region);
    render_runtime_.EndPaintScope();
    if (saved != 0) RestoreDC(render_context_.dc(), saved);
    render_context_.ForceOpaqueAlpha(invalid_region);
    frame_ready_ = true;
    return true;
}

bool WindowContainer::PrepareRenderResources() {
    if (resources_prepared_) return true;
    if (!root_) return false;
    resources_prepared_ = root_->PrepareResources(GetSysColor(COLOR_WINDOW));
    return resources_prepared_;
}

void WindowContainer::TraceInputStart() {
    if (pending_input_correlation_) return;
    pending_input_correlation_ = NextCorrelationId();
    last_scenario_correlation_ = *pending_input_correlation_;
    instrumentation::TraceInputReceived(*pending_input_correlation_);
    SetTimer(window_, 1, 10000, nullptr);
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
    const auto patch = application_bridge_.Dispatch({event.action, event.payload});
    if (!patch) return;
    if (patch->window_title) SetWindowTextW(window_, patch->window_title->c_str());
    if (patch->request_repaint) {
        frame_ready_ = false;
        InvalidateRect(window_, nullptr, FALSE);
    }
}

}  // namespace ui::containers
