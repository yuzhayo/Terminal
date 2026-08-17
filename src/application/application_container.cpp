#include "application/application_container.h"

#include <shellapi.h>

#include <algorithm>
#include <cassert>
#include <cwchar>
#include <set>
#include <utility>

#include "app/app_identity.h"
#include "platform/single_instance.h"
#include "resource.h"

namespace application {
namespace {

constexpr UINT kTrayIconId = 1;
constexpr UINT kFirstRouteCommand = 1000;
constexpr UINT kExitCommand = 1999;

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), count) <= 0) {
        return {};
    }
    return result;
}

}  // namespace

ApplicationContainer::ApplicationContainer(
    HINSTANCE instance, rendering::RenderRuntime& render_runtime,
    std::shared_ptr<const ui::config::ResolvedUiDocument> document,
    ui::theme::ThemePlatformAdapter& theme_adapter,
    std::shared_ptr<ui::application::UiApplicationBridge> application_bridge,
    ApplicationContainerOptions options)
    : instance_(instance), render_runtime_(render_runtime), document_(std::move(document)),
      theme_adapter_(theme_adapter), application_bridge_(std::move(application_bridge)),
      options_(options) {}

ApplicationContainer::~ApplicationContainer() { BeginShutdown(); }

bool ApplicationContainer::Initialize(
    std::string window_id, const std::optional<platform::IpcRequest>& startup_request,
    std::wstring& diagnostic) {
    if (!document_ || !application_bridge_ || !document_->windows.contains(window_id)) {
        diagnostic = L"ApplicationContainer membutuhkan window JSON dan application bridge.";
        return false;
    }
    if (infrastructure_window_.hwnd() || !windows_.empty()) {
        diagnostic = L"ApplicationContainer sudah diinisialisasi.";
        return false;
    }
    window_definition_id_ = std::move(window_id);

    if (!infrastructure_window_.Create(
            instance_,
            [this](const platform::IpcRequest& request) { HandleIpcRequest(request); },
            [this](std::string_view route_id) { return IsConfiguredRoute(route_id); },
            [this](std::uint32_t signals) { HandleProcessSignals(signals); },
            [this](TrayInteraction interaction, POINT point) {
                HandleTrayInteraction(interaction, point);
            },
            [this] { HandleTaskbarCreated(); },
            [this] { DrainApplicationWork(); }, diagnostic)) {
        return false;
    }

    std::string initial_route = DefaultRoute();
    if (startup_request && startup_request->command == platform::IpcCommand::OpenRoute) {
        if (!IsConfiguredRoute(startup_request->route_id)) {
            diagnostic = L"Startup route tidak tersedia pada resolved UI document.";
            return false;
        }
        initial_route = startup_request->route_id;
    }
    if (!CreateRouteWindow(initial_route, false, SW_HIDE, diagnostic)) return false;

    if (options_.enable_tray) {
        std::wstring tray_diagnostic;
        if (!InstallTrayIcon(tray_diagnostic)) nonfatal_diagnostic_ = std::move(tray_diagnostic);
    }
    diagnostic.clear();
    return true;
}

bool ApplicationContainer::PrepareAndShowInitialWindow(int show_command,
                                                       std::wstring& diagnostic) {
    auto found = windows_.find(initial_window_id_);
    if (found == windows_.end() || !found->second.container) {
        diagnostic = L"Initial route window tidak tersedia.";
        return false;
    }
    if (!found->second.container->PrepareFirstFrame(diagnostic)) return false;
    found->second.container->Show(show_command);
    diagnostic.clear();
    return true;
}

bool ApplicationContainer::StartThemeMonitoring(std::wstring& diagnostic) noexcept {
    return theme_adapter_.StartPostFirstFrameMonitoring(
        infrastructure_window_.hwnd(), infrastructure_window_.theme_signal_message(),
        diagnostic);
}

