#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <cwchar>
#include <memory>
#include <string>

#include "application/application_container.h"
#include "application/adapters/settings_adapter.h"
#include "application/adapters/chrome_adapter.h"
#include "application/adapters/inject_adapter.h"
#include "application/adapters/json_editor_adapter.h"
#include "application/adapters/terminal_adapter.h"
#include "app/app_identity.h"
#include "instrumentation/performance_trace.h"
#include "platform/app_paths.h"
#include "platform/jump_list.h"
#include "platform/single_instance.h"
#include "platform/updater.h"
#include "platform/windows_runtime.h"
#include "rendering/render_runtime.h"
#include "logic/core_gate.h"
#include "ui/config/ui_config_gate.h"
#include "ui/application/stub_application_bridge.h"
#include "ui/theme/theme_platform_adapter.h"

namespace {

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

    platform::ApplyAppUserModelId();
    platform::SingleInstance single_instance;
    const platform::InstanceClaim claim = single_instance.Claim(command_line);
    switch (claim) {
        case platform::InstanceClaim::Primary: break;
        case platform::InstanceClaim::SecondaryAccepted: return 0;
        case platform::InstanceClaim::SecondaryReceiverNotFound: return 20;
        case platform::InstanceClaim::SecondaryRejected: return 21;
        case platform::InstanceClaim::SecondaryBusy: return 22;
        case platform::InstanceClaim::SecondaryTimedOut: return 23;
        case platform::InstanceClaim::Error: return 4;
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

    ui::theme::ThemePlatformAdapter theme_adapter(
        ui::theme::ThemePlatformAdapter::ReadInitialSnapshot());
    rendering::RenderRuntime render_runtime;
    auto application_bridge = std::make_shared<ui::application::StubApplicationBridge>();
    auto business_logic = std::make_shared<logic::CoreApplication>();
    const logic::core::Status logic_status = business_logic->Initialize();
    if (!logic_status.ok()) {
        ShowBootstrapError(logic_status.text);
        return 17;
    }
    if (!application::adapters::RegisterTerminalAdapter(*application_bridge, business_logic) ||
        !application::adapters::RegisterSettingsAdapter(*application_bridge, business_logic) ||
        !application::adapters::RegisterChromeAdapter(*application_bridge, business_logic) ||
        !application::adapters::RegisterInjectAdapter(*application_bridge, business_logic) ||
        !application::adapters::RegisterJsonEditorAdapter(*application_bridge, business_logic)) {
        ShowBootstrapError(L"Business feature adapters tidak dapat diregistrasikan.");
        return 18;
    }
    const auto startup_request = platform::BuildIpcRequestFromCommandLine(command_line);
    application::ApplicationContainerOptions container_options;
    // Eksperimen: samakan perilaku dengan v1 (Open-terminal) — setiap aktivasi
    // taskbar/jump list membuka window baru.
    container_options.allow_duplicate_route_windows = true;
    application::ApplicationContainer application_container(
        instance, render_runtime, config_gate.document(), theme_adapter, application_bridge,
        container_options);
    if (!application_container.Initialize("main", startup_request, diagnostic)) {
        ShowBootstrapError(diagnostic);
        return 16;
    }
    platform::InstallJumpList(application_container.JumpListRoutes());
    if (!application_container.nonfatal_diagnostic().empty()) {
        OutputDebugStringW((application_container.nonfatal_diagnostic() + L"\n").c_str());
    }
    instrumentation::TraceFirstLayoutComplete();

    ui::containers::WindowContainer* initial_window = application_container.initial_window();
    if (!initial_window || !initial_window->PrepareFirstFrame(diagnostic)) {
        ShowBootstrapError(diagnostic);
        return 15;
    }
    instrumentation::TraceRenderBufferReady();
    initial_window->Show(show_command);
    instrumentation::TraceFirstPresentComplete();
    if (SUCCEEDED(DwmFlush())) {
        instrumentation::TraceFirstFrameVisible();
        instrumentation::TraceResourceSnapshot(render_runtime.diagnostics());
    }
    if (!application_container.StartThemeMonitoring(diagnostic)) {
        OutputDebugStringW((diagnostic + L"\n").c_str());
    }

    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    application_container.BeginShutdown();

    return result == -1 ? 3 : static_cast<int>(message.wParam);
}
