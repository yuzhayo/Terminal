#pragma once

#include "ui/components/component.h"

namespace ui::components {

class ButtonComponent final : public Component {
public:
    using Component::Component;

    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Paint(HDC dc) override;
    bool PointerMove(POINT point) override;
    bool PointerDown(POINT point) override;
    bool PointerUp(POINT point) override;

private:
    config::VisualState State() const noexcept;

    bool hovered_ = false;
    bool pressed_ = false;
};

}  // namespace ui::components