bool ApplicationContainer::OpenExternalRoute(std::string_view route_id,
                                             std::wstring& diagnostic) {
    DrainApplicationWork();
    if (close_operation_.kind != CloseOperationKind::None) {
        diagnostic = L"Route intent ditunda karena close transaction sedang aktif.";
        return false;
    }
    if (!IsConfiguredRoute(route_id)) {
        diagnostic = L"External route tidak tersedia pada resolved UI document.";
        return false;
    }
    if (const auto existing_id = FindRouteWindowId(route_id)) {
        if (retained_window_id_ == existing_id) {
            return RestoreRetainedWindow(*existing_id, options_.created_window_show_command,
                                         diagnostic);
        }
        platform::ActivateMainWindow(windows_.at(*existing_id).container->hwnd());
        diagnostic.clear();
        return true;
    }
    return CreateRouteWindow(route_id, true, options_.created_window_show_command,
                             diagnostic) != nullptr;
}

void ApplicationContainer::ActivateDefault() {
    DrainApplicationWork();
    if (retained_window_id_) {
        std::wstring diagnostic;
        if (!RestoreRetainedWindow(*retained_window_id_,
                                   options_.created_window_show_command, diagnostic)) {
            nonfatal_diagnostic_ = std::move(diagnostic);
        }
        return;
    }
    if (ui::containers::WindowContainer* initial = initial_window()) {
        platform::ActivateMainWindow(initial->hwnd());
        return;
    }
    if (!windows_.empty()) {
        platform::ActivateMainWindow(windows_.begin()->second.container->hwnd());
        return;
    }
    std::wstring diagnostic;
    if (!CreateRouteWindow(DefaultRoute(), true, options_.created_window_show_command,
                           diagnostic)) {
        nonfatal_diagnostic_ = std::move(diagnostic);
    }
}

void ApplicationContainer::HandleIpcRequest(const platform::IpcRequest& request) {
    if (shutdown_in_progress_) return;
    switch (request.command) {
        case platform::IpcCommand::ActivateDefault: {
            std::wstring diagnostic;
            if (!CreateRouteWindow(DefaultRoute(), true,
                                   options_.created_window_show_command, diagnostic)) {
                nonfatal_diagnostic_ = std::move(diagnostic);
            }
            break;
        }
        case platform::IpcCommand::OpenRoute: {
            std::wstring diagnostic;
            if (!OpenExternalRoute(request.route_id, diagnostic)) {
                nonfatal_diagnostic_ = std::move(diagnostic);
            }
            break;
        }
        case platform::IpcCommand::RequestExit:
            RequestExit();
            break;
    }
}

void ApplicationContainer::BeginShutdown() noexcept {
    if (shutdown_in_progress_) return;
    shutdown_in_progress_ = true;
    infrastructure_window_.BeginShutdown();
    RemoveTrayIcon();
    for (auto& [id, record] : windows_) {
        (void)id;
        if (record.container) {
            record.container->SetDestroyedHandler({});
            record.container->SetCloseRequestedHandler({});
        }
    }
    windows_.clear();
    destroyed_window_ids_.clear();
    retained_window_id_.reset();
    shutdown_after_drain_ = false;
    ClearCloseOperation();
}

ui::containers::WindowContainer* ApplicationContainer::initial_window() noexcept {
    const auto found = windows_.find(initial_window_id_);
    return found == windows_.end() ? nullptr : found->second.container.get();
}

ui::containers::WindowContainer* ApplicationContainer::FindRouteWindow(
    std::string_view route_id) noexcept {
    const auto id = FindRouteWindowId(route_id);
    return id ? windows_.at(*id).container.get() : nullptr;
}

std::size_t ApplicationContainer::window_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        windows_.begin(), windows_.end(), [](const auto& item) {
            return item.second.container && item.second.container->hwnd();
        }));
}

