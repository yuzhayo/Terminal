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
#include "rendering/render_runtime.h"
#include "ui/config/ui_config_gate.h"
#include "ui/containers/window_container.h"
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

    ui::theme::ThemePlatformAdapter theme_adapter(
        ui::theme::ThemePlatformAdapter::ReadInitialSnapshot());
    rendering::RenderRuntime render_runtime;
    ui::containers::WindowContainer window_container(
        instance, render_runtime, config_gate.document(),
        theme_adapter.Select(ui::config::ThemePreference::System));
    if (!window_container.Create("main", diagnostic)) {
        ShowBootstrapError(diagnostic);
        return 2;
    }
    instrumentation::TraceFirstLayoutComplete();

    if (!window_container.PrepareFirstFrame(diagnostic)) {
        ShowBootstrapError(diagnostic);
        return 15;
    }
    instrumentation::TraceRenderBufferReady();
    window_container.Show(show_command);
    instrumentation::TraceFirstPresentComplete();
    if (SUCCEEDED(DwmFlush())) {
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
