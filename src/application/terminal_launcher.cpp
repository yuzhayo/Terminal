#include "application/terminal_launcher.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include "ui/application/stub_application_bridge.h"

namespace application {
namespace {

std::wstring ToWide(const std::string& value) {
    if (value.empty()) return std::wstring{};
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return std::wstring{};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), required);
    return result;
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return std::string{};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return std::string{};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), required, nullptr, nullptr);
    return result;
}

std::wstring ExpandEnvironment(const std::wstring& value) {
    const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (required == 0) return value;
    std::wstring result(required, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(value.c_str(), result.data(), required);
    if (written == 0 || written >= required) return value;
    result.resize(written - 1);
    return result;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring EscapePowerShellSingleQuoted(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        result += character;
        if (character == L'\'') result += L'\'';
    }
    return result;
}

std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    for (const wchar_t character : value) {
        if (character == L'"') result += L'\\';
        result += character;
    }
    result += L'"';
    return result;
}

std::wstring SystemErrorMessage(DWORD code) {
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message;
    if (length != 0 && buffer) message.assign(buffer, length);
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' ||
                                message.back() == L' ')) {
        message.pop_back();
    }
    if (message.empty()) message = L"error " + std::to_wstring(code);
    return message;
}

std::wstring Trim(const std::wstring& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && iswspace(value[begin])) ++begin;
    while (end > begin && iswspace(value[end - 1])) --end;
    return value.substr(begin, end - begin);
}

}  // namespace

std::optional<TerminalTarget> TerminalTargetFromProfile(std::wstring_view profile) noexcept {
    if (profile == L"PowerShell Admin") return TerminalTarget::PowerShellAdmin;
    if (profile == L"PowerShell") return TerminalTarget::PowerShell;
    if (profile == L"Ubuntu (WSL)") return TerminalTarget::UbuntuWsl;
    return std::nullopt;
}

std::wstring BuildPowerShellArguments(const std::wstring& folder) {
    return L"-NoLogo -NoExit -ExecutionPolicy Bypass -Command " +
           QuoteArgument(L"Set-Location -LiteralPath '" + EscapePowerShellSingleQuoted(folder) + L"'");
}

std::wstring BuildWslArguments(const std::wstring& folder) {
    return L"--cd " + QuoteArgument(folder);
}

std::wstring ResolvePowerShellExecutable() {
    const std::wstring candidates[] = {
        ExpandEnvironment(L"%ProgramFiles%") + L"\\PowerShell\\7\\pwsh.exe",
        ExpandEnvironment(L"%LocalAppData%") + L"\\Microsoft\\WindowsApps\\pwsh.exe",
    };
    for (const std::wstring& candidate : candidates) {
        if (FileExists(candidate)) return candidate;
    }
    return ExpandEnvironment(L"%SystemRoot%") + L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
}

std::wstring ResolveWslExecutable() {
    return ExpandEnvironment(L"%SystemRoot%") + L"\\System32\\wsl.exe";
}

TerminalLaunchOutcome TerminalLauncher::Plan(TerminalTarget target, const std::wstring& raw_folder) const {
    (void)target;
    const std::wstring folder = Trim(raw_folder);
    if (folder.empty()) {
        return {false, L"Pilih folder tujuan terlebih dahulu."};
    }
    if (!DirectoryExists(folder)) {
        return {false, L"Folder tidak ditemukan: " + folder};
    }
    return {true, std::wstring{}};
}

TerminalLaunchOutcome TerminalLauncher::Run(TerminalTarget target, const std::wstring& raw_folder) {
    const std::wstring folder = Trim(raw_folder);
    const TerminalLaunchOutcome plan = Plan(target, folder);
    if (!plan.success) return plan;
    if (launch_hook_) return launch_hook_(target, folder);
    return Launch(target, folder);
}