std::size_t ApplicationContainer::visible_window_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        windows_.begin(), windows_.end(), [](const auto& item) {
            return item.second.container && item.second.container->hwnd() &&
                   IsWindowVisible(item.second.container->hwnd());
        }));
}

ui::containers::WindowContainer* ApplicationContainer::retained_window() noexcept {
    if (!retained_window_id_) return nullptr;
    const auto found = windows_.find(*retained_window_id_);
    return found == windows_.end() ? nullptr : found->second.container.get();
}

bool ApplicationContainer::route_registry_is_unique() const noexcept {
    std::set<std::string_view, std::less<>> routes;
    for (const auto& [id, record] : windows_) {
        (void)id;
        if (!record.container || !record.container->hwnd() ||
            record.container->active_route().empty()) {
            continue;
        }
        if (!routes.insert(record.container->active_route()).second) return false;
    }
    if (retained_window_id_) {
        const auto retained = windows_.find(*retained_window_id_);
        if (retained == windows_.end() || !retained->second.container ||
            !retained->second.container->hwnd() ||
            IsWindowVisible(retained->second.container->hwnd())) {
            return false;
        }
    }
    return true;
}

bool ApplicationContainer::tray_available() const noexcept { return tray_icon_added_; }

HWND ApplicationContainer::infrastructure_hwnd() const noexcept {
    return infrastructure_window_.hwnd();
}

UINT ApplicationContainer::taskbar_created_message() const noexcept {
    return infrastructure_window_.taskbar_created_message();
}

const std::wstring& ApplicationContainer::nonfatal_diagnostic() const noexcept {
    return nonfatal_diagnostic_;
}

ui::containers::WindowContainer* ApplicationContainer::CreateRouteWindow(
    std::string_view route_id, bool prepare_and_show, int show_command,
    std::wstring& diagnostic) {
    if (!route_id.empty() && !IsConfiguredRoute(route_id)) {
        diagnostic = L"Route window tidak dapat dibuat karena route tidak terdaftar.";
        return nullptr;
    }
    if (!route_id.empty() && FindRouteWindowId(route_id)) {
        diagnostic = L"Route window duplikat ditolak oleh process registry.";
        return nullptr;
    }

    const std::uint64_t registry_id = next_window_id_++;
    auto container = std::make_unique<ui::containers::WindowContainer>(
        instance_, render_runtime_, document_,
        theme_adapter_.Select(ui::config::ThemePreference::System), application_bridge_);
    if (!container->Create(window_definition_id_, diagnostic)) return nullptr;
    if (!route_id.empty() && container->active_route() != route_id &&
        !container->Navigate(route_id, diagnostic)) {
        return nullptr;
    }
    if (prepare_and_show && !container->PrepareFirstFrame(diagnostic)) return nullptr;

    container->SetDestroyedHandler(
        [this, registry_id](ui::containers::WindowContainer&) {
            OnWindowDestroyed(registry_id);
        });
    container->SetRouteRequestHandler(
        [this](ui::containers::WindowContainer& source, std::string_view target_route,
               std::wstring& route_diagnostic) {
            return HandleSameWindowRoute(source, target_route, route_diagnostic);
        });
    container->SetCloseRequestedHandler(
        [this](ui::containers::WindowContainer& target) { RequestCloseWindow(target); });
    ui::containers::WindowContainer* result = container.get();
    windows_.emplace(registry_id, WindowRecord{std::move(container)});
    if (initial_window_id_ == 0) initial_window_id_ = registry_id;
    if (prepare_and_show) result->Show(show_command);
    AssertRouteRegistryInvariant();
    diagnostic.clear();
    return result;
}

