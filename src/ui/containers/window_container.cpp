#include "ui/containers/window_container.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <exception>
#include <stdexcept>

#include "app/app_identity.h"
#include "instrumentation/performance_trace.h"
#include "platform/single_instance.h"
#include "ui/components/button/button_component.h"
#include "ui/components/tabs/tabs_component.h"

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
    window_root_.reset();
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
    const DWORD window_style = WS_POPUP | WS_CLIPCHILDREN;
    const std::wstring title = ResolveWindowTitle(*window_definition_);
    window_ = CreateWindowExW(WS_EX_APPWINDOW, platform::MainWindowClassName(),
                              title.empty() ? app_identity::kProductName : title.c_str(),
                              window_style, CW_USEDEFAULT, CW_USEDEFAULT,
                              window_bounds.right - window_bounds.left,
                              window_bounds.bottom - window_bounds.top, nullptr, nullptr, instance_, this);
    if (!window_) {
        diagnostic = L"Main window tidak dapat dibuat.";
        return false;
    }
    ApplyNonClientTheme();
    ApplyWindowRegion();
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
        component_host_->resolve_route_tabs = [this] {
            std::vector<components::RouteTabDefinition> tabs;
            if (!document_) return tabs;
            tabs.reserve(document_->screens.size());
            for (const auto& [route_id, definition] : document_->screens) {
                const auto& properties =
                    std::get<config::ScreenProperties>(definition.properties);
                if (!properties.show_in_tabs) continue;
                tabs.push_back({route_id, components::Utf8ToWide(properties.tab_label)});
            }
            return tabs;
        };
        component_host_->resolve_active_route = [this]() -> std::string_view {
            return active_route_;
        };
        component_host_->request_route = [this](std::string_view route_id) {
            std::wstring ignored;
            return Navigate(route_id, ignored);
        };
        // Aturan interaksi screen (selectRules single): memilih satu tombol
        // menghapus seleksi tombol lain dalam grup yang sama.
        component_host_->selection_changed = [this](components::Component& source,
                                                    bool now_selected) {
            if (!now_selected || !active_screen_ || !root_) return;
            const auto& screen_properties =
                std::get<config::ScreenProperties>(active_screen_->definition().properties);
            const std::string source_id(source.definition().id);
            for (const auto& rule : screen_properties.select_rules) {
                if (rule.mode != config::SelectMode::Single) continue;
                if (std::find(rule.ids.begin(), rule.ids.end(), source_id) == rule.ids.end()) {
                    continue;
                }
                for (const std::string& other : rule.ids) {
                    if (other == source_id) continue;
                    components::Component* target = root_->FindById(other);
                    if (auto* button = dynamic_cast<components::ButtonComponent*>(target)) {
                        button->SetSelectedOverride(false);
                    }
                }
            }
        };
        if (!BuildWindowRoot(diagnostic)) return false;
        const auto& properties =
            std::get<config::WindowProperties>(window_definition_->properties);
        if (properties.initial_route.empty()) {
            FitWindowToContent();
            diagnostic.clear();
            return true;
        }
        return ActivateRoute(properties.initial_route, diagnostic);
    } catch (const std::exception&) {
        diagnostic = L"Component tree JSON tidak dapat dibuat oleh registry.";
        return false;
    }
}

