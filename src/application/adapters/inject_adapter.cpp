#include "application/adapters/inject_adapter.h"

#include <algorithm>

#include "application/adapters/logic_adapter_common.h"

namespace application::adapters {
namespace {

struct InjectAdapterState {
    std::vector<logic::features::InjectChoice> providers;
    std::vector<logic::features::InjectChoice> keys;
};

std::optional<logic::features::InjectChoice> FindChoice(
    const std::vector<logic::features::InjectChoice>& choices,
    std::wstring_view label) {
    const auto found = std::find_if(choices.begin(), choices.end(),
                                    [&](const auto& choice) {
                                        return choice.label == label;
                                    });
    return found == choices.end() ? std::nullopt
                                  : std::optional<logic::features::InjectChoice>(*found);
}

void Refresh(ui::application::StubApplicationBridge& bridge,
             logic::CoreApplication& logic, InjectAdapterState& state) {
    state.providers = logic.BaseUrlChoices();
    std::vector<std::wstring> provider_labels;
    for (const auto& provider : state.providers) provider_labels.push_back(provider.label);
    bridge.SetStringItems("viewState.injectProviders", provider_labels);
    const std::wstring selected_provider = logic.SelectedBaseUrlId();
    for (const auto& provider : state.providers) {
        if (provider.id == selected_provider) {
            bridge.SetStringValue("viewState.selectedInjectProvider",
                                  ToUtf8(provider.label));
            break;
        }
    }

    state.keys = logic.ApiKeyChoices();
    std::vector<std::wstring> key_labels;
    for (const auto& key : state.keys) key_labels.push_back(key.label);
    bridge.SetStringItems("viewState.injectApiKeys", key_labels);
    const std::wstring selected_key = logic.SelectedApiKeyId();
    for (const auto& key : state.keys) {
        if (key.id == selected_key) {
            bridge.SetStringValue("viewState.selectedInjectApiKey", ToUtf8(key.label));
            break;
        }
    }
    bridge.SetStringValue("viewState.injectModel", ToUtf8(logic.SelectedInjectModel()));
}

}  // namespace

bool RegisterInjectAdapter(ui::application::StubApplicationBridge& bridge,
                           const std::shared_ptr<logic::CoreApplication>& logic) {
    auto state = std::make_shared<InjectAdapterState>();
    bridge.SetStringItems("viewState.injectTargets", {L"Windows", L"Ubuntu (WSL)"});
    bridge.SetStringValue("viewState.selectedInjectTarget", "Windows");
    bridge.SetStringValue("viewState.injectBaseUrl", "");
    bridge.SetStringValue("viewState.injectApiKeyDraft", "");
    Refresh(bridge, *logic, *state);

    bool ok = true;
    ok = bridge.ReplaceAction(
             "select-inject-provider",
             [&bridge, logic, state](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 const auto choice = FindChoice(state->providers, ToWide(*selected));
                 if (!choice) return std::nullopt;
                 const auto status = logic->SelectBaseUrl(choice->id);
                 Refresh(bridge, *logic, *state);
                 ui::application::UiPatch patch =
                     StatusPatch(bridge, "viewState.injectStatus", status,
                                 L"Provider selected.");
                 Put(bridge, patch, "viewState.selectedInjectProvider", *selected);
                 Put(bridge, patch, "viewState.injectModel",
                     ToUtf8(logic->SelectedInjectModel()));
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "select-inject-target",
             [&bridge](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 ui::application::UiPatch patch;
                 Put(bridge, patch, "viewState.selectedInjectTarget", *selected);
                 Put(bridge, patch, "viewState.injectStatus", "Inject target selected.");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "select-inject-api-key",
             [&bridge, logic, state](const ui::application::UiEvent& event)
                 -> std::optional<ui::application::UiPatch> {
                 const auto selected = PayloadScalar(event, "selectedValue");
                 if (!selected) return std::nullopt;
                 const auto choice = FindChoice(state->keys, ToWide(*selected));
                 if (!choice) return std::nullopt;
                 const auto status = logic->SelectApiKey(choice->id);
                 ui::application::UiPatch patch =
                     StatusPatch(bridge, "viewState.injectStatus", status,
                                 L"API key selected.");
                 Put(bridge, patch, "viewState.selectedInjectApiKey", *selected);
                 return patch;
             }) && ok;
    for (const auto [action, binding] : {
             std::pair{"update-inject-base-url", "viewState.injectBaseUrl"},
             std::pair{"update-inject-model", "viewState.injectModel"},
             std::pair{"update-inject-api-keys", "viewState.injectApiKeyDraft"}}) {
        ok = bridge.ReplaceAction(
                 action, [&bridge, binding](const ui::application::UiEvent& event)
                     -> std::optional<ui::application::UiPatch> {
                     const auto value = PayloadScalar(event, "value");
                     if (!value) return std::nullopt;
                     ui::application::UiPatch patch;
                     Put(bridge, patch, binding, *value);
                     Put(bridge, patch, "viewState.injectStatus",
                         "Provider draft changed; press Save provider.");
                     return patch;
                 }) && ok;
    }
    ok = bridge.ReplaceAction(
             "save-inject-provider",
             [&bridge, logic, state](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 const std::wstring url = ToWide(
                     bridge.StringValue("viewState.injectBaseUrl").value_or(""));
                 const std::wstring model = ToWide(
                     bridge.StringValue("viewState.injectModel").value_or(""));
                 const std::string key_draft =
                     bridge.StringValue("viewState.injectApiKeyDraft").value_or("");
                 if (!url.empty()) {
                     const auto add_status = logic->AddBaseUrl(url, {}, model);
                     if (!add_status.ok()) {
                         return StatusPatch(bridge, "viewState.injectStatus", add_status);
                     }
                 }
                 const auto model_status = logic->CommitInjectModel(model);
                 if (!model_status.ok()) {
                     return StatusPatch(bridge, "viewState.injectStatus", model_status);
                 }
                 if (!key_draft.empty()) {
                     const auto key_status = logic->BulkAddApiKeys({ToWide(key_draft)});
                     if (!key_status.ok()) {
                         return StatusPatch(bridge, "viewState.injectStatus", key_status);
                     }
                 }
                 Refresh(bridge, *logic, *state);
                 ui::application::UiPatch patch = TextPatch(
                     bridge, "viewState.injectStatus", L"Provider configuration saved.");
                 Put(bridge, patch, "viewState.injectApiKeyDraft", "");
                 return patch;
             }) && ok;
    ok = bridge.ReplaceAction(
             "apply-inject",
             [&bridge, logic](const ui::application::UiEvent&)
                 -> std::optional<ui::application::UiPatch> {
                 const bool wsl_target =
                     bridge.StringValue("viewState.selectedInjectTarget")
                         .value_or("Windows") == "Ubuntu (WSL)";
                 const auto status = logic->InjectClaude(
                     wsl_target ? logic::features::InjectTarget::UbuntuWsl
                                : logic::features::InjectTarget::Windows);
                 return StatusPatch(bridge, "viewState.injectStatus", status);
             }) && ok;
    return ok;
}

}  // namespace application::adapters
