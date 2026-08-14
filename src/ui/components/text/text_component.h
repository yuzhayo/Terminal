#pragma once

#include "ui/components/component.h"

namespace ui::components {

class TextComponent final : public Component {
public:
    using Component::Component;

    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Paint(HDC dc) override;
};

}  // namespace ui::components
