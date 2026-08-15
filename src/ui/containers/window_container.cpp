#include "ui/containers/window_container.h"

#include <dwmapi.h>
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

std::uint64_t NextRuntimeInstanceId() noexcept {
    static std::atomic_uint64_t next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

std::size_t CountDirtyParticipants(components::Component& root) {
    std::vector<components::EditableParticipant*> participants;
    root.CollectEditableParticipants(participants);
    return static_cast<std::size_t>(std::count_if(
        participants.begin(), participants.end(),
        [](const components::EditableParticipant* participant) {
            return participant && participant->IsDirty();
        }));
}

std::size_t CountDirtySnapshot(
    const components::ComponentRuntimeStateMap& states) noexcept {
    return static_cast<std::size_t>(std::count_if(
        states.begin(), states.end(), [](const auto& item) {
            const components::ComponentRuntimeState& state = item.second;
            return state.type == config::ComponentType::Input && state.draft_baseline &&
                   state.draft_value && *state.draft_baseline != *state.draft_value;
        }));
}

const config::ResolvedComponent* FindComponentDefinition(
    const config::ResolvedComponent& component, std::string_view component_id) noexcept {
    if (component.id == component_id) return &component;
    for (const config::ResolvedComponent& child : component.children) {
        if (const config::ResolvedComponent* found =
                FindComponentDefinition(child, component_id)) {
            return found;
        }
    }
    return nullptr;
}

bool PatchTargetsEvent(const application::UiPatch& patch,
                       const application::UiEvent& event) noexcept {
    return patch.target == event.source &&
           patch.config_generation == event.config_generation &&
           patch.generation != 0;
}

}  // namespace

WindowContainer::WindowContainer(HINSTANCE instance, rendering::RenderRuntime& render_runtime,
                                 std::shared_ptr<const config::ResolvedUiDocument> document,
                                 config::ThemeKind theme_kind,
                                 std::shared_ptr<application::UiApplicationBridge> application_bridge)
    : instance_(instance), render_runtime_(render_runtime), document_(std::move(document)),
      theme_kind_(theme_kind), render_context_(&render_runtime_),
      application_bridge_(std::move(application_bridge)) {}

WindowContainer::~WindowContainer() {
    ResetAutomationProvider();
    root_ = nullptr;
    screen_cache_.clear();
    pending_screen_snapshots_.clear();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
}

bool WindowContainer::Create(const std::string& window_id, std::wstring& diagnostic) {
    if (!document_ || !application_bridge_) {
        diagnostic = L"Resolved UI document tidak tersedia.";
        return false;
    }
    const auto definition = document_->windows.find(window_id);
    if (definition == document_->windows.end()) {
        diagnostic = L"Window JSON tidak ditemukan.";
        return false;
    }
    window_definition_ = &definition->second;
    window_id_ = window_id;
    window_instance_id_ = NextRuntimeInstanceId();

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
    ApplyNonClientTheme();
    dpi_ = GetDpiForWindow(window_);
    render_context_.SetRedrawRequest([this] {
        if (window_) InvalidateRect(window_, nullptr, FALSE);
    });
    UpdateMinimumTrackSize();
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
            [this](components::Component& source, std::string_view event_type,
                   const config::EventDefinition& event) {
                DispatchUiEvent(source, event_type, event);
            };
        component_host_->native_focus_changed = [this](components::Component* component, bool focused) {
            focus_coordinator_.NotifyNativeFocus(component, focused);
            if (focused && automation_provider_) automation_provider_->NotifyFocusChanged();
        };
        component_host_->popup_state_changed = [this](components::Component* component, bool open) {
            if (open) {
                if (active_popup_owner_ && active_popup_owner_ != component) {
                    active_popup_owner_->DismissOwnedPopup();
                }
                active_popup_owner_ = component;
            } else if (active_popup_owner_ == component) {
                active_popup_owner_ = nullptr;
            }
            if (automation_provider_) {
                automation_provider_->NotifyPopupStateChanged(component, open);
            }
        };
        component_host_->resolve_string_items = [this](std::string_view binding) {
            return application_bridge_->ResolveStringItems(binding);
        };
        component_host_->resolve_string_value = [this](std::string_view binding) {
            return application_bridge_->ResolveStringValue(binding);
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
                        case components::AutomationAction::Expand:
                            return component && component->AutomationExpand();
                        case components::AutomationAction::Collapse:
                            return component && component->AutomationCollapse();
                        case components::AutomationAction::SetRangeValue:
                            return component && component->AutomationSetRangeValue(value);
                        case components::AutomationAction::Close:
                            return component && component->AutomationClose();
                        case components::AutomationAction::SelectItem:
                            return component && component->AutomationSelectItem(
                                                    static_cast<std::size_t>(value));
                        case components::AutomationAction::RealizeItem:
                            return component && component->AutomationRealizeItem(
                                                    static_cast<std::size_t>(value));
                        case components::AutomationAction::ScrollVertical:
                            return component && component->AutomationScrollVertical(
                                                    static_cast<components::AutomationScrollAmount>(
                                                        static_cast<int>(value)));
                        case components::AutomationAction::SetVerticalScrollPercent:
                            return component && component->AutomationSetVerticalScrollPercent(value);
                        case components::AutomationAction::SelectPopupItem:
                            return component && component->AutomationSelectPopupItem(
                                                    static_cast<std::size_t>(value));
                        case components::AutomationAction::RealizePopupItem:
                            return component && component->AutomationRealizePopupItem(
                                                    static_cast<std::size_t>(value));
                    }
                    return false;
                }
                SendMessageW(window_, kAutomationActionMessage, 0,
                             reinterpret_cast<LPARAM>(&request));
                return request.result;
            };
        component_host_->request_focus_traversal =
            [this](bool reverse) { focus_coordinator_.Move(reverse); };
        component_host_->request_modal_close =
            [this](components::Component* dialog, components::ModalResult result) {
                if (!dialog || modal_stack_.top() != dialog) return false;
                std::wstring diagnostic;
                return CloseModal(result, diagnostic);
            };
        component_host_->return_popup_automation_provider =
            [this](components::Component* component, HWND popup, WPARAM wparam, LPARAM lparam) {
                return automation_provider_
                           ? automation_provider_->ReturnPopupProvider(component, popup, wparam,
                                                                       lparam)
                           : 0;
            };
        const auto& properties =
            std::get<config::WindowProperties>(window_definition_->properties);
        return ActivateRoute(properties.initial_route, diagnostic);
    } catch (const std::exception&) {
        diagnostic = L"Component tree JSON tidak dapat dibuat oleh registry.";
        return false;
    }
}

