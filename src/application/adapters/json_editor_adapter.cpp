#include "application/adapters/json_editor_adapter.h"

#include "application/adapters/logic_adapter_common.h"

namespace application::adapters {
namespace {

struct EditorAdapterState {
    logic::features::EditorTarget target = logic::features::EditorTarget::Windows;
    logic::features::EditorDraft windows;
    logic::features::EditorDraft wsl;
};

logic::features::EditorDraft& ActiveDraft(EditorAdapterState& state) {
    return state.target == logic::features::EditorTarget::Windows ? state.windows : state.wsl;
}

logic::features::EditorTarget TargetFromLabel(std::string_view label) {
    return label == "Ubuntu settings.json" ? logic::features::EditorTarget::UbuntuWsl
                                            : logic::features::EditorTarget::Windows;
}

ui::application::UiPatch Load(ui::application::StubApplicationBridge& bridge,
                              logic::CoreApplication& logic,
                              EditorAdapterState& state, bool force) {
    auto& draft = ActiveDraft(state);
    const auto result = logic.StartEditorLoad(state.target, force, draft);
    const auto status = logic.ApplyEditorLoad(result, &draft);
    ui::application::UiPatch patch =
        StatusPatch(bridge, "viewState.jsonEditorStatus", status);
    if (status.ok()) {
        Put(bridge, patch, "viewState.jsonEditorDraft", ToUtf8(draft.text));
    }
    return patch;
}

}  // namespace

bool RegisterJsonEditorAdapter(
    ui::application::StubApplicationBridge& bridge,
    const std::shared_ptr<logic::CoreApplication>& logic) {
    auto state = std::make_shared<EditorAdapterState>();
    bridge.SetStringItems("viewState.jsonEditorTargets",
                          {L"Windows settings.json", L"Ubuntu settings.json"});
    bridge.SetStringValue("viewState.selectedJsonEditorTarget", "Windows settings.json");
    const ui::application::UiPatch initial = Load(bridge, *logic, *state, false);
    (void)initial;

    bool ok = true;
    ok = bridge.ReplaceAction(
             "select-json-editor-target",
             [&bridge, logic, state](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 state->target = TargetFromLabel(*selected);
                 ui::application::UiPatch patch = Load(bridge, *logic, *state, false);
                 Put(bridge, patch, "viewState.selectedJsonEditorTarget", *selected);
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "update-json-editor-draft",
             [&bridge, state](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto value = PayloadScalar(event, "value");
                 if (!value) return std::nullopt;
                 auto& draft = ActiveDraft(*state);
                 draft.text = ToWide(*value);
                 draft.loaded = true;
                 draft.dirty = true;
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.jsonEditorDraft", *value);
                 Put(bridge, patch, "viewState.jsonEditorStatus",
                     "Draft changed; validate or save when ready.");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "validate-json-editor",
             [&bridge, logic, state](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 return StatusPatch(bridge, "viewState.jsonEditorStatus",
                                    logic->ValidateEditor(ActiveDraft(*state)));
             }) && ok;
    ok = bridge.ReplaceAction(
             "save-json-editor",
             [&bridge, logic, state](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 const auto status = logic->SaveEditor(state->target, &ActiveDraft(*state));
                 return StatusPatch(bridge, "viewState.jsonEditorStatus", status);
             }) && ok;
    ok = bridge.ReplaceAction(
             "restore-json-editor-backup",
             [&bridge, logic, state](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 const auto restore = logic->RestoreEditorBackup(state->target);
                 if (!restore.ok()) {
                     return StatusPatch(bridge, "viewState.jsonEditorStatus", restore);
                 }
                 return Load(bridge, *logic, *state, true);
             }) && ok;
    return ok;
}

}  // namespace application::adapters
