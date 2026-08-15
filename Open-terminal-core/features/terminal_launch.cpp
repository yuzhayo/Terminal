#include "features/terminal_launch.h"

#include "platform/paths.h"
#include "platform/process.h"
#include "platform/strings.h"
#include "platform/wsl.h"
#include "storage/settings.h"

namespace features {
namespace {

std::wstring PowerShellExe() {
    static const std::wstring resolved = [] {
        const std::wstring candidates[] = {
            paths::Join(paths::ExpandEnvironment(L"%ProgramFiles%"), L"PowerShell\\7\\pwsh.exe"),
            paths::Join(paths::LocalAppDataDir(), L"Microsoft\\WindowsApps\\pwsh.exe"),
        };
        for (const std::wstring& c : candidates)
            if (paths::FileExists(c)) return c;
        return paths::Join(paths::ExpandEnvironment(L"%SystemRoot%"),
                           L"System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    }();
    return resolved;
}

std::wstring PowerShellCommand(const std::wstring& folder, const std::wstring& activate_script) {
    std::wstring script = L"Set-Location -LiteralPath '" +
                          str::EscapePowerShellSingleQuoted(folder) + L"'";
    if (!activate_script.empty())
        script += L"; & '" + str::EscapePowerShellSingleQuoted(activate_script) + L"'";
    return script;
}

}  // namespace

std::wstring VenvActivateWindows(const std::wstring& folder) {
    return paths::Join(folder, L".venv\\Scripts\\Activate.ps1");
}

std::wstring VenvActivateWsl(const std::wstring& folder) {
    return paths::Join(folder, L".venv\\bin\\activate");
}

TerminalPlan Plan(const TerminalRequest& request) {
    TerminalPlan plan;

    const std::wstring folder = paths::Normalize(request.folder);
    if (folder.empty()) {
        plan.error = L"Choose a target folder first.";
        return plan;
    }
    if (!paths::DirectoryExists(folder)) {
        plan.error = L"Folder does not exist:\n" + folder;
        return plan;
    }
    if (request.activate_venv) {
        const std::wstring activate = request.target == TerminalTarget::UbuntuWsl
                                          ? VenvActivateWsl(folder)
                                          : VenvActivateWindows(folder);
        if (!paths::FileExists(activate)) {
            plan.error = L"No virtual environment found at:\n" + activate;
            return plan;
        }
    }

    plan.ok = true;
    plan.needs_wsl_probe = (request.target == TerminalTarget::UbuntuWsl && !wsl::IsResolved());
    return plan;
}

core::Status Run(const TerminalRequest& request) {
    // Re-validate so Run is safe to call without a prior Plan call.
    const std::wstring folder = paths::Normalize(request.folder);
    if (folder.empty())
        return core::Error(core::ErrorCode::ValidationFailed, L"Choose a target folder first.");
    if (!paths::DirectoryExists(folder))
        return core::Error(core::ErrorCode::FolderNotFound, L"Folder does not exist:\n" + folder);

    std::wstring error;

    if (request.target == TerminalTarget::UbuntuWsl) {
        std::wstring inner;
        if (request.activate_venv)
            inner = L"source './.venv/bin/activate' && exec bash -i";

        std::wstring command = str::QuoteArg(wsl::Exe());
        if (!request.wsl_distro.empty())
            command += L" -d " + str::QuoteArg(request.wsl_distro);
        command += L" --cd " + str::QuoteArg(folder);
        if (!inner.empty())
            command += L" -- bash -c " + str::QuoteArg(inner);

        if (!process::Launch({}, command, folder, process::Window::NewConsole, &error))
            return core::Error(core::ErrorCode::LaunchFailed, error);
    } else {
        const std::wstring activate_script =
            request.activate_venv ? VenvActivateWindows(folder) : std::wstring();
        const std::wstring exe = PowerShellExe();
        const std::wstring parameters =
            L"-NoLogo -NoExit -ExecutionPolicy Bypass -Command " +
            str::QuoteArg(PowerShellCommand(folder, activate_script));

        if (request.target == TerminalTarget::PowerShellAdmin) {
            if (!process::ShellLaunch(L"runas", exe, parameters, folder, &error))
                return core::Error(core::ErrorCode::LaunchFailed, error);
        } else {
            const std::wstring command = str::QuoteArg(exe) + L" " + parameters;
            if (!process::Launch(exe, command, folder, process::Window::NewConsole, &error))
                return core::Error(core::ErrorCode::LaunchFailed, error);
        }
    }

    // Persist the folder after a successful launch.
    storage::RememberFolder(folder);
    storage::SaveSettings();

    return core::Success(L"Opened in " + folder);
}

bool VenvEnabled(TerminalTarget target) {
    const storage::Settings& s = storage::CurrentSettings();
    switch (target) {
        case TerminalTarget::PowerShellAdmin: return s.venv_powershell_admin;
        case TerminalTarget::PowerShell:      return s.venv_powershell;
        case TerminalTarget::UbuntuWsl:       return s.venv_wsl;
    }
    return false;
}

void SetVenvEnabled(TerminalTarget target, bool enabled) {
    storage::Settings& s = storage::CurrentSettings();
    switch (target) {
        case TerminalTarget::PowerShellAdmin: s.venv_powershell_admin = enabled; break;
        case TerminalTarget::PowerShell:      s.venv_powershell = enabled;       break;
        case TerminalTarget::UbuntuWsl:       s.venv_wsl = enabled;              break;
    }
    storage::SaveSettings();
}

void RememberFolder(const std::wstring& folder) {
    if (folder.empty()) return;
    storage::RememberFolder(folder);
    storage::SaveSettings();
}

const std::vector<std::wstring>& RecentFolders() {
    return storage::CurrentSettings().recent_folders;
}

}  // namespace features
