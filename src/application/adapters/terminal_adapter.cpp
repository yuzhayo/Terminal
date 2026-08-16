#include "application/adapters/terminal_adapter.h"

#include <algorithm>

#include "application/adapters/logic_adapter_common.h"

namespace application::adapters {
namespace {

logic::features::TerminalTarget TargetFromLabel(std::wstring_view label) {
    if (label == L"PowerShell Admin") return logic::features::TerminalTarget::PowerShellAdmin;
    if (label == L"Ubuntu (WSL)") return logic::features::TerminalTarget::UbuntuWsl;
    return logic::features::TerminalTarget::PowerShell;
}

logic::features::TerminalRequest RequestFrom(
    const ui::application::StubApplicationBridge& bridge) {
    logic::features::TerminalRequest request;
    request.folder = ToWide(bridge.StringValue("viewState.terminalInput").value_or(""));
    request.target = TargetFromLabel(ToWide(
        bridge.StringValue("viewState.selectedTerminalProfile").value_or("PowerShell")));
    request.activate_venv = ScalarBool(
        bridge.StringValue("viewState.terminalVenvEnabled").value_or("false"));
    return request;
}

void RefreshRecentFolders(ui::application::StubApplicationBridge& bridge,
                          const logic::CoreApplication& logic) {
    const std::vector<std::wstring> folders = logic.RecentFolders();
    bridge.SetStringItems("viewState.recentFolders", folders);
    const std::wstring current = logic.TerminalFolder();
    if (std::find(folders.begin(), folders.end(), current) != folders.end()) {
        bridge.SetStringValue("viewState.selectedRecentFolder", ToUtf8(current));
    } else if (!folders.empty()) {
        bridge.SetStringValue("viewState.selectedRecentFolder", ToUtf8(folders.front()));
    } else {
        bridge.SetStringValue("viewState.selectedRecentFolder", "");
    }
}

ui::application::UiPatch Execute(ui::application::StubApplicationBridge& bridge,
                                 logic::CoreApplication& logic) {
    const logic::core::Status status = logic.LaunchTerminal(RequestFrom(bridge));
    if (status.ok()) RefreshRecentFolders(bridge, logic);
    return StatusPatch(bridge, "viewState.terminalStatus", status,
                       L"Terminal request completed.");
}

}  // namespace

bool RegisterTerminalAdapter(ui::application::StubApplicationBridge& bridge,
                             const std::shared_ptr<logic::CoreApplication>& logic) {
    bridge.SetStringItems("viewState.terminalProfiles",
                          {L"PowerShell Admin", L"PowerShell", L"Ubuntu (WSL)"});
    bridge.SetStringValue("viewState.terminalInput", ToUtf8(logic->TerminalFolder()));
    bridge.SetStringValue("viewState.selectedTerminalProfile", "PowerShell");
    bridge.SetStringValue("viewState.confirmBeforeRun",
                          logic->ConfirmBeforeRun() ? "true" : "false");
    bridge.SetStringValue("viewState.terminalVenvEnabled",
                          logic->TerminalVenvEnabled(logic::features::TerminalTarget::PowerShell)
                              ? "true" : "false");
    RefreshRecentFolders(bridge, *logic);
    bridge.SetStringValue("viewState.terminalStatus",
                          "Terminal siap. Pilih folder dan target untuk menjalankan.");

    bool ok = true;
    ok = bridge.ReplaceAction(
             "select-terminal-profile",
             [&bridge, logic](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 const auto target = TargetFromLabel(ToWide(*selected));
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.selectedTerminalProfile", *selected);
                 Put(bridge, patch, "viewState.terminalVenvEnabled",
                     logic->TerminalVenvEnabled(target) ? "true" : "false");
                 Put(bridge, patch, "viewState.terminalStatus", "Terminal target selected.");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "toggle-confirm-before-run",
             [&bridge, logic](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto checked = PayloadScalar(event, "checked");
                 if (!checked) return std::nullopt;
                 const bool enabled = ScalarBool(*checked);
                 const auto status = logic->SetConfirmBeforeRun(enabled);
                 ui::application::UiPatch patch =
                     StatusPatch(bridge, "viewState.terminalStatus", status);
                 Put(bridge, patch, "viewState.confirmBeforeRun", enabled ? "true" : "false");
                 Put(bridge, patch, "viewState.settingsConfirmBeforeRun",
                     enabled ? "true" : "false");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "toggle-terminal-venv",
             [&bridge, logic](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto checked = PayloadScalar(event, "checked");
                 if (!checked) return std::nullopt;
                 const bool enabled = ScalarBool(*checked);
                 const auto target = TargetFromLabel(ToWide(
                     bridge.StringValue("viewState.selectedTerminalProfile")
                         .value_or("PowerShell")));
                 const auto status = logic->SetTerminalVenvEnabled(target, enabled);
                 ui::application::UiPatch patch = StatusPatch(
                     bridge, "viewState.terminalStatus", status,
                     enabled ? L"Virtual environment enabled."
                             : L"Virtual environment disabled.");
                 Put(bridge, patch, "viewState.terminalVenvEnabled",
                     enabled ? "true" : "false");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "select-terminal-folder",
             [&bridge](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.selectedRecentFolder", *selected);
                 Put(bridge, patch, "viewState.terminalInput", *selected);
                 Put(bridge, patch, "viewState.terminalStatus", "Recent folder selected.");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "run-terminal",
             [&bridge, logic](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 logic::features::TerminalPlan plan;
                 const auto status = logic->PlanTerminalLaunch(RequestFrom(bridge), &plan);
                 if (!status.ok()) {
                     return StatusPatch(bridge, "viewState.terminalStatus", status);
                 }
                 if (logic->ConfirmBeforeRun()) {
                     ui::application::UiPatch patch;
                     patch.dialog_request = ui::application::DialogRequest{
                         ui::application::DialogRequestAction::Open, "run-confirm-dialog"};
                     patch.request_repaint = true;
                     return patch;
                 }
                 return Execute(bridge, *logic);
             }) && ok;
    ok = bridge.ReplaceAction(
             "run-terminal-confirmed",
             [&bridge, logic](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 auto patch = Execute(bridge, *logic);
                 patch.dialog_request = ui::application::DialogRequest{
                     ui::application::DialogRequestAction::Save, "run-confirm-dialog"};
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "run-terminal-cancel",
             [](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 ui::application::UiPatch patch;
                 patch.dialog_request = ui::application::DialogRequest{
                     ui::application::DialogRequestAction::Cancel, "run-confirm-dialog"};
                 patch.request_repaint = true;
                 return patch;
             }) && ok;
    for (const char* action : {"run-confirmation-accepted", "run-confirmation-cancelled",
                               "run-confirmation-dismissed"}) {
        ok = bridge.ReplaceAction(
                 action, [](const ui::application::UiEvent&)
                     -> std::optional<ui::application::UiPatch> {
                     return ui::application::UiPatch{};
                 }) && ok;
    }
    return ok;
}

}  // namespace application::adapters