bool ApplicationContainer::HandleSameWindowRoute(
    ui::containers::WindowContainer& source, std::string_view route_id,
    std::wstring& diagnostic) {
    DrainApplicationWork();
    if (close_operation_.kind != CloseOperationKind::None) {
        diagnostic = L"Navigation ditunda karena close transaction sedang aktif.";
        return false;
    }
    if (!FindWindowId(source)) {
        diagnostic = L"Route source tidak terdaftar pada process window registry.";
        return false;
    }
    if (!IsConfiguredRoute(route_id)) {
        diagnostic = L"Same-window route tidak tersedia pada resolved UI document.";
        return false;
    }
    if (source.active_route() == route_id) {
        diagnostic.clear();
        AssertRouteRegistryInvariant();
        return true;
    }
    if (const auto existing_id = FindRouteWindowId(route_id)) {
        if (retained_window_id_ == existing_id) {
            return RestoreRetainedWindow(*existing_id, options_.created_window_show_command,
                                         diagnostic);
        }
        platform::ActivateMainWindow(windows_.at(*existing_id).container->hwnd());
        diagnostic.clear();
        AssertRouteRegistryInvariant();
        return true;
    }
    const bool navigated = source.ActivateRoute(route_id, diagnostic);
    AssertRouteRegistryInvariant();
    return navigated;
}

bool ApplicationContainer::RetainRouteWindow(
    ui::containers::WindowContainer& target, std::wstring& diagnostic) {
    const auto target_id = FindWindowId(target);
    if (!target_id || !target.hwnd()) {
        diagnostic = L"Route window yang akan disimpan tidak terdaftar.";
        return false;
    }
    if (retained_window_id_) {
        if (*retained_window_id_ == *target_id) {
            diagnostic.clear();
            return true;
        }
        diagnostic = L"V1 hanya mengizinkan satu retained hidden route window.";
        return false;
    }
    if (!target.SuspendNativePeers(diagnostic)) return false;
    ShowWindow(target.hwnd(), SW_HIDE);
    target.ReleaseRetainedResources();
    retained_window_id_ = *target_id;
    AssertRouteRegistryInvariant();
    diagnostic.clear();
    return true;
}

bool ApplicationContainer::RestoreRetainedWindow(
    std::uint64_t registry_id, int show_command, std::wstring& diagnostic) {
    if (!retained_window_id_ || *retained_window_id_ != registry_id) {
        diagnostic = L"Retained route window tidak cocok dengan registry slot.";
        return false;
    }
    const auto found = windows_.find(registry_id);
    if (found == windows_.end() || !found->second.container ||
        !found->second.container->hwnd()) {
        diagnostic = L"Retained route window tidak lagi tersedia.";
        retained_window_id_.reset();
        return false;
    }
    ui::containers::WindowContainer& target = *found->second.container;
    if (!target.RestoreAndShow(show_command, diagnostic)) return false;
    retained_window_id_.reset();
    platform::ActivateMainWindow(target.hwnd());
    AssertRouteRegistryInvariant();
    diagnostic.clear();
    return true;
}

std::optional<std::uint64_t> ApplicationContainer::FindWindowId(
    const ui::containers::WindowContainer& target) const noexcept {
    for (const auto& [id, record] : windows_) {
        if (record.container.get() == &target && record.container->hwnd()) return id;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> ApplicationContainer::FindRouteWindowId(
    std::string_view route_id) const noexcept {
    std::optional<std::uint64_t> result;
    for (const auto& [id, record] : windows_) {
        if (!record.container || !record.container->hwnd() ||
            record.container->active_route() != route_id) {
            continue;
        }
        assert(!result.has_value() && "Duplicate route window in process registry");
        if (!result) result = id;
    }
    return result;
}

void ApplicationContainer::AssertRouteRegistryInvariant() const noexcept {
    assert(route_registry_is_unique() && "Application route registry invariant violated");
}

std::size_t ApplicationContainer::reachable_window_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        windows_.begin(), windows_.end(), [this](const auto& item) {
            return item.second.container && item.second.container->hwnd() &&
                   retained_window_id_ != item.first;
        }));
}

