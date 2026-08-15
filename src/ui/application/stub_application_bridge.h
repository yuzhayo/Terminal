#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "ui/application/ui_action_registry.h"

namespace ui::application {

class StubApplicationBridge final : public UiApplicationBridge {
public:
    StubApplicationBridge();

    std::optional<UiPatch> Dispatch(const UiEvent& event) override;
    const std::map<std::string, std::string, std::less<>>& view_state() const noexcept;
    std::vector<std::wstring> ResolveStringItems(std::string_view binding) const override;
    std::optional<std::wstring> ResolveStringValue(
        std::string_view binding) const override;
    bool RegisterAction(std::string action, UiActionRegistry::Handler handler);
    bool ReplaceAction(std::string_view action, UiActionRegistry::Handler handler);
    void SetStringValue(std::string binding, std::string value);
    void SetStringItems(std::string binding, std::vector<std::wstring> items);
    std::optional<std::string> StringValue(std::string_view binding) const;
    bool HasRegisteredAction(std::string_view action) const noexcept;
    std::size_t registered_action_count() const noexcept;

private:
    void InitializeDeterministicState();
    void RegisterTerminalFeature();
    void RegisterJsonInjectFeature();
    void RegisterJsonEditorFeature();
    void RegisterChromeLauncherFeature();
    void RegisterChromeProfileManagerFeature();
    void RegisterSettingsFeature();
    void RegisterUiEditorFeature();
    void RegisterDialogFeature();
    void RegisterNavigationFeature();
    void RegisterPayloadStateAction(std::string action, std::string payload_key,
                                    std::string binding, std::string status_binding,
                                    std::string status);
    void RegisterStatusAction(std::string action, std::string status_binding,
                              std::string status,
                              std::optional<std::wstring> window_title = std::nullopt);

    UiActionRegistry actions_;
    std::map<std::string, std::string, std::less<>> view_state_;
    std::map<std::string, std::vector<std::wstring>, std::less<>> item_state_;
    std::uint64_t generation_ = 0;
};

}  // namespace ui::application