void WindowContainer::ResetAutomationProvider() {
    if (!automation_provider_) return;
    automation_provider_->Disconnect();
    automation_provider_->Release();
    automation_provider_ = nullptr;
}

bool WindowContainer::ActivateRoute(std::string_view route_id, std::wstring& diagnostic) {
    if (!component_host_ || !document_) {
        diagnostic = L"Component host belum tersedia.";
        return false;
    }
    const auto definition = document_->screens.find(route_id);
    if (definition == document_->screens.end()) {
        diagnostic = L"Route JSON tidak ditemukan.";
        return false;
    }
    if (root_ && active_route_ == route_id) {
        diagnostic.clear();
        return true;
    }

    if (active_popup_owner_) active_popup_owner_->DismissOwnedPopup();
    active_popup_owner_ = nullptr;
    pointer_target_ = nullptr;
    if (GetCapture() == window_) ReleaseCapture();
    if (root_ && modal_stack_.active() &&
        !modal_stack_.Drain(*root_, focus_coordinator_, diagnostic)) {
        return false;
    }

    components::Component* previous_root = root_;
    ScreenEntry* previous_entry = nullptr;
    if (!active_route_.empty()) {
        const auto previous = screen_cache_.find(active_route_);
        if (previous != screen_cache_.end()) previous_entry = &previous->second;
    }
    if (previous_entry && focus_coordinator_.focused()) {
        previous_entry->focused_component_id =
            focus_coordinator_.focused()->definition().id;
    }
    focus_coordinator_.Clear();
    ResetAutomationProvider();
    if (previous_root && !previous_root->SuspendNativePeers(diagnostic)) {
        focus_coordinator_.Rebuild(*previous_root);
        automation_provider_ = new accessibility::AutomationRootProvider(
            window_, *previous_root, [this] { return focus_coordinator_.focused(); });
        return false;
    }
    if (previous_entry) previous_entry->suspended = true;

    ScreenEntry* activated_entry = nullptr;
    try {
        auto [entry, inserted] = screen_cache_.try_emplace(std::string(route_id));
        if (inserted) {
            entry->second.instance_id = NextRuntimeInstanceId();
            entry->second.root = registry_.CreateTree(definition->second, *component_host_);
            AttachCloseConfirmationIfMissing(*entry->second.root);
        }
        activated_entry = &entry->second;
        root_ = entry->second.root.get();
        active_route_ = entry->first;
        active_screen_instance_id_ = entry->second.instance_id;
        root_->OnDpiChanged();
        const auto snapshot = pending_screen_snapshots_.find(entry->first);
        if (snapshot != pending_screen_snapshots_.end()) {
            root_->RestoreRuntimeState(snapshot->second.component_states);
            entry->second.focused_component_id = snapshot->second.focused_component_id;
            pending_screen_snapshots_.erase(snapshot);
        }
        if (entry->second.suspended) {
            root_->ResumeNativePeers();
            entry->second.suspended = false;
        }
    } catch (const std::exception&) {
        const auto incomplete = screen_cache_.find(route_id);
        if (incomplete != screen_cache_.end() && !incomplete->second.root) {
            screen_cache_.erase(incomplete);
        }
        if (previous_root) {
            previous_root->ResumeNativePeers();
            if (previous_entry) previous_entry->suspended = false;
            root_ = previous_root;
            focus_coordinator_.Rebuild(*root_);
            automation_provider_ = new accessibility::AutomationRootProvider(
                window_, *root_, [this] { return focus_coordinator_.focused(); });
            if (previous_entry && !previous_entry->focused_component_id.empty()) {
                focus_coordinator_.RequestFocus(
                    root_->FindById(previous_entry->focused_component_id));
            }
        }
        diagnostic = L"Screen JSON tidak dapat dibuat oleh component registry.";
        return false;
    }

    focus_coordinator_.Rebuild(*root_);
    automation_provider_ = new accessibility::AutomationRootProvider(
        window_, *root_, [this] { return focus_coordinator_.focused(); });
    if (activated_entry && !activated_entry->focused_component_id.empty()) {
        if (!focus_coordinator_.RequestFocus(
                root_->FindById(activated_entry->focused_component_id))) {
            activated_entry->focused_component_id.clear();
        }
    }
    resources_prepared_ = false;
    frame_ready_ = false;
    if (render_context_.valid()) {
        Layout();
        PrepareRenderResources();
        render_context_.InvalidateAll();
    }
    if (window_) InvalidateRect(window_, nullptr, FALSE);
    diagnostic.clear();
    return true;
}