TerminalLaunchOutcome TerminalLauncher::Launch(TerminalTarget target,
                                               const std::wstring& folder) const {
    if (target == TerminalTarget::UbuntuWsl) {
        const std::wstring executable = ResolveWslExecutable();
        if (!FileExists(executable)) {
            return {false, L"WSL tidak tersedia pada sistem ini (wsl.exe tidak ditemukan)."};
        }
        std::wstring command_line = QuoteArgument(executable) + L" " + BuildWslArguments(folder);
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                            CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE, nullptr,
                            folder.c_str(), &startup, &process)) {
            return {false, L"Gagal menjalankan WSL: " + SystemErrorMessage(GetLastError())};
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return {true, L"Terminal Ubuntu (WSL) dibuka di " + folder};
    }

    const std::wstring executable = ResolvePowerShellExecutable();
    const std::wstring arguments = BuildPowerShellArguments(folder);

    if (target == TerminalTarget::PowerShellAdmin) {
        const HRESULT com_result = CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"runas";
        info.lpFile = executable.c_str();
        info.lpParameters = arguments.c_str();
        info.lpDirectory = folder.c_str();
        info.nShow = SW_SHOWNORMAL;
        const BOOL executed = ShellExecuteExW(&info);
        const DWORD code = GetLastError();
        if (SUCCEEDED(com_result)) CoUninitialize();
        if (!executed) {
            if (code == ERROR_CANCELLED) {
                return {false, L"Elevation dibatalkan oleh user."};
            }
            return {false, L"Gagal menjalankan PowerShell Admin: " + SystemErrorMessage(code)};
        }
        if (info.hProcess) CloseHandle(info.hProcess);
        return {true, L"Terminal PowerShell Admin dibuka di " + folder};
    }

    std::wstring command_line = QuoteArgument(executable) + L" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE, nullptr,
                        folder.c_str(), &startup, &process)) {
        return {false, L"Gagal menjalankan PowerShell: " + SystemErrorMessage(GetLastError())};
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {true, L"Terminal PowerShell dibuka di " + folder};
}

void TerminalLauncher::SetLaunchHookForTest(LaunchHook hook) {
    launch_hook_ = std::move(hook);
}

namespace {

std::optional<std::string> ViewStateString(const ui::application::StubApplicationBridge& bridge,
                                           const std::string& key) {
    const auto& state = bridge.view_state();
    const auto found = state.find(key);
    return found == state.end() ? std::nullopt : std::optional<std::string>(found->second);
}

ui::application::UiPatch StatusPatch(const std::string& status) {
    ui::application::UiPatch patch;
    patch.view_state["viewState.terminalStatus"] = status;
    patch.request_repaint = true;
    return patch;
}

std::optional<ui::application::UiPatch> ExecuteTerminal(TerminalLauncher& launcher,
                                                        const ui::application::UiEvent& event,
                                                        ui::application::StubApplicationBridge& bridge) {
    (void)event;
    const std::string profile =
        ViewStateString(bridge, "viewState.selectedTerminalProfile").value_or(std::string{});
    const std::string folder =
        ViewStateString(bridge, "viewState.terminalInput").value_or(std::string{});
    const auto target = TerminalTargetFromProfile(ToWide(profile));
    if (!target) {
        return StatusPatch("Profile terminal tidak dikenal: " + profile);
    }
    const TerminalLaunchOutcome outcome = launcher.Run(*target, ToWide(folder));
    return StatusPatch(ToUtf8(outcome.message));
}

}  // namespace

bool RegisterTerminalLauncherHandlers(ui::application::StubApplicationBridge& bridge,
                                      TerminalLauncher& launcher) {
    if (!bridge.ReplaceAction(
            "run-terminal-stub",
            [&bridge, &launcher](const ui::application::UiEvent& event)
                -> std::optional<ui::application::UiPatch> {
                const std::string confirm =
                    ViewStateString(bridge, "viewState.confirmBeforeRun").value_or("false");
                if (confirm == "true") {
                    ui::application::UiPatch patch;
                    patch.dialog_request = ui::application::DialogRequest{
                        ui::application::DialogRequestAction::Open, "run-confirm-dialog"};
                    patch.request_repaint = true;
                    return patch;
                }
                return ExecuteTerminal(launcher, event, bridge);
            })) {
        return false;
    }
    if (!bridge.ReplaceAction(
            "run-terminal-confirmed",
            [&bridge, &launcher](const ui::application::UiEvent& event)
                -> std::optional<ui::application::UiPatch> {
                auto result = ExecuteTerminal(launcher, event, bridge);
                if (result) {
                    result->dialog_request = ui::application::DialogRequest{
                        ui::application::DialogRequestAction::Save, "run-confirm-dialog"};
                }
                return result;
            })) {
        return false;
    }
    return true;
}

}  // namespace application
