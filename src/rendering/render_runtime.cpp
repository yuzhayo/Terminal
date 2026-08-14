#include "rendering/render_runtime.h"

#include <tuple>

#include "ui/theme/theme_platform_adapter.h"

namespace rendering {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"Segoe UI";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}

}  // namespace

RenderRuntime::~RenderRuntime() {
    Reset();
}

bool RenderRuntime::FontKey::operator<(const FontKey& other) const noexcept {
    return std::tie(family, point_size, weight, dpi) <
           std::tie(other.family, other.point_size, other.weight, other.dpi);
}

bool RenderRuntime::PenKey::operator<(const PenKey& other) const noexcept {
    return std::tie(color, width) < std::tie(other.color, other.width);
}

RgbaColor RenderRuntime::ResolveColor(const ui::config::ResolvedColor& color) const noexcept {
    if (const auto* literal = std::get_if<ui::config::LiteralRgba>(&color)) {
        return {literal->red, literal->green, literal->blue, literal->alpha};
    }
    const COLORREF system = ui::theme::ThemePlatformAdapter::MaterializeSystemColor(
        std::get<ui::config::SystemColorSlot>(color));
    return {GetRValue(system), GetGValue(system), GetBValue(system), 255};
}

HFONT RenderRuntime::Font(const ui::config::ResolvedFont& descriptor, UINT dpi) {
    FontKey key{descriptor.family, descriptor.point_size, descriptor.weight, dpi};
    if (const auto existing = fonts_.find(key); existing != fonts_.end()) return existing->second;

    const int height = -MulDiv(descriptor.point_size, static_cast<int>(dpi), 72);
    const std::wstring family = Utf8ToWide(descriptor.family);
    HFONT font = CreateFontW(height, 0, 0, 0, descriptor.weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family.c_str());
    if (!font && !descriptor.fallback_family.empty()) {
        const std::wstring fallback = Utf8ToWide(descriptor.fallback_family);
        font = CreateFontW(height, 0, 0, 0, descriptor.weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fallback.c_str());
    }
    if (font) fonts_.emplace(std::move(key), font);
    return font;
}

HBRUSH RenderRuntime::Brush(COLORREF color) {
    if (const auto existing = brushes_.find(color); existing != brushes_.end()) return existing->second;
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) brushes_.emplace(color, brush);
    return brush;
}

HPEN RenderRuntime::Pen(COLORREF color, int width) {
    PenKey key{color, width};
    if (const auto existing = pens_.find(key); existing != pens_.end()) return existing->second;
    HPEN pen = CreatePen(PS_SOLID, width, color);
    if (pen) pens_.emplace(key, pen);
    return pen;
}

void RenderRuntime::Reset() {
    for (const auto& [key, font] : fonts_) {
        (void)key;
        DeleteObject(font);
    }
    for (const auto& [key, brush] : brushes_) {
        (void)key;
        DeleteObject(brush);
    }
    for (const auto& [key, pen] : pens_) {
        (void)key;
        DeleteObject(pen);
    }
    fonts_.clear();
    brushes_.clear();
    pens_.clear();
}

COLORREF ToColorRef(const RgbaColor& color) noexcept {
    return RGB(color.red, color.green, color.blue);
}

}  // namespace rendering