bool WindowContainer::NormalizeForReload(std::wstring& diagnostic) {
    if (!root_) {
        diagnostic.clear();
        return true;
    }
    if (active_popup_owner_) active_popup_owner_->DismissOwnedPopup();
    active_popup_owner_ = nullptr;
    pointer_target_ = nullptr;
    if (GetCapture() == window_) ReleaseCapture();
    if (modal_stack_.active() &&
        !modal_stack_.Drain(*root_, focus_coordinator_, diagnostic)) {
        return false;
    }
    const auto active = screen_cache_.find(active_route_);
    if (active == screen_cache_.end()) {
        diagnostic = L"Active route tidak tercatat pada screen cache.";
        return false;
    }
    if (focus_coordinator_.focused()) {
        active->second.focused_component_id =
            focus_coordinator_.focused()->definition().id;
    }
    if (!active->second.suspended) {
        if (!root_->SuspendNativePeers(diagnostic)) return false;
        active->second.suspended = true;
    }
    diagnostic.clear();
    return true;
}

void WindowContainer::CaptureScreenSnapshots() {
    auto snapshots = std::move(pending_screen_snapshots_);
    for (const auto& [route_id, entry] : screen_cache_) {
        if (!entry.root) continue;
        ScreenRuntimeSnapshot snapshot;
        snapshot.focused_component_id = entry.focused_component_id;
        entry.root->CaptureRuntimeState(snapshot.component_states);
        snapshots.insert_or_assign(route_id, std::move(snapshot));
    }
    pending_screen_snapshots_ = std::move(snapshots);
}

bool WindowContainer::InstallDocument(
    std::shared_ptr<const config::ResolvedUiDocument> document,
    std::string_view preferred_route, std::wstring& diagnostic) {
    if (!document) {
        diagnostic = L"Resolved UI document reload tidak tersedia.";
        return false;
    }
    const auto window_definition = document->windows.find(window_id_);
    if (window_definition == document->windows.end()) {
        diagnostic = L"Window aktif tidak tersedia pada config generation baru.";
        return false;
    }
    if (window_definition->second.type != config::ComponentType::Window) {
        diagnostic = L"Config generation baru tidak memiliki Window definition yang valid.";
        return false;
    }
    const auto& properties =
        std::get<config::WindowProperties>(window_definition->second.properties);
    const std::string target_route = document->screens.contains(preferred_route)
                                         ? std::string(preferred_route)
                                         : properties.initial_route;

    focus_coordinator_.Clear();
    ResetAutomationProvider();
    root_ = nullptr;
    screen_cache_.clear();
    document_ = std::move(document);
    window_definition_ = &document_->windows.at(window_id_);
    component_host_->theme = &document_->theme(theme_kind_);
    for (auto snapshot = pending_screen_snapshots_.begin();
         snapshot != pending_screen_snapshots_.end();) {
        if (!document_->screens.contains(snapshot->first)) {
            snapshot = pending_screen_snapshots_.erase(snapshot);
        } else {
            ++snapshot;
        }
    }
    active_route_.clear();
    active_screen_instance_id_ = 0;
    render_runtime_.AdvanceResourceEpoch();
    resources_prepared_ = false;
    frame_ready_ = false;

    if (window_) {
        const std::wstring title = ResolveWindowTitle(*window_definition_);
        SetWindowTextW(window_, title.empty() ? app_identity::kProductName : title.c_str());
        UpdateMinimumTrackSize();
    }
    return ActivateRoute(target_route, diagnostic);
}