void ApplicationContainer::RequestCloseWindow(
    ui::containers::WindowContainer& target) {
    DrainApplicationWork();
    if (shutdown_in_progress_ || close_operation_.kind != CloseOperationKind::None) return;
    const auto target_id = FindWindowId(target);
    if (!target_id || retained_window_id_ == target_id) return;

    if (reachable_window_count() > 1) {
        BeginCloseOne(*target_id);
        return;
    }
    if (tray_icon_added_) {
        if (!retained_window_id_) {
            std::wstring diagnostic;
            if (!RetainRouteWindow(target, diagnostic)) {
                nonfatal_diagnostic_ = std::move(diagnostic);
            }
            return;
        }
        BeginRetainedReplacement(*retained_window_id_, *target_id);
        return;
    }
    BeginCloseAll();
}

void ApplicationContainer::BeginCloseOne(std::uint64_t registry_id) {
    const auto found = windows_.find(registry_id);
    if (found == windows_.end() || !found->second.container) return;
    close_operation_.kind = CloseOperationKind::CloseOne;
    close_operation_.primary_id = registry_id;
    std::wstring diagnostic;
    const ui::containers::ClosePreparation preparation =
        found->second.container->PrepareClose(
            [this, registry_id](ui::containers::WindowContainer&,
                                ui::containers::ClosePreparation result) {
                CompleteCloseOne(registry_id, result);
            },
            diagnostic);
    if (preparation == ui::containers::ClosePreparation::Ready ||
        preparation == ui::containers::ClosePreparation::Failed) {
        CompleteCloseOne(registry_id, preparation);
    }
    if (!diagnostic.empty()) nonfatal_diagnostic_ = std::move(diagnostic);
}

void ApplicationContainer::CompleteCloseOne(
    std::uint64_t registry_id, ui::containers::ClosePreparation preparation) {
    if (close_operation_.kind != CloseOperationKind::CloseOne ||
        close_operation_.primary_id != registry_id) {
        return;
    }
    const auto found = windows_.find(registry_id);
    if (preparation == ui::containers::ClosePreparation::Ready &&
        found != windows_.end() && found->second.container) {
        std::wstring diagnostic;
        if (!found->second.container->CommitClose(diagnostic)) {
            found->second.container->RollbackPreparedClose();
            nonfatal_diagnostic_ = std::move(diagnostic);
        }
    }
    ClearCloseOperation();
}

void ApplicationContainer::BeginRetainedReplacement(
    std::uint64_t retained_id, std::uint64_t replacement_id) {
    close_operation_.kind = CloseOperationKind::ReplaceRetained;
    close_operation_.primary_id = retained_id;
    close_operation_.secondary_id = replacement_id;
    std::wstring diagnostic;
    if (!RestoreRetainedWindow(retained_id, options_.created_window_show_command,
                               diagnostic)) {
        nonfatal_diagnostic_ = std::move(diagnostic);
        ClearCloseOperation();
        return;
    }
    const auto retained = windows_.find(retained_id);
    if (retained == windows_.end() || !retained->second.container) {
        ClearCloseOperation();
        return;
    }
    const ui::containers::ClosePreparation preparation =
        retained->second.container->PrepareClose(
            [this, retained_id, replacement_id](
                ui::containers::WindowContainer&,
                ui::containers::ClosePreparation result) {
                CompleteRetainedReplacement(retained_id, replacement_id, result);
            },
            diagnostic);
    if (preparation == ui::containers::ClosePreparation::Ready ||
        preparation == ui::containers::ClosePreparation::Failed) {
        CompleteRetainedReplacement(retained_id, replacement_id, preparation);
    }
    if (!diagnostic.empty()) nonfatal_diagnostic_ = std::move(diagnostic);
}

