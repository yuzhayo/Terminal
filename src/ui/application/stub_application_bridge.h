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
    std::size_t registered_action_count() const noexcept;

private:
    void RegisterTerminalFeature();
    void RegisterDialogFeature();
    void RegisterNavigationFeature();

    UiActionRegistry actions_;
    std::map<std::string, std::string, std::less<>> view_state_;
    std::uint64_t generation_ = 0;
};

}  // namespace ui::application