bool WindowContainer::ReloadDocument(
    std::shared_ptr<const config::ResolvedUiDocument> document,
    std::wstring& diagnostic) {
    if (!document || !component_host_) {
        diagnostic = L"Resolved UI document reload tidak tersedia.";
        return false;
    }
    if (document == document_) {
        diagnostic.clear();
        return true;
    }
    if (document_ && document->generation <= document_->generation) {
        diagnostic = L"Config generation reload harus meningkat secara monotonik.";
        return false;
    }
    if (!document->windows.contains(window_id_)) {
        diagnostic = L"Window aktif tidak tersedia pada config generation baru.";
        return false;
    }
    if (!NormalizeForReload(diagnostic)) return false;
    CaptureScreenSnapshots();

    const auto previous_document = document_;
    const std::string previous_route = active_route_;
    const auto previous_snapshots = pending_screen_snapshots_;
    if (InstallDocument(std::move(document), previous_route, diagnostic)) return true;

    const std::wstring reload_diagnostic = diagnostic;
    pending_screen_snapshots_ = previous_snapshots;
    std::wstring rollback_diagnostic;
    if (!InstallDocument(previous_document, previous_route, rollback_diagnostic)) {
        diagnostic = reload_diagnostic + L" Rollback generation lama juga gagal: " +
                     rollback_diagnostic;
        return false;
    }
    diagnostic = reload_diagnostic;
    return false;
}

bool WindowContainer::PrepareFirstFrame(std::wstring& diagnostic) {
    return PrepareFrameForShow(false, diagnostic);
}

bool WindowContainer::PrepareFrameForShow(bool resume_native_peers,
                                          std::wstring& diagnostic) {
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
    if (sized && resume_native_peers) ResumeNativePeers();
    const bool prepared = sized && PrepareRenderResources();
    const bool rendered = prepared && RenderCompleteFrame(dc);
    if (dc) ReleaseDC(window_, dc);
    if (!rendered) {
        if (resume_native_peers) {
            std::wstring ignored;
            SuspendNativePeers(ignored);
        }
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
    if (modal_stack_.active() &&
        !modal_stack_.Drain(*root_, focus_coordinator_, diagnostic)) return false;
    return root_->SuspendNativePeers(diagnostic);
}

void WindowContainer::ResumeNativePeers() {
    if (root_) root_->ResumeNativePeers();
}

bool WindowContainer::RestoreAndShow(int show_command, std::wstring& diagnostic) {
    if (!window_ || !root_) {
        diagnostic = L"Route window tidak tersedia untuk dipulihkan.";
        return false;
    }
    const UINT active_dpi = GetDpiForWindow(window_);
    if (active_dpi != 0 && active_dpi != dpi_) {
        dpi_ = active_dpi;
        if (component_host_) component_host_->dpi = dpi_;
        UpdateMinimumTrackSize();
        root_->OnDpiChanged();
        render_runtime_.AdvanceResourceEpoch();
        resources_prepared_ = false;
        frame_ready_ = false;
    }
    if (!PrepareFrameForShow(true, diagnostic)) return false;
    retained_resources_released_ = false;
    Show(show_command);
    diagnostic.clear();
    return true;
}

void WindowContainer::ReleaseRetainedResources() noexcept {
    if (root_) root_->ReleaseResources();
    render_context_.Reset();
    resources_prepared_ = false;
    frame_ready_ = false;
    retained_resources_released_ = true;
}

void WindowContainer::Show(int show_command) {
    if (!window_ || !frame_ready_) return;
    ShowWindow(window_, show_command == 0 ? SW_SHOWNORMAL : show_command);
    UpdateWindow(window_);
}

void WindowContainer::ApplyTheme(config::ThemeKind theme_kind) {
    ApplyThemeState(theme_kind, true);
}

void WindowContainer::ApplySharedTheme(config::ThemeKind theme_kind) {
    ApplyThemeState(theme_kind, false);
}

void WindowContainer::ApplyThemeState(config::ThemeKind theme_kind,
                                      bool advance_shared_epoch) {
    if (!component_host_) return;
    theme_kind_ = theme_kind;
    component_host_->theme = &document_->theme(theme_kind_);
    if (advance_shared_epoch) render_runtime_.AdvanceResourceEpoch();
    ApplyNonClientTheme();
    if (root_) root_->OnDpiChanged();
    resources_prepared_ = false;
    PrepareRenderResources();
    frame_ready_ = false;
    InvalidateRect(window_, nullptr, FALSE);
}

void WindowContainer::ApplyNonClientTheme() noexcept {
    if (!window_) return;
    const BOOL enabled = theme_kind_ == config::ThemeKind::Dark ? TRUE : FALSE;
    DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled,
                          sizeof(enabled));
}

