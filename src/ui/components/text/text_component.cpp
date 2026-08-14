#include "ui/components/text/text_component.h"

#include <algorithm>

namespace ui::components {
namespace {

const config::TextProperties& Properties(const config::ResolvedComponent& definition) {
    return std::get<config::TextProperties>(definition.properties);
}

UINT TextFlags(const config::TextProperties& properties) noexcept {
    UINT flags = DT_NOPREFIX;
    flags |= properties.wrap ? DT_WORDBREAK : DT_SINGLELINE;
    if (properties.align == config::TextAlign::Center) flags |= DT_CENTER;
    if (properties.align == config::TextAlign::End) flags |= DT_RIGHT;
    return flags;
}

}  // namespace

MeasuredSize TextComponent::Measure(HDC dc, int available_width, int available_height) {
    (void)dc;
    const config::TextProperties& properties = Properties(definition_);
    const std::wstring text = ResolveText(properties.text);
    const SIZE measured = host_.render_runtime->MeasureText(
        text, style().font, host_.dpi, available_width, TextFlags(properties));
    return ApplyConstraints({measured.cx, measured.cy},
                            available_width, available_height);
}

void TextComponent::Paint(HDC dc) {
    const config::TextProperties& properties = Properties(definition_);
    const std::wstring text = ResolveText(properties.text);
    const rendering::RgbaColor foreground = host_.render_runtime->ResolveColor(
        style().states[static_cast<std::size_t>(enabled() ? config::VisualState::Normal
                                                         : config::VisualState::Disabled)].foreground);
    host_.render_runtime->DrawTextRun(dc, text, style().font, host_.dpi, bounds_,
                                      TextFlags(properties), rendering::ToColorRef(foreground));
}

}  // namespace ui::components
