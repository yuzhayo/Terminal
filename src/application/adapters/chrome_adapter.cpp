#include "application/adapters/chrome_adapter.h"

#include <algorithm>

#include "application/adapters/logic_adapter_common.h"

namespace application::adapters {
namespace {

struct ChromeAdapterState {
    logic::features::ChromeRuntime runtime = logic::features::ChromeRuntime::Windows;
    logic::features::VisibleSetDraft windows_draft;
    logic::features::VisibleSetDraft wsl_draft;
    std::vector<logic::features::VisibleSetRow> rows;
    std::vector<logic::features::ChromeProfile> visible_profiles;
    std::vector<logic::features::ChromeBookmark> bookmarks;
};

logic::features::VisibleSetDraft& ActiveDraft(ChromeAdapterState& state) {
    return state.runtime == logic::features::ChromeRuntime::Windows
               ? state.windows_draft
               : state.wsl_draft;
}

std::wstring ProfileLabel(const logic::features::ChromeProfile& profile) {
    const std::wstring name = profile.name.empty() ? profile.directory : profile.name;
    return name + L"    " + profile.RuntimeLabel() + L" · " + profile.directory;
}

std::wstring BookmarkLabel(const logic::features::ChromeBookmark& bookmark) {
    return bookmark.label.empty() ? bookmark.url : bookmark.label + L" — " + bookmark.url;
}

void Refresh(ui::application::StubApplicationBridge& bridge,
             logic::CoreApplication& logic, ChromeAdapterState& state) {
    state.visible_profiles = logic.VisibleChromeProfiles(state.runtime);
    std::vector<std::wstring> profiles;
    profiles.reserve(state.visible_profiles.size());
    for (const auto& profile : state.visible_profiles) profiles.push_back(ProfileLabel(profile));
    bridge.SetStringItems("viewState.chromeProfiles", profiles);
    if (!profiles.empty()) {
        const std::string current =
            bridge.StringValue("viewState.selectedChromeProfile").value_or("");
        if (std::find(profiles.begin(), profiles.end(), ToWide(current)) == profiles.end()) {
            bridge.SetStringValue("viewState.selectedChromeProfile", ToUtf8(profiles.front()));
        }
    } else {
        bridge.SetStringValue("viewState.selectedChromeProfile", "");
    }

    state.bookmarks = logic.ChromeBookmarks();
    std::vector<std::wstring> bookmarks;
    bookmarks.reserve(state.bookmarks.size());
    for (const auto& bookmark : state.bookmarks) bookmarks.push_back(BookmarkLabel(bookmark));
    bridge.SetStringItems("viewState.chromeBookmarks", bookmarks);
    bridge.SetStringValue("viewState.selectedChromeBookmark", "");
    const std::wstring selected_bookmark = logic.SelectedChromeBookmarkId();
    for (std::size_t index = 0; index < state.bookmarks.size(); ++index) {
        if (state.bookmarks[index].id == selected_bookmark) {
            bridge.SetStringValue("viewState.selectedChromeBookmark",
                                  ToUtf8(bookmarks[index]));
            break;
        }
    }

    const auto cached = logic.CachedChromeProfiles(state.runtime);
    state.rows = logic::features::BuildRows(state.runtime, cached, &ActiveDraft(state));
    std::vector<std::wstring> managed;
    managed.reserve(state.rows.size());
    for (const auto& row : state.rows) managed.push_back(row.label);
    bridge.SetStringItems("viewState.managedChromeProfiles", managed);
    if (!state.rows.empty()) {
        bridge.SetStringValue("viewState.selectedManagedProfile", ToUtf8(state.rows.front().label));
        bridge.SetStringValue("viewState.profileName", ToUtf8(state.rows.front().name));
        bridge.SetStringValue("viewState.profileEnabled",
                              state.rows.front().checked ? "true" : "false");
    } else {
        bridge.SetStringValue("viewState.selectedManagedProfile", "");
        bridge.SetStringValue("viewState.profileName", "");
        bridge.SetStringValue("viewState.profileEnabled", "false");
    }
}

std::optional<std::size_t> FindLabel(const std::vector<std::wstring>& labels,
                                     std::wstring_view selected) {
    const auto found = std::find(labels.begin(), labels.end(), selected);
    if (found == labels.end()) return std::nullopt;
    return static_cast<std::size_t>(std::distance(labels.begin(), found));
}

}  // namespace

bool RegisterChromeAdapter(ui::application::StubApplicationBridge& bridge,
                           const std::shared_ptr<logic::CoreApplication>& logic) {
    auto state = std::make_shared<ChromeAdapterState>();
    state->runtime = logic->ActiveChromeRuntime();
    bridge.SetStringItems("viewState.chromeRuntimes", {L"Windows", L"Ubuntu (WSL)"});
    bridge.SetStringValue("viewState.selectedChromeRuntime",
                          state->runtime == logic::features::ChromeRuntime::Windows
                              ? "Windows"
                              : "Ubuntu (WSL)");
    bridge.SetStringValue("viewState.chromeUrl", "");
    Refresh(bridge, *logic, *state);
    bridge.SetStringValue(
        "viewState.chromeStatus",
        state->visible_profiles.empty()
            ? "Belum ada profile Chrome yang terlihat. Buka Profile Manager untuk memilih profile."
            : "Profile Chrome siap digunakan.");
    bridge.SetStringValue(
        "viewState.profileStatus",
        state->rows.empty()
            ? "Belum ada cache profile. Tekan Refresh profiles untuk memindai."
            : "Profile Chrome dimuat dari cache.");

    bool ok = true;
    ok = bridge.ReplaceAction(
             "select-chrome-profile",
             [&bridge](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.selectedChromeProfile", *selected);
                 Put(bridge, patch, "viewState.chromeStatus", "Chrome profile selected.");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "select-chrome-bookmark",
             [&bridge, logic, state](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 std::vector<std::wstring> labels;
                 for (const auto& bookmark : state->bookmarks) {
                     labels.push_back(BookmarkLabel(bookmark));
                 }
                 const auto index = FindLabel(labels, ToWide(*selected));
                 if (!index) return std::nullopt;
                 const auto status = logic->SelectChromeBookmarkById(
                     state->bookmarks[*index].id);
                 ui::application::UiPatch patch =
                     StatusPatch(bridge, "viewState.chromeStatus", status,
                                 L"Chrome bookmark selected.");
                 Put(bridge, patch, "viewState.selectedChromeBookmark", *selected);
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "launch-chrome",
             [&bridge, logic, state](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 const std::wstring selected = ToWide(
                     bridge.StringValue("viewState.selectedChromeProfile").value_or(""));
                 std::vector<std::wstring> labels;
                 for (const auto& profile : state->visible_profiles) {
                     labels.push_back(ProfileLabel(profile));
                 }
                 const auto index = FindLabel(labels, selected);
                 if (!index) {
                     return TextPatch(bridge, "viewState.chromeStatus",
                                      L"Choose a visible Chrome profile first.");
                 }
                 const auto result = logic->LaunchChromeCard(
                     *index, ToWide(bridge.StringValue("viewState.chromeUrl").value_or("")));
                 ui::application::UiPatch patch =
                     StatusPatch(bridge, "viewState.chromeStatus", result.status);
                 if (result.clear_input) Put(bridge, patch, "viewState.chromeUrl", "");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "select-managed-chrome-profile",
             [&bridge, state](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 const auto found = std::find_if(
                     state->rows.begin(), state->rows.end(), [&](const auto& row) {
                         return row.label == ToWide(*selected);
                     });
                 if (found == state->rows.end()) return std::nullopt;
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.selectedManagedProfile", *selected);
                 Put(bridge, patch, "viewState.profileName", ToUtf8(found->name));
                 Put(bridge, patch, "viewState.profileEnabled",
                     found->checked ? "true" : "false");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "toggle-chrome-profile-enabled",
             [&bridge, state](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto checked = PayloadScalar(event, "checked");
                 if (!checked) return std::nullopt;
                 const std::wstring selected = ToWide(
                     bridge.StringValue("viewState.selectedManagedProfile").value_or(""));
                 for (std::size_t index = 0; index < state->rows.size(); ++index) {
                     if (state->rows[index].label != selected) continue;
                     if (state->rows[index].checked != ScalarBool(*checked)) {
                         logic::features::ToggleRow(index, state->rows, &ActiveDraft(*state));
                         state->rows[index].checked = ScalarBool(*checked);
                     }
                     ui::application::UiPatch patch;
                     Put(bridge, patch, "viewState.profileEnabled", *checked);
                     Put(bridge, patch, "viewState.profileStatus",
                         "Visible profile draft changed; press Save.");
                     return patch;
                 }
                 return std::nullopt;
             }) && ok;
    ok = bridge.ReplaceAction(
             "save-chrome-profiles",
             [&bridge, logic, state](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 if (!logic::features::Apply(state->runtime, &ActiveDraft(*state))) {
                     return TextPatch(bridge, "viewState.profileStatus",
                                      L"Could not save visible Chrome profiles.");
                 }
                 Refresh(bridge, *logic, *state);
                 return TextPatch(bridge, "viewState.profileStatus",
                                  L"Visible Chrome profiles saved.");
             }) && ok;
    ok = bridge.ReplaceAction(
             "select-chrome-runtime",
             [&bridge, logic, state](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 state->runtime = *selected == "Ubuntu (WSL)"
                                      ? logic::features::ChromeRuntime::Wsl
                                      : logic::features::ChromeRuntime::Windows;
                 const auto status = logic->SwitchChromeRuntime(state->runtime);
                 Refresh(bridge, *logic, *state);
                 ui::application::UiPatch patch =
                     StatusPatch(bridge, "viewState.profileStatus", status);
                 Put(bridge, patch, "viewState.selectedChromeRuntime", *selected);
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "refresh-chrome-profiles",
             [&bridge, logic, state](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 const auto result = logic->StartChromeScan(state->runtime);
                 const auto status = logic->ApplyChromeScan(result);
                 Refresh(bridge, *logic, *state);
                 return StatusPatch(bridge, "viewState.profileStatus", status);
             }) && ok;
    return ok;
}

}  // namespace application::adapters