void WindowContainer::UpdateMinimumTrackSize() noexcept {
    if (!window_ || !window_definition_ ||
        window_definition_->type != config::ComponentType::Window) return;
    const auto& properties =
        std::get<config::WindowProperties>(window_definition_->properties);
    const int minimum_width = components::ScaleDip(properties.minimum_width, dpi_);
    const int minimum_height = components::ScaleDip(properties.minimum_height, dpi_);
    SetPropW(window_, L"Terminal.MinimumWidth",
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(minimum_width)));
    SetPropW(window_, L"Terminal.MinimumHeight",
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(minimum_height)));
}

bool WindowContainer::Navigate(std::string_view route_id, std::wstring& diagnostic) {
    if (route_request_handler_) return route_request_handler_(*this, route_id, diagnostic);
    return ActivateRoute(route_id, diagnostic);
}

void WindowContainer::SetDestroyedHandler(DestroyedHandler handler) {
    destroyed_handler_ = std::move(handler);
}

void WindowContainer::SetRouteRequestHandler(RouteRequestHandler handler) {
    route_request_handler_ = std::move(handler);
}

void WindowContainer::SetCloseRequestedHandler(CloseRequestedHandler handler) {
    close_requested_handler_ = std::move(handler);
}

HWND WindowContainer::hwnd() const noexcept {
    return window_;
}

std::string_view WindowContainer::active_route() const noexcept {
    return active_route_;
}

std::size_t WindowContainer::cached_screen_count() const noexcept {
    return screen_cache_.size();
}

bool WindowContainer::IsDirty() const {
    return dirty_participant_count() != 0;
}

std::size_t WindowContainer::dirty_participant_count() const {
    std::size_t count = 0;
    for (const auto& [route_id, entry] : screen_cache_) {
        (void)route_id;
        if (entry.root) count += CountDirtyParticipants(*entry.root);
    }
    for (const auto& [route_id, snapshot] : pending_screen_snapshots_) {
        (void)route_id;
        count += CountDirtySnapshot(snapshot.component_states);
    }
    return count;
}

ClosePreparation WindowContainer::PrepareClose(ClosePreparedHandler handler,
                                               std::wstring& diagnostic) {
    if (close_prepared_) {
        diagnostic.clear();
        return ClosePreparation::Ready;
    }
    if (close_decision_pending_) {
        diagnostic = L"Close confirmation sudah aktif.";
        return ClosePreparation::AwaitingDecision;
    }
    if (!IsDirty()) {
        close_prepared_ = true;
        diagnostic.clear();
        return ClosePreparation::Ready;
    }

    close_prepared_handler_ = std::move(handler);
    close_decision_pending_ = true;
    pending_close_save_result_.reset();
    if (!OpenCloseConfirmation(diagnostic)) {
        close_decision_pending_ = false;
        close_prepared_handler_ = {};
        return ClosePreparation::Failed;
    }
    diagnostic.clear();
    return ClosePreparation::AwaitingDecision;
}

bool WindowContainer::CommitClose(std::wstring& diagnostic) {
    if (!close_prepared_ || close_decision_pending_ || !window_) {
        diagnostic = L"Window belum melewati PrepareClose.";
        return false;
    }
    if (!DestroyWindow(window_)) {
        diagnostic = L"Route window tidak dapat dihancurkan setelah CommitClose.";
        return false;
    }
    CommitDiscardTransaction();
    close_prepared_ = false;
    diagnostic.clear();
    return true;
}

void WindowContainer::RollbackPreparedClose() noexcept {
    RollbackDiscardTransaction();
    close_prepared_ = false;
    pending_close_save_result_.reset();
}

std::vector<components::EditableParticipant*>
WindowContainer::CollectEditableParticipants() {
    std::vector<components::EditableParticipant*> participants;
    for (auto& [route_id, entry] : screen_cache_) {
        (void)route_id;
        if (entry.root) entry.root->CollectEditableParticipants(participants);
    }
    return participants;
}

