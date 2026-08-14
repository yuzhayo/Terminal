#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "ui/application/ui_application_bridge.h"

namespace ui::application {

class UiActionRegistry final {
public:
    using Handler = std::function<std::optional<UiPatch>(const UiEvent&)>;

    bool Register(std::string action, Handler handler);
    bool Contains(std::string_view action) const noexcept;
    std::optional<UiPatch> Dispatch(const UiEvent& event) const;
    std::size_t size() const noexcept;

private:
    std::map<std::string, Handler, std::less<>> handlers_;
};

}  // namespace ui::application
