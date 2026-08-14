#include "rendering/render_runtime.h"

#include <tuple>

#include "rendering/window_render_context.h"
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

bool RenderRuntime::PaintRoundedStyleBox(HDC target, const RECT& bounds, int radius,
                                         int border_width, const RgbaColor& fill,
                                         const RgbaColor& border, COLORREF opaque_background,
                                         UINT dpi, unsigned int visual_state) {
    if (!target) return false;
    if (fill.alpha == 0 && (border.alpha == 0 || border_width <= 0)) return true;
    const COLORREF opaque_fill = CompositeOverOpaque(opaque_background, fill);
    const COLORREF opaque_border = CompositeOverOpaque(opaque_background, border);
    HBRUSH fill_brush = Brush(opaque_fill);
    HBRUSH border_brush = border_width > 0 ? Brush(opaque_border) : nullptr;
    HPEN fallback_pen = border_width > 0 ? Pen(opaque_border, border_width) : nullptr;
    if (!fill_brush) return false;
    return corner_tiles_.Paint(
        target, bounds,
        CornerTileKey{radius, border_width, opaque_fill, opaque_border, opaque_background, dpi,
                      visual_state},
        fill_brush, border_brush, fallback_pen);
}

NativePeerGdiResourceCache& RenderRuntime::native_peer_resources() noexcept {
    return native_peer_resources_;
}

const NativePeerGdiResourceCache& RenderRuntime::native_peer_resources() const noexcept {
    return native_peer_resources_;
}

void RenderRuntime::AdvanceResourceEpoch() {
    ++resource_epoch_;
    corner_tiles_.Clear();
    const auto contexts = window_contexts_;
    for (WindowRenderContext* context : contexts) {
        if (context) context->OnResourceEpochChanged(resource_epoch_);
    }
}

std::uint64_t RenderRuntime::resource_epoch() const noexcept {
    return resource_epoch_;
}

RenderRuntimeDiagnostics RenderRuntime::diagnostics() const noexcept {
    return {resource_epoch_,
            window_contexts_.size(),
            fonts_.size(),
            brushes_.size(),
            pens_.size(),
            corner_tiles_.entry_count(),
            native_peer_resources_.physical_font_count(),
            native_peer_resources_.physical_brush_count(),
            native_peer_resources_.active_font_lease_count(),
            native_peer_resources_.active_brush_lease_count()};
}

void RenderRuntime::RegisterWindowContext(WindowRenderContext* context) {
    if (context) window_contexts_.insert(context);
}

void RenderRuntime::UnregisterWindowContext(WindowRenderContext* context) noexcept {
    window_contexts_.erase(context);
}

void RenderRuntime::Reset() {
    corner_tiles_.Clear();
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

COLORREF CompositeOverOpaque(COLORREF background, const RgbaColor& foreground) noexcept {
    if (foreground.alpha == 255) return ToColorRef(foreground);
    if (foreground.alpha == 0) return background;
    const unsigned int alpha = foreground.alpha;
    const unsigned int inverse = 255u - alpha;
    const auto channel = [alpha, inverse](BYTE base, BYTE over) {
        return static_cast<BYTE>((static_cast<unsigned int>(over) * alpha +
                                  static_cast<unsigned int>(base) * inverse + 127u) /
                                 255u);
    };
    return RGB(channel(GetRValue(background), foreground.red),
               channel(GetGValue(background), foreground.green),
               channel(GetBValue(background), foreground.blue));
}

}  // namespace rendering