bool WindowContainer::StageDiscardTransaction(std::wstring& diagnostic) {
    staged_discard_participants_.clear();
    staged_snapshot_backup_.reset();
    for (components::EditableParticipant* participant : CollectEditableParticipants()) {
        if (!participant || !participant->IsDirty()) continue;
        if (!participant->StageDiscard()) {
            RollbackDiscardTransaction();
            diagnostic = L"Dirty participant menolak staged Discard.";
            return false;
        }
        staged_discard_participants_.push_back(participant);
    }

    bool snapshot_staged = false;
    for (auto& [route_id, snapshot] : pending_screen_snapshots_) {
        (void)route_id;
        for (auto& [component_id, state] : snapshot.component_states) {
            (void)component_id;
            if (!state.draft_baseline || !state.draft_value ||
                *state.draft_baseline == *state.draft_value) {
                continue;
            }
            if (!snapshot_staged) {
                staged_snapshot_backup_ = pending_screen_snapshots_;
                snapshot_staged = true;
            }
            state.draft_value = state.draft_baseline;
        }
    }
    diagnostic.clear();
    return true;
}

void WindowContainer::ApplySaveSuccess() noexcept {
    for (components::EditableParticipant* participant : CollectEditableParticipants()) {
        if (participant && participant->IsDirty()) participant->ApplySaveResult(true);
    }
    for (auto& [route_id, snapshot] : pending_screen_snapshots_) {
        (void)route_id;
        for (auto& [component_id, state] : snapshot.component_states) {
            (void)component_id;
            if (state.draft_baseline && state.draft_value) {
                state.draft_baseline = state.draft_value;
            }
        }
    }
}

void WindowContainer::CommitDiscardTransaction() noexcept {
    // A successful DestroyWindow has already destroyed the component tree. The
    // staged participant pointers are intentionally not dereferenced here; only
    // their rollback bookkeeping needs to be forgotten at commit.
    staged_discard_participants_.clear();
    staged_snapshot_backup_.reset();
}

void WindowContainer::RollbackDiscardTransaction() noexcept {
    for (auto participant = staged_discard_participants_.rbegin();
         participant != staged_discard_participants_.rend(); ++participant) {
        if (*participant) (*participant)->RollbackDiscard();
    }
    staged_discard_participants_.clear();
    if (staged_snapshot_backup_) {
        pending_screen_snapshots_ = std::move(*staged_snapshot_backup_);
        staged_snapshot_backup_.reset();
    }
}

std::uint64_t WindowContainer::document_generation() const noexcept {
    return document_ ? document_->generation : 0;
}

UINT WindowContainer::dpi() const noexcept { return dpi_; }

bool WindowContainer::close_decision_pending() const noexcept {
    return close_decision_pending_;
}

