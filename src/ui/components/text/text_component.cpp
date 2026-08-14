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
    const config::TextProperties& properties = Properties(definition_);
    const std::wstring text = ResolveText(properties.text);
    HFONT font = host_.render_runtime->Font(style().font, host_.dpi);
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    RECT measured{0, 0, std::max(1, available_width), std::max(1, available_height)};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measured,
              TextFlags(properties) | DT_CALCRECT);
    if (previous) SelectObject(dc, previous);
    return ApplyConstraints({measured.right - measured.left, measured.bottom - measured.top},
                            available_width, available_height);
}

void TextComponent::Paint(HDC dc) {
    const config::TextProperties& properties = Properties(definition_);
    const std::wstring text = ResolveText(properties.text);
    const rendering::RgbaColor foreground = host_.render_runtime->ResolveColor(
        style().states[static_cast<std::size_t>(enabled() ? config::VisualState::Normal
                                                         : config::VisualState::Disabled)].foreground);
    HFONT font = host_.render_runtime->Font(style().font, host_.dpi);
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, rendering::ToColorRef(foreground));
    RECT text_bounds = bounds_;
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &text_bounds, TextFlags(properties));
    if (previous) SelectObject(dc, previous);
}

}  // namespace ui::components