void ApplicationContainer::CompleteRetainedReplacement(
    std::uint64_t retained_id, std::uint64_t replacement_id,
    ui::containers::ClosePreparation preparation) {
    if (close_operation_.kind != CloseOperationKind::ReplaceRetained ||
        close_operation_.primary_id != retained_id ||
        close_operation_.secondary_id != replacement_id) {
        return;
    }
    auto retained = windows_.find(retained_id);
    auto replacement = windows_.find(replacement_id);
    std::wstring diagnostic;
    if (preparation == ui::containers::ClosePreparation::Ready &&
        retained != windows_.end() && retained->second.container &&
        replacement != windows_.end() && replacement->second.container) {
        if (retained->second.container->CommitClose(diagnostic)) {
            if (!RetainRouteWindow(*replacement->second.container, diagnostic)) {
                nonfatal_diagnostic_ = diagnostic;
            }
        } else if (retained->second.container->hwnd()) {
            retained->second.container->RollbackPreparedClose();
            if (!RetainRouteWindow(*retained->second.container, diagnostic)) {
                nonfatal_diagnostic_ = diagnostic;
            }
        }
    } else if (retained != windows_.end() && retained->second.container &&
               retained->second.container->hwnd()) {
        retained->second.container->RollbackPreparedClose();
        if (!RetainRouteWindow(*retained->second.container, diagnostic)) {
            nonfatal_diagnostic_ = diagnostic;
        }
    }
    if (!diagnostic.empty()) nonfatal_diagnostic_ = std::move(diagnostic);
    ClearCloseOperation();
}

void ApplicationContainer::BeginCloseAll() {
    if (shutdown_in_progress_ || close_operation_.kind != CloseOperationKind::None) return;
    close_operation_.kind = CloseOperationKind::ExitAll;
    close_operation_.original_retained_id = retained_window_id_;
    if (retained_window_id_) {
        std::wstring diagnostic;
        if (!RestoreRetainedWindow(*retained_window_id_,
                                   options_.created_window_show_command, diagnostic)) {
            nonfatal_diagnostic_ = std::move(diagnostic);
            ClearCloseOperation();
            return;
        }
    }
    for (const auto& [id, record] : windows_) {
        if (record.container && record.container->hwnd()) {
            close_operation_.window_ids.push_back(id);
        }
    }
    ContinuePrepareCloseAll();
}

void ApplicationContainer::ContinuePrepareCloseAll() {
    while (close_operation_.kind == CloseOperationKind::ExitAll &&
           close_operation_.next_index < close_operation_.window_ids.size()) {
        const std::uint64_t id =
            close_operation_.window_ids[close_operation_.next_index];
        const auto found = windows_.find(id);
        if (found == windows_.end() || !found->second.container ||
            !found->second.container->hwnd()) {
            ++close_operation_.next_index;
            continue;
        }
        std::wstring diagnostic;
        const ui::containers::ClosePreparation preparation =
            found->second.container->PrepareClose(
                [this, id](ui::containers::WindowContainer&,
                           ui::containers::ClosePreparation result) {
                    CompletePrepareCloseAllWindow(id, result);
                },
                diagnostic);
        if (!diagnostic.empty()) nonfatal_diagnostic_ = std::move(diagnostic);
        if (preparation == ui::containers::ClosePreparation::AwaitingDecision) return;
        if (preparation != ui::containers::ClosePreparation::Ready) {
            CancelCloseAll();
            return;
        }
        close_operation_.prepared_ids.push_back(id);
        ++close_operation_.next_index;
    }
    if (close_operation_.kind == CloseOperationKind::ExitAll) CommitCloseAll();
}

void ApplicationContainer::CompletePrepareCloseAllWindow(
    std::uint64_t registry_id, ui::containers::ClosePreparation preparation) {
    if (close_operation_.kind != CloseOperationKind::ExitAll ||
        close_operation_.next_index >= close_operation_.window_ids.size() ||
        close_operation_.window_ids[close_operation_.next_index] != registry_id) {
        return;
    }
    if (preparation != ui::containers::ClosePreparation::Ready) {
        CancelCloseAll();
        return;
    }
    close_operation_.prepared_ids.push_back(registry_id);
    ++close_operation_.next_index;
    ContinuePrepareCloseAll();
}