bool WindowContainer::retained_resources_released() const noexcept {
    return retained_resources_released_;
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
            case components::AutomationAction::Expand:
                request->result = request->component->AutomationExpand();
                break;
            case components::AutomationAction::Collapse:
                request->result = request->component->AutomationCollapse();
                break;
            case components::AutomationAction::SetRangeValue:
                request->result = request->component->AutomationSetRangeValue(request->value);
                break;
            case components::AutomationAction::Close:
                request->result = request->component->AutomationClose();
                break;
            case components::AutomationAction::SelectItem:
                request->result = request->component->AutomationSelectItem(
                    static_cast<std::size_t>(request->value));
                break;
            case components::AutomationAction::RealizeItem:
                request->result = request->component->AutomationRealizeItem(
                    static_cast<std::size_t>(request->value));
                break;
            case components::AutomationAction::ScrollVertical:
                request->result = request->component->AutomationScrollVertical(
                    static_cast<components::AutomationScrollAmount>(
                        static_cast<int>(request->value)));
                break;
            case components::AutomationAction::SetVerticalScrollPercent:
                request->result = request->component->AutomationSetVerticalScrollPercent(
                    request->value);
                break;
            case components::AutomationAction::SelectPopupItem:
                request->result = request->component->AutomationSelectPopupItem(
                    static_cast<std::size_t>(request->value));
                break;
            case components::AutomationAction::RealizePopupItem:
                request->result = request->component->AutomationRealizePopupItem(
                    static_cast<std::size_t>(request->value));
                break;
        }
        if (request->result && request->action == components::AutomationAction::Focus &&
            automation_provider_) automation_provider_->NotifyFocusChanged();
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
            const UINT next_dpi = HIWORD(wparam) == 0 ? LOWORD(wparam) : HIWORD(wparam);
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            if (suggested) {
                SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOACTIVATE | SWP_NOZORDER);
            }
            dpi_ = next_dpi == 0 ? 96 : next_dpi;
            if (component_host_) component_host_->dpi = dpi_;
            UpdateMinimumTrackSize();
            if (root_) root_->OnDpiChanged();
            render_runtime_.AdvanceResourceEpoch();
            resources_prepared_ = false;
            PrepareRenderResources();
            if (root_ && render_context_.valid()) {
                Layout();
                render_context_.InvalidateAll();
            }
            frame_ready_ = false;
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
            // Process-global resource invalidation is owned by ApplicationContainer.
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
            if (LOWORD(wparam) == WA_INACTIVE && active_popup_owner_) {
                active_popup_owner_->DismissOwnedPopup();
            }
            focus_coordinator_.SetWindowActive(LOWORD(wparam) != WA_INACTIVE);
            return 0;
        case WM_KEYDOWN:
            TraceInputStart();
            if (active_popup_owner_ &&
                active_popup_owner_->HandleKeyDown(static_cast<UINT>(wparam))) {
                return 0;
            }
            if (modal_stack_.active() && modal_stack_.HandleKeyDown(static_cast<UINT>(wparam))) {
                return 0;
            }
            if (wparam == VK_TAB) {
                if (focus_coordinator_.Move((GetKeyState(VK_SHIFT) & 0x8000) != 0)) return 0;
            } else if (focus_coordinator_.HandleKeyDown(static_cast<UINT>(wparam))) {
                return 0;
            }
            break;
        case WM_LBUTTONDOWN: {
            TraceInputStart();
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (active_popup_owner_) {
                POINT screen_point = point;
                ClientToScreen(window_, &screen_point);
                if (!active_popup_owner_->OwnsPopupScopePoint(screen_point)) {
                    active_popup_owner_->DismissOwnedPopup();
                }
            }
            components::Component* target = HitTestInteractive(point);
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
            components::Component* target = HitTestInteractive(point);
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
        case WM_CLOSE:
            if (close_requested_handler_) {
                close_requested_handler_(*this);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (active_popup_owner_) active_popup_owner_->DismissOwnedPopup();
            active_popup_owner_ = nullptr;
            if (root_ && modal_stack_.active()) {
                std::wstring ignored;
                modal_stack_.Drain(*root_, focus_coordinator_, ignored);
            }
            focus_coordinator_.Clear();
            ResetAutomationProvider();
            root_ = nullptr;
            screen_cache_.clear();
            pending_screen_snapshots_.clear();
            render_context_.Reset();
            RemovePropW(window_, L"Terminal.MinimumWidth");
            RemovePropW(window_, L"Terminal.MinimumHeight");
            window_ = nullptr;
            if (destroyed_handler_) destroyed_handler_(*this);
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
    modal_stack_.Paint(render_context_, invalid_region);
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
    modal_stack_.Arrange(client);
    component_host_->layout_dc = nullptr;
}

void WindowContainer::TrackPointer(POINT point) {
    components::Component* target = HitTestInteractive(point);
    if (pointer_target_ && pointer_target_ != target) pointer_target_->PointerMove({-1, -1});
    if (target) target->PointerMove(point);
    pointer_target_ = target;
}

void WindowContainer::DispatchUiEvent(components::Component& source,
                                      std::string_view event_type,
                                      const config::EventDefinition& event) {
    application::UiEvent ui_event;
    ui_event.source = {window_instance_id_, active_screen_instance_id_, source.instance_id(),
                       window_id_, active_route_, source.definition().id};
    ui_event.event_type = std::string(event_type);
    ui_event.action = event.action;
    ui_event.payload = event.payload;
    ui_event.config_generation = document_->generation;
    const auto patch = application_bridge_->Dispatch(ui_event);
    if (!patch) return;
    if (!PatchTargetsEvent(*patch, ui_event) ||
        patch->config_generation != document_->generation ||
        patch->generation <= last_patch_generation_) {
        return;
    }
    last_patch_generation_ = patch->generation;
    if (patch->close_save_result && close_decision_pending_) {
        const application::CloseSaveResult& result = *patch->close_save_result;
        if (result.source.window_instance_id == ui_event.source.window_instance_id &&
            result.source.screen_instance_id == ui_event.source.screen_instance_id &&
            result.source.component_instance_id ==
                ui_event.source.component_instance_id &&
            result.source.window_id == ui_event.source.window_id &&
            result.source.route_id == ui_event.source.route_id &&
            result.source.component_id == ui_event.source.component_id &&
            result.config_generation == ui_event.config_generation &&
            result.config_generation == document_->generation) {
            pending_close_save_result_ = result;
        }
    }
    if (patch->window_title) SetWindowTextW(window_, patch->window_title->c_str());
    if (patch->route_id) {
        std::wstring diagnostic;
        Navigate(*patch->route_id, diagnostic);
    }
    if (patch->dialog_request) {
        std::wstring diagnostic;
        switch (patch->dialog_request->action) {
            case application::DialogRequestAction::Open:
                OpenModal(patch->dialog_request->dialog_id, diagnostic);
                break;
            case application::DialogRequestAction::Save:
                CloseModal(components::ModalResult::Accept, diagnostic);
                break;
            case application::DialogRequestAction::Discard:
                CloseModal(components::ModalResult::Discard, diagnostic);
                break;
            case application::DialogRequestAction::Cancel:
                CloseModal(components::ModalResult::Cancel, diagnostic);
                break;
        }
    }
    if (patch->request_repaint) {
        if (!patch->view_state.empty()) Layout();
        frame_ready_ = false;
        InvalidateRect(window_, nullptr, FALSE);
    }
}

bool WindowContainer::OpenModal(std::string_view dialog_id, std::wstring& diagnostic) {
    if (!root_) {
        diagnostic = L"Component tree tidak tersedia.";
        return false;
    }
    components::Component* dialog = root_->FindById(dialog_id);
    if (!dialog || !dialog->IsModalOverlay()) {
        diagnostic = L"Dialog JSON tidak ditemukan.";
        return false;
    }
    if (GetCapture()) ReleaseCapture();
    pointer_target_ = nullptr;
    if (!modal_stack_.Push(*dialog, *root_, focus_coordinator_, diagnostic)) return false;
    if (automation_provider_) automation_provider_->SetActiveScope(dialog);
    RECT client{};
    GetClientRect(window_, &client);
    modal_stack_.Arrange(client);
    frame_ready_ = false;
    render_context_.InvalidateAll();
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

bool WindowContainer::CloseModal(components::ModalResult result, std::wstring& diagnostic) {
    if (!root_ || !modal_stack_.active()) {
        diagnostic = L"Tidak ada Dialog aktif.";
        return false;
    }
    if (close_decision_pending_) return ResolveCloseDecision(result, diagnostic);
    if (GetCapture()) ReleaseCapture();
    pointer_target_ = nullptr;
    if (!modal_stack_.Pop(result, *root_, focus_coordinator_, diagnostic)) return false;
    if (automation_provider_) automation_provider_->SetActiveScope(modal_stack_.top());
    frame_ready_ = false;
    render_context_.InvalidateAll();
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

bool WindowContainer::ResolveCloseDecision(components::ModalResult result,
                                           std::wstring& diagnostic) {
    if (!close_decision_pending_ || !root_ || !modal_stack_.active()) {
        diagnostic = L"Close confirmation tidak aktif.";
        return false;
    }
    if (result == components::ModalResult::Accept &&
        (!pending_close_save_result_ || !pending_close_save_result_->success)) {
        diagnostic = L"Save belum menghasilkan success patch yang cocok.";
        return false;
    }
    if (result == components::ModalResult::Discard &&
        !StageDiscardTransaction(diagnostic)) {
        return false;
    }

    if (GetCapture()) ReleaseCapture();
    pointer_target_ = nullptr;
    if (!modal_stack_.Pop(result, *root_, focus_coordinator_, diagnostic)) {
        if (result == components::ModalResult::Discard) RollbackDiscardTransaction();
        return false;
    }
    if (automation_provider_) automation_provider_->SetActiveScope(modal_stack_.top());

    const bool accepted = result == components::ModalResult::Accept ||
                          result == components::ModalResult::Discard;
    if (result == components::ModalResult::Accept) ApplySaveSuccess();
    close_prepared_ = accepted;
    close_decision_pending_ = false;
    pending_close_save_result_.reset();
    frame_ready_ = false;
    render_context_.InvalidateAll();
    InvalidateRect(window_, nullptr, FALSE);

    if (!accepted) {
        RollbackDiscardTransaction();
    }
    ClosePreparedHandler handler = std::move(close_prepared_handler_);
    close_prepared_handler_ = {};
    diagnostic.clear();
    if (handler) {
        handler(*this, accepted ? ClosePreparation::Ready
                                : ClosePreparation::Cancelled);
    }
    return true;
}

bool WindowContainer::OpenCloseConfirmation(std::wstring& diagnostic) {
    constexpr std::string_view kDialogId = "save-discard-dialog";
    if (!root_ || !root_->FindById(kDialogId)) {
        diagnostic = L"Dialog konfirmasi close tidak ditemukan pada resolved UI document.";
        return false;
    }
    return OpenModal(kDialogId, diagnostic);
}

void WindowContainer::AttachCloseConfirmationIfMissing(
    components::Component& screen) {
    constexpr std::string_view kDialogId = "save-discard-dialog";
    if (screen.FindById(kDialogId) || !document_ || !component_host_) return;
    for (const auto& [route_id, definition] : document_->screens) {
        (void)route_id;
        const config::ResolvedComponent* dialog =
            FindComponentDefinition(definition, kDialogId);
        if (!dialog || dialog->type != config::ComponentType::Dialog) continue;
        screen.AddChild(registry_.CreateTree(*dialog, *component_host_));
        return;
    }
}

components::Component* WindowContainer::HitTestInteractive(POINT point) const {
    if (modal_stack_.active()) return modal_stack_.HitTest(point);
    return root_ ? root_->HitTest(point) : nullptr;
}

}  // namespace ui::containers
