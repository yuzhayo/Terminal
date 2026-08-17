#pragma once

#include "ui/components/component.h"

namespace ui::components {

class TabsComponent final : public Component {
public:
    using Component::Component;

    int PreferredWidth(HDC dc);
    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Arrange(const RECT& bounds) override;
    void Paint(HDC dc) override;
    bool PointerMove(POINT point) override;
    bool PointerDown(POINT point) override;
    bool PointerUp(POINT point) override;
    AutomationRole automation_role() const noexcept override;

private:
    struct Item {
        std::string route_id;
        std::wstring label;
        RECT bounds{};
        int natural_width = 0;
    };

    void RefreshItems(HDC dc);
    std::optional<std::size_t> ItemAt(POINT point) const noexcept;
    config::VisualState ItemState(std::size_t index) const noexcept;

    std::vector<Item> items_;
    std::optional<std::size_t> hovered_;
    std::optional<std::size_t> pressed_;
};

}  // namespace ui::components