void ApplicationContainer::CancelCloseAll() {
    if (close_operation_.kind != CloseOperationKind::ExitAll) return;
    const std::optional<std::uint64_t> original_retained =
        close_operation_.original_retained_id;
    for (const std::uint64_t id : close_operation_.prepared_ids) {
        const auto found = windows_.find(id);
        if (found != windows_.end() && found->second.container) {
            found->second.container->RollbackPreparedClose();
        }
    }
    ClearCloseOperation();
    if (original_retained) {
        const auto found = windows_.find(*original_retained);
        if (found != windows_.end() && found->second.container &&
            found->second.container->hwnd()) {
            std::wstring diagnostic;
            if (!RetainRouteWindow(*found->second.container, diagnostic)) {
                nonfatal_diagnostic_ = std::move(diagnostic);
            }
        }
    }
}

void ApplicationContainer::CommitCloseAll() {
    if (close_operation_.kind != CloseOperationKind::ExitAll) return;
    const std::vector<std::uint64_t> prepared = close_operation_.prepared_ids;
    bool all_committed = true;
    for (const std::uint64_t id : prepared) {
        const auto found = windows_.find(id);
        if (found == windows_.end() || !found->second.container ||
            !found->second.container->hwnd()) {
            continue;
        }
        std::wstring diagnostic;
        if (!found->second.container->CommitClose(diagnostic)) {
            all_committed = false;
            if (nonfatal_diagnostic_.empty()) {
                nonfatal_diagnostic_ = std::move(diagnostic);
            }
        }
    }
    ClearCloseOperation();
    if (all_committed) {
        shutdown_after_drain_ = true;
        infrastructure_window_.PostApplicationWork();
    }
}

void ApplicationContainer::ClearCloseOperation() noexcept {
    close_operation_ = {};
}

void ApplicationContainer::OnWindowDestroyed(std::uint64_t registry_id) noexcept {
    destroyed_window_ids_.push_back(registry_id);
    infrastructure_window_.PostApplicationWork();
}

void ApplicationContainer::DrainApplicationWork() {
    if (!destroyed_window_ids_.empty()) {
        std::vector<std::uint64_t> destroyed = std::move(destroyed_window_ids_);
        destroyed_window_ids_.clear();
        for (const std::uint64_t id : destroyed) {
            if (id == initial_window_id_) initial_window_id_ = 0;
            if (retained_window_id_ == id) retained_window_id_.reset();
            windows_.erase(id);
        }
    }
    AssertRouteRegistryInvariant();
    if (shutdown_after_drain_ && !shutdown_in_progress_) {
        shutdown_after_drain_ = false;
        BeginShutdown();
        PostQuitMessage(0);
        return;
    }
    if (windows_.empty() && !tray_icon_added_ && !shutdown_in_progress_) {
        PostQuitMessage(0);
    }
}

void ApplicationContainer::HandleProcessSignals(std::uint32_t signals) {
    if (shutdown_in_progress_ || signals == 0) return;
    const bool post_frame_theme_signal =
        (signals & SignalMask(ProcessGlobalSignal::Theme)) != 0;
    const ui::theme::ThemePlatformSnapshot snapshot =
        post_frame_theme_signal ? theme_adapter_.ReadPostFirstFrameSnapshot()
                                : ui::theme::ThemePlatformAdapter::ReadInitialSnapshot();
    theme_adapter_.Reconcile(snapshot);
    render_runtime_.AdvanceResourceEpoch();
    const ui::config::ThemeKind theme_kind =
        theme_adapter_.Select(ui::config::ThemePreference::System);
    for (auto& [id, record] : windows_) {
        (void)id;
        if (record.container && record.container->hwnd()) {
            record.container->ApplySharedTheme(theme_kind);
        }
    }
}