bool WindowContainer::BuildWindowRoot(std::wstring& diagnostic) {
    try {
        ResetAutomationProvider();
        window_root_ = registry_.CreateTree(*window_definition_, *component_host_);
        root_ = window_root_.get();
        screen_host_ = root_->FindById("screen-host");
        if (screen_host_ && screen_host_->definition().type != config::ComponentType::Container) {
            diagnostic = L"screen-host harus berupa Container.";
            root_ = nullptr;
            window_root_.reset();
            screen_host_ = nullptr;
            return false;
        }
        active_screen_ = nullptr;
        active_route_.clear();
        active_screen_instance_id_ = 0;
        focus_coordinator_.Rebuild(*root_);
        automation_provider_ = new accessibility::AutomationRootProvider(
            window_, *root_, [this] { return focus_coordinator_.focused(); });
        resources_prepared_ = false;
        frame_ready_ = false;
        diagnostic.clear();
        return true;
    } catch (const std::exception&) {
        root_ = nullptr;
        window_root_.reset();
        diagnostic = L"Window JSON tidak dapat dibuat oleh component registry.";
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
    if (!component_host_ || !document_ || !window_root_ || !screen_host_) {
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
    components::Component* previous_screen = active_screen_;
    const std::uint64_t previous_screen_instance_id = active_screen_instance_id_;
    ScreenEntry* previous_entry = nullptr;
    if (!active_route_.empty()) {
        const auto previous = screen_cache_.find(active_route_);
        if (previous != screen_cache_.end()) previous_entry = &previous->second;
    }
    if (previous_entry && active_screen_ && focus_coordinator_.focused() &&
        focus_coordinator_.focused()->IsDescendantOrSelfOf(active_screen_)) {
        previous_entry->focused_component_id =
                focus_coordinator_.focused()->definition().id;
    }
    focus_coordinator_.Clear();
    ResetAutomationProvider();
    if (active_screen_ && !active_screen_->SuspendNativePeers(diagnostic)) {
        focus_coordinator_.Rebuild(*previous_root);
        automation_provider_ = new accessibility::AutomationRootProvider(
                window_, *previous_root, [this] { return focus_coordinator_.focused(); });
        return false;
    }
    if (previous_entry && active_screen_) {
        previous_entry->root = screen_host_->DetachChild(active_screen_);
        if (!previous_entry->root) {
            diagnostic = L"Active screen tidak dapat dilepas dari screen-host.";
            focus_coordinator_.Rebuild(*previous_root);
            automation_provider_ = new accessibility::AutomationRootProvider(
                window_, *previous_root, [this] { return focus_coordinator_.focused(); });
            return false;
        }
        previous_entry->suspended = true;
    }
    active_screen_ = nullptr;

    ScreenEntry* activated_entry = nullptr;
    components::Component* activated_screen = nullptr;
    try {
        auto [entry, inserted] = screen_cache_.try_emplace(std::string(route_id));
        if (inserted) {
            entry->second.instance_id = NextRuntimeInstanceId();
            entry->second.root = registry_.CreateTree(definition->second, *component_host_);
            AttachCloseConfirmationIfMissing(*entry->second.root);
        }
        activated_entry = &entry->second;
        activated_screen = entry->second.root.get();
        if (!activated_screen) throw std::runtime_error("Screen root is not available.");
        active_screen_instance_id_ = entry->second.instance_id;
        activated_screen->OnDpiChanged();
        const auto snapshot = pending_screen_snapshots_.find(entry->first);
        if (snapshot != pending_screen_snapshots_.end()) {
            activated_screen->RestoreRuntimeState(snapshot->second.component_states);
            entry->second.focused_component_id = snapshot->second.focused_component_id;
            pending_screen_snapshots_.erase(snapshot);
        }
        if (entry->second.suspended) {
            activated_screen->ResumeNativePeers();
            entry->second.suspended = false;
        }
        screen_host_->AddChild(std::move(entry->second.root));
        active_screen_ = activated_screen;
        active_route_ = entry->first;
    } catch (const std::exception&) {
        const auto incomplete = screen_cache_.find(route_id);
        if (incomplete != screen_cache_.end() && !incomplete->second.root) {
            screen_cache_.erase(incomplete);
        }
        if (previous_root) {
            if (previous_entry && previous_entry->root) {
                screen_host_->AddChild(std::move(previous_entry->root));
                active_screen_ = previous_screen;
            }
            if (active_screen_) active_screen_->ResumeNativePeers();
            if (previous_entry) previous_entry->suspended = false;
            active_screen_instance_id_ = previous_screen_instance_id;
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

    root_ = window_root_.get();
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
    FitWindowToContent();
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
    if (active_route_.empty()) {
        if (!root_->SuspendNativePeers(diagnostic)) return false;
        diagnostic.clear();
        return true;
    }
    const auto active = screen_cache_.find(active_route_);
    if (active == screen_cache_.end()) {
        diagnostic = L"Active route tidak tercatat pada screen cache.";
        return false;
    }
    if (active_screen_ && focus_coordinator_.focused() &&
        focus_coordinator_.focused()->IsDescendantOrSelfOf(active_screen_)) {
        active->second.focused_component_id =
            focus_coordinator_.focused()->definition().id;
    }
    if (!active->second.suspended && active_screen_) {
        if (!active_screen_->SuspendNativePeers(diagnostic)) return false;
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
    if (!active_route_.empty() && active_screen_) {
        ScreenRuntimeSnapshot snapshot;
        const auto active = screen_cache_.find(active_route_);
        if (active != screen_cache_.end()) {
            snapshot.focused_component_id = active->second.focused_component_id;
            active_screen_->CaptureRuntimeState(snapshot.component_states);
            snapshots.insert_or_assign(active_route_, std::move(snapshot));
        }
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
    window_root_.reset();
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
    active_screen_ = nullptr;
    screen_host_ = nullptr;
    content_minimum_width_ = 0;
    content_minimum_height_ = 0;
    render_runtime_.AdvanceResourceEpoch();
    resources_prepared_ = false;
    frame_ready_ = false;

    if (window_) {
        const std::wstring title = ResolveWindowTitle(*window_definition_);
        SetWindowTextW(window_, title.empty() ? app_identity::kProductName : title.c_str());
        UpdateMinimumTrackSize();
    }
    if (!BuildWindowRoot(diagnostic)) return false;
    if (target_route.empty()) {
        FitWindowToContent();
        diagnostic.clear();
        return true;
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

void WindowContainer::ApplyWindowRegion() noexcept {
    if (!window_ || !document_ || !window_definition_) return;
    RECT client{};
    if (!GetClientRect(window_, &client)) return;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;
    const auto& theme = document_->theme(theme_kind_);
    const int radius = components::ScaleDip(
        theme.styles.at(window_definition_->style_index).radius, dpi_);
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2);
    if (region && SetWindowRgn(window_, region, TRUE) == 0) DeleteObject(region);
}

bool WindowContainer::IsResizeHit(UINT hit) noexcept {
    switch (hit) {
        case HTLEFT:
        case HTRIGHT:
        case HTTOP:
        case HTBOTTOM:
        case HTTOPLEFT:
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:
        case HTBOTTOMRIGHT:
            return true;
        default:
            return false;
    }
}

UINT WindowContainer::HitTestResize(POINT point) const noexcept {
    if (!window_ || !window_definition_ ||
        window_definition_->type != config::ComponentType::Window) {
        return HTNOWHERE;
    }
    const auto& properties =
        std::get<config::WindowProperties>(window_definition_->properties);
    if (!properties.resizable) return HTNOWHERE;
    RECT client{};
    if (!GetClientRect(window_, &client)) return HTNOWHERE;
    const int border = std::max(components::ScaleDip(8, dpi_), 4);
    const bool left = point.x >= client.left && point.x < client.left + border;
    const bool right = point.x < client.right && point.x >= client.right - border;
    const bool top = point.y >= client.top && point.y < client.top + border;
    const bool bottom = point.y < client.bottom && point.y >= client.bottom - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTNOWHERE;
}

void WindowContainer::BeginResize(UINT hit, POINT screen_point) noexcept {
    if (!IsResizeHit(hit) || !window_ || !GetWindowRect(window_, &resize_start_window_)) return;
    resize_hit_ = hit;
    resize_start_screen_ = screen_point;
    resizing_ = true;
    SetCapture(window_);
}

void WindowContainer::UpdateResize(POINT screen_point) noexcept {
    if (!resizing_ || !window_ || !window_definition_) return;
    const auto& properties =
        std::get<config::WindowProperties>(window_definition_->properties);
    const int minimum_width = std::max(
        components::ScaleDip(properties.minimum_width, dpi_), content_minimum_width_);
    const int minimum_height = std::max(
        components::ScaleDip(properties.minimum_height, dpi_), content_minimum_height_);
    const int dx = screen_point.x - resize_start_screen_.x;
    const int dy = screen_point.y - resize_start_screen_.y;
    RECT next = resize_start_window_;
    switch (resize_hit_) {
        case HTLEFT:
            next.left = std::min(resize_start_window_.left + dx,
                                 resize_start_window_.right - minimum_width);
            break;
        case HTRIGHT:
            next.right = std::max(resize_start_window_.right + dx,
                                  resize_start_window_.left + minimum_width);
            break;
        case HTTOP:
            next.top = std::min(resize_start_window_.top + dy,
                                resize_start_window_.bottom - minimum_height);
            break;
        case HTBOTTOM:
            next.bottom = std::max(resize_start_window_.bottom + dy,
                                   resize_start_window_.top + minimum_height);
            break;
        case HTTOPLEFT:
            next.left = std::min(resize_start_window_.left + dx,
                                 resize_start_window_.right - minimum_width);
            next.top = std::min(resize_start_window_.top + dy,
                                resize_start_window_.bottom - minimum_height);
            break;
        case HTTOPRIGHT:
            next.right = std::max(resize_start_window_.right + dx,
                                  resize_start_window_.left + minimum_width);
            next.top = std::min(resize_start_window_.top + dy,
                                resize_start_window_.bottom - minimum_height);
            break;
        case HTBOTTOMLEFT:
            next.left = std::min(resize_start_window_.left + dx,
                                 resize_start_window_.right - minimum_width);
            next.bottom = std::max(resize_start_window_.bottom + dy,
                                   resize_start_window_.top + minimum_height);
            break;
        case HTBOTTOMRIGHT:
            next.right = std::max(resize_start_window_.right + dx,
                                  resize_start_window_.left + minimum_width);
            next.bottom = std::max(resize_start_window_.bottom + dy,
                                   resize_start_window_.top + minimum_height);
            break;
        default:
            return;
    }
    SetWindowPos(window_, nullptr, next.left, next.top, next.right - next.left,
                 next.bottom - next.top, SWP_NOACTIVATE | SWP_NOZORDER);
}

void WindowContainer::EndResize() noexcept {
    if (!resizing_) return;
    resizing_ = false;
    resize_hit_ = HTNOWHERE;
    if (GetCapture() == window_) ReleaseCapture();
}

void WindowContainer::UpdateMinimumTrackSize() noexcept {
    if (!window_ || !window_definition_ ||
        window_definition_->type != config::ComponentType::Window) return;
    const auto& properties =
        std::get<config::WindowProperties>(window_definition_->properties);
    const int minimum_width = std::max(
        components::ScaleDip(properties.minimum_width, dpi_), content_minimum_width_);
    const int configured_minimum_height =
        components::ScaleDip(properties.minimum_height, dpi_);
    const int minimum_height = std::max(configured_minimum_height, content_minimum_height_);
    SetPropW(window_, L"Terminal.MinimumWidth",
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(minimum_width)));
    SetPropW(window_, L"Terminal.MinimumHeight",
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(minimum_height)));
}

void WindowContainer::FitWindowToContent() noexcept {
    if (!window_ || !root_ || !screen_host_ || !component_host_) return;

    RECT client{};
    if (!GetClientRect(window_, &client)) return;
    int available_width = 8192;
    int available_height = 8192;
    MONITORINFO monitor{sizeof(monitor)};
    if (const HMONITOR handle = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
        handle && GetMonitorInfoW(handle, &monitor)) {
        available_width = std::max(1L, monitor.rcWork.right - monitor.rcWork.left);
        available_height = std::max(1L, monitor.rcWork.bottom - monitor.rcWork.top);
    }

    HDC dc = component_host_->layout_dc;
    const bool release_dc = dc == nullptr;
    if (!dc) dc = GetDC(window_);
    if (!dc) return;
    component_host_->layout_dc = dc;

    components::Component* frame = root_->FindById("window-frame");
    components::Component* chrome = root_->FindById("window-chrome");
    components::Component* tabs = root_->FindById("route-tabs");
    components::Component* close = root_->FindById("window-close");

    int horizontal_padding = 0;
    int vertical_padding = 0;
    int gap = 0;
    if (frame && frame->definition().type == config::ComponentType::Container) {
        const auto& properties =
            std::get<config::ContainerProperties>(frame->definition().properties);
        horizontal_padding = components::ScaleDip(
            properties.padding.left + properties.padding.right, dpi_);
        vertical_padding = components::ScaleDip(
            properties.padding.top + properties.padding.bottom, dpi_);
        gap = components::ScaleDip(properties.gap, dpi_);
    }

    const auto& window_properties =
        std::get<config::WindowProperties>(window_definition_->properties);
    const int probe_width = std::max(
        0, std::min(available_width, components::ScaleDip(window_properties.initial_width, dpi_)) -
               horizontal_padding);
    const int close_width =
        close ? close->Measure(dc, probe_width, available_height).width : 0;
    int tabs_width = 0;
    if (auto* route_tabs = dynamic_cast<components::TabsComponent*>(tabs)) {
        tabs_width = route_tabs->PreferredWidth(dc);
    }
    const int screen_width = active_screen_
                                 ? active_screen_->Measure(dc, probe_width, available_height).width
                                 : 0;
    const int inner_width = std::max({close_width, tabs_width, screen_width});
    const int measured_width = horizontal_padding + inner_width;

    int measured_height = vertical_padding;
    int child_count = 0;
    auto add_child = [&](components::Component* child) {
        if (!child) return;
        if (child_count++ > 0) measured_height += gap;
        measured_height += child->Measure(dc, inner_width, available_height).height;
    };
    add_child(chrome);
    add_child(tabs);
    const int screen_height =
        screen_host_->Measure(dc, inner_width, available_height).height;
    add_child(screen_host_);

    content_minimum_width_ = std::max(0, horizontal_padding + close_width);
    content_minimum_height_ = std::max(0, measured_height - screen_height);
    component_host_->layout_dc = nullptr;
    if (release_dc) ReleaseDC(window_, dc);
    UpdateMinimumTrackSize();

    RECT window_bounds{};
    if (!GetWindowRect(window_, &window_bounds)) return;
    const int desired_width =
        std::clamp(measured_width, content_minimum_width_, available_width);
    const int desired_height =
        std::clamp(measured_height, content_minimum_height_, available_height);
    const int current_width = window_bounds.right - window_bounds.left;
    const int current_height = window_bounds.bottom - window_bounds.top;
    if (desired_width == current_width && desired_height == current_height) return;
    SetWindowPos(window_, nullptr, window_bounds.left, window_bounds.top,
                 desired_width, desired_height,
                 SWP_NOACTIVATE | SWP_NOZORDER);
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
    std::size_t count = root_ ? CountDirtyParticipants(*root_) : 0;
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
    if (root_) root_->CollectEditableParticipants(participants);
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
        case WM_NCCALCSIZE:
            // Frameless shell: the client surface owns the full window bounds.
            return 0;
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window_, &point);
            if (root_) {
                if (components::Component* target = HitTestInteractive(point);
                    target && (target->CanFocus() || !target->definition().events.empty())) {
                    return HTCLIENT;
                }
            }
            if (const UINT hit = HitTestResize(point); IsResizeHit(hit)) return hit;
            return HTCAPTION;
        }
        case WM_SETCURSOR: {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window_, &point);
            const UINT hit = resizing_ ? resize_hit_ : HitTestResize(point);
            LPCWSTR cursor = IDC_ARROW;
            switch (hit) {
                case HTLEFT:
                case HTRIGHT:
                    cursor = IDC_SIZEWE;
                    break;
                case HTTOP:
                case HTBOTTOM:
                    cursor = IDC_SIZENS;
                    break;
                case HTTOPLEFT:
                case HTBOTTOMRIGHT:
                    cursor = IDC_SIZENWSE;
                    break;
                case HTTOPRIGHT:
                case HTBOTTOMLEFT:
                    cursor = IDC_SIZENESW;
                    break;
                default:
                    break;
            }
            SetCursor(LoadCursorW(nullptr, cursor));
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
        case WM_GETOBJECT:
            if (automation_provider_ && static_cast<LONG>(lparam) == UiaRootObjectId) {
                return UiaReturnRawElementProvider(
                    window_, wparam, lparam,
                    static_cast<IRawElementProviderSimple*>(automation_provider_));
            }
            break;
        case WM_SIZE:
            ApplyWindowRegion();
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
            if (resizing_) {
                POINT screen_point{};
                GetCursorPos(&screen_point);
                UpdateResize(screen_point);
                return 0;
            }
            TraceInputStart();
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
            TrackMouseEvent(&tracking);
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (GetCapture() == window_ && pointer_target_) pointer_target_->PointerMove(point);
            else TrackPointer(point);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (resizing_) return 0;
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
        case WM_NCLBUTTONDOWN:
            if (IsResizeHit(static_cast<UINT>(wparam))) {
                const POINT screen_point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                BeginResize(static_cast<UINT>(wparam), screen_point);
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
            if (!target || (target->CanFocus() == false && target->definition().events.empty())) {
                const UINT resize_hit = HitTestResize(point);
                if (IsResizeHit(resize_hit)) {
                    POINT screen_point = point;
                    ClientToScreen(window_, &screen_point);
                    BeginResize(resize_hit, screen_point);
                    return 0;
                }
            }
            if (target && target->CanFocus()) focus_coordinator_.RequestFocus(target);
            if (target && target->PointerDown(point)) pointer_target_ = target;
            return 0;
        }
        case WM_LBUTTONUP: {
            if (resizing_) {
                EndResize();
                return 0;
            }
            TraceInputStart();
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (pointer_target_) pointer_target_->PointerUp(point);
            pointer_target_ = nullptr;
            TrackPointer(point);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (resizing_) {
                resizing_ = false;
                resize_hit_ = HTNOWHERE;
            }
            break;
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
            screen_host_ = nullptr;
            active_screen_ = nullptr;
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
    if (event.action == "close-window") {
        if (close_requested_handler_) close_requested_handler_(*this);
        return;
    }
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
