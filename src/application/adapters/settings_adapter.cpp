#include "application/adapters/settings_adapter.h"

#include <cwctype>

#include "application/adapters/logic_adapter_common.h"

namespace application::adapters {
namespace {

std::wstring TitleToken(std::wstring value) {
    if (!value.empty()) value[0] = static_cast<wchar_t>(std::towupper(value[0]));
    return value;
}

std::wstring LowerToken(std::wstring value) {
    for (wchar_t& character : value) {
        character = static_cast<wchar_t>(std::towlower(character));
    }
    return value;
}

}  // namespace

bool RegisterSettingsAdapter(ui::application::StubApplicationBridge& bridge,
                             const std::shared_ptr<logic::CoreApplication>& logic) {
    bridge.SetStringItems("viewState.themeOptions", {L"System", L"Dark", L"Light"});
    bridge.SetStringValue("viewState.selectedTheme",
                          ToUtf8(TitleToken(logic->CurrentTheme())));
    bridge.SetStringValue("viewState.startupToTray",
                          logic->IsStartWithWindowsEnabled() ? "true" : "false");
    bridge.SetStringValue("viewState.settingsConfirmBeforeRun",
                          logic->ConfirmBeforeRun() ? "true" : "false");
    bridge.SetStringItems("viewState.recentFolders", logic->RecentFolders());

    bool ok = true;
    ok = bridge.ReplaceAction(
             "select-settings-theme",
             [&bridge](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.selectedTheme", *selected);
                 Put(bridge, patch, "viewState.settingsStatus",
                     "Theme preference changed; press Apply.");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "toggle-startup-to-tray",
             [&bridge](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto checked = PayloadScalar(event, "checked");
                 if (!checked) return std::nullopt;
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.startupToTray", *checked);
                 Put(bridge, patch, "viewState.settingsStatus",
                     "Startup preference changed; press Apply.");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "toggle-settings-confirm-before-run",
             [&bridge](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto checked = PayloadScalar(event, "checked");
                 if (!checked) return std::nullopt;
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.settingsConfirmBeforeRun", *checked);
                 Put(bridge, patch, "viewState.confirmBeforeRun", *checked);
                 Put(bridge, patch, "viewState.settingsStatus",
                     "Confirmation preference changed; press Apply.");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "select-recent-folder",
             [&bridge](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.selectedRecentFolder", *selected);
                 Put(bridge, patch, "viewState.terminalInput", *selected);
                 Put(bridge, patch, "viewState.settingsStatus",
                     "Recent folder selected for Terminal.");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "apply-settings",
             [&bridge, logic](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 const std::wstring theme = LowerToken(ToWide(
                     bridge.StringValue("viewState.selectedTheme").value_or("System")));
                 const bool startup = ScalarBool(
                     bridge.StringValue("viewState.startupToTray").value_or("false"));
                 const bool confirm = ScalarBool(
                     bridge.StringValue("viewState.settingsConfirmBeforeRun")
                         .value_or("true"));
                 for (const logic::core::Status status : {
                          logic->SetTheme(theme), logic->SetStartWithWindows(startup),
                          logic->SetConfirmBeforeRun(confirm)}) {
                     if (!status.ok()) {
                         return StatusPatch(bridge, "viewState.settingsStatus", status);
                     }
                 }
                 ui::application::UiPatch patch = TextPatch(
                     bridge, "viewState.settingsStatus", L"Settings saved.");
                 Put(bridge, patch, "viewState.confirmBeforeRun",
                     confirm ? "true" : "false");
                 return patch;
             }) && ok;
    return ok;
}

}  // namespace application::adapters