void ApplicationContainer::HandleTrayInteraction(TrayInteraction interaction, POINT point) {
    if (shutdown_in_progress_) return;
    if (interaction == TrayInteraction::ActivateDefault) ActivateDefault();
    else ShowTrayMenu(point);
}

void ApplicationContainer::HandleTaskbarCreated() {
    if (shutdown_in_progress_ || !options_.enable_tray) return;
    tray_icon_added_ = false;
    std::wstring diagnostic;
    if (!InstallTrayIcon(diagnostic)) {
        nonfatal_diagnostic_ = std::move(diagnostic);
        if (retained_window_id_) {
            std::wstring restore_diagnostic;
            if (!RestoreRetainedWindow(*retained_window_id_,
                                       options_.created_window_show_command,
                                       restore_diagnostic)) {
                nonfatal_diagnostic_ = std::move(restore_diagnostic);
            }
        } else if (reachable_window_count() == 0) {
            ActivateDefault();
        }
    }
}

void ApplicationContainer::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    std::vector<std::string> routes;
    routes.reserve(document_->screens.size());
    UINT command = kFirstRouteCommand;
    for (const auto& [route_id, definition] : document_->screens) {
        (void)definition;
        const std::wstring label = Utf8ToWide(route_id);
        if (label.empty() || command >= kExitCommand) continue;
        if (AppendMenuW(menu, MF_STRING, command, label.c_str())) {
            routes.push_back(route_id);
            ++command;
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");

    SetForegroundWindow(infrastructure_window_.hwnd());
    const UINT selected = TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        point.x, point.y, infrastructure_window_.hwnd(), nullptr);
    DestroyMenu(menu);
    PostMessageW(infrastructure_window_.hwnd(), WM_NULL, 0, 0);

    if (selected == kExitCommand) {
        RequestExit();
    } else if (selected >= kFirstRouteCommand &&
               selected < kFirstRouteCommand + routes.size()) {
        std::wstring diagnostic;
        if (!OpenExternalRoute(routes[selected - kFirstRouteCommand], diagnostic)) {
            nonfatal_diagnostic_ = std::move(diagnostic);
        }
    }
}

bool ApplicationContainer::InstallTrayIcon(std::wstring& diagnostic) noexcept {
    if (!options_.enable_tray || !infrastructure_window_.hwnd()) {
        diagnostic = L"Tray tidak diaktifkan untuk ApplicationContainer.";
        return false;
    }
    if (tray_icon_added_) {
        diagnostic.clear();
        return true;
    }

    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = infrastructure_window_.hwnd();
    icon.uID = kTrayIconId;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    icon.uCallbackMessage = infrastructure_window_.tray_callback_message();
    icon.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(icon.szTip, app_identity::kProductName);
    if (!icon.hIcon || !Shell_NotifyIconW(NIM_ADD, &icon)) {
        diagnostic = L"Tray icon tidak dapat dipasang.";
        tray_icon_added_ = false;
        return false;
    }
    icon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon);
    tray_icon_added_ = true;
    diagnostic.clear();
    return true;
}

void ApplicationContainer::RemoveTrayIcon() noexcept {
    if (!tray_icon_added_ || !infrastructure_window_.hwnd()) return;
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = infrastructure_window_.hwnd();
    icon.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    tray_icon_added_ = false;
}

void ApplicationContainer::RequestExit() {
    if (shutdown_in_progress_) return;
    BeginCloseAll();
}

std::string ApplicationContainer::DefaultRoute() const {
    if (!document_) return {};
    const auto window = document_->windows.find(window_definition_id_);
    if (window == document_->windows.end() ||
        window->second.type != ui::config::ComponentType::Window) {
        return {};
    }
    return std::get<ui::config::WindowProperties>(window->second.properties).initial_route;
}

bool ApplicationContainer::IsConfiguredRoute(std::string_view route_id) const noexcept {
    return document_ && document_->screens.contains(route_id);
}

}  // namespace application
