#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ui::application {
class StubApplicationBridge;
}

namespace application {

enum class TerminalTarget { PowerShellAdmin, PowerShell, UbuntuWsl };

std::optional<TerminalTarget> TerminalTargetFromProfile(std::wstring_view profile) noexcept;

std::wstring BuildPowerShellArguments(const std::wstring& folder);
std::wstring BuildWslArguments(const std::wstring& folder);
std::wstring ResolvePowerShellExecutable();
std::wstring ResolveWslExecutable();

struct TerminalLaunchOutcome {
    bool success = false;
    std::wstring message;
};

class TerminalLauncher final {
public:
    using LaunchHook = std::function<TerminalLaunchOutcome(TerminalTarget, const std::wstring&)>;

    TerminalLaunchOutcome Plan(TerminalTarget target, const std::wstring& folder) const;
    TerminalLaunchOutcome Run(TerminalTarget target, const std::wstring& folder);

    void SetLaunchHookForTest(LaunchHook hook);

private:
    TerminalLaunchOutcome Launch(TerminalTarget target, const std::wstring& folder) const;

    LaunchHook launch_hook_;
};

bool RegisterTerminalLauncherHandlers(ui::application::StubApplicationBridge& bridge,
                                      TerminalLauncher& launcher);

}  // namespace application
