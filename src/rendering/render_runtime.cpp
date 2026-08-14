#include "rendering/render_runtime.h"

#include <usp10.h>

#include <algorithm>
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

    if (in_paint_scope_) return nullptr;
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
    if (in_paint_scope_) return nullptr;
    HBRUSH brush = CreateSolidBrush(color);
    if (brush) brushes_.emplace(color, brush);
    return brush;
}

HPEN RenderRuntime::Pen(COLORREF color, int width) {
    PenKey key{color, width};
    if (const auto existing = pens_.find(key); existing != pens_.end()) return existing->second;
    if (in_paint_scope_) return nullptr;
    HPEN pen = CreatePen(PS_SOLID, width, color);
    if (pen) pens_.emplace(key, pen);
    return pen;
}

bool RenderRuntime::PrepareStyleResources(const ui::config::ResolvedStyle& style, UINT dpi,
                                          COLORREF opaque_background) {
    if (!Font(style.font, dpi)) return false;
    for (std::size_t index = 0; index < style.states.size(); ++index) {
        const auto& visual = style.states[index];
        const RgbaColor fill = ResolveColor(visual.background);
        const RgbaColor border = ResolveColor(visual.border);
        const COLORREF opaque_fill = CompositeOverOpaque(opaque_background, fill);
        const COLORREF opaque_border = CompositeOverOpaque(opaque_background, border);
        if (!Brush(opaque_fill)) return false;
        const int border_width = MulDiv(style.border_width, static_cast<int>(dpi), 96);
        if (border_width > 0 && (!Brush(opaque_border) || !Pen(opaque_border, border_width))) {
            return false;
        }
        const int radius = MulDiv(style.radius, static_cast<int>(dpi), 96);
        if (radius > 0 && !corner_tiles_.Prepare(
                CornerTileKey{radius, border_width,
                              opaque_fill, opaque_border, opaque_background, dpi,
                              static_cast<unsigned int>(index)})) {
            return false;
        }
    }
    return true;
}

HDC RenderRuntime::MeasurementDc() {
    if (!measurement_dc_ && !in_paint_scope_) measurement_dc_ = CreateCompatibleDC(nullptr);
    return measurement_dc_;
}

namespace {

bool NeedsComplexShaping(std::wstring_view text) noexcept {
    for (const wchar_t ch : text) {
        if ((ch >= 0x0590 && ch <= 0x08FF) || (ch >= 0x0900 && ch <= 0x0DFF) ||
            (ch >= 0x200C && ch <= 0x200F) || (ch >= 0xFB1D && ch <= 0xFEFC)) return true;
    }
    return false;
}

}  // namespace

SIZE RenderRuntime::MeasureText(std::wstring_view text, const ui::config::ResolvedFont& descriptor,
                                UINT dpi, int available_width, UINT flags) {
    SIZE result{};
    HDC dc = MeasurementDc();
    HFONT font = Font(descriptor, dpi);
    if (!dc || !font) return result;
    HGDIOBJ previous = SelectObject(dc, font);
    if (NeedsComplexShaping(text) && (flags & DT_SINGLELINE) != 0) {
        SCRIPT_STRING_ANALYSIS analysis = nullptr;
        if (SUCCEEDED(ScriptStringAnalyse(dc, text.data(), static_cast<int>(text.size()),
                                          static_cast<int>(text.size() * 3 / 2 + 16), -1,
                                          SSA_GLYPHS | SSA_FALLBACK, 0, nullptr, nullptr,
                                          nullptr, nullptr, nullptr, &analysis))) {
            if (const SIZE* size = ScriptString_pSize(analysis)) result = *size;
            ScriptStringFree(&analysis);
        }
    } else {
        RECT measured{0, 0, std::max(1, available_width), 32767};
        ::DrawTextW(dc, text.data(), static_cast<int>(text.size()), &measured, flags | DT_CALCRECT);
        result = {measured.right - measured.left, measured.bottom - measured.top};
    }
    if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
    return result;
}

bool RenderRuntime::DrawTextRun(HDC target, std::wstring_view text,
                                const ui::config::ResolvedFont& descriptor, UINT dpi,
                                const RECT& bounds, UINT flags, COLORREF color) {
    if (!target) return false;
    HFONT font = Font(descriptor, dpi);
    if (!font) return false;
    HGDIOBJ previous = SelectObject(target, font);
    SetBkMode(target, TRANSPARENT);
    SetTextColor(target, color);
    bool success = false;
    if (NeedsComplexShaping(text) && (flags & DT_SINGLELINE) != 0) {
        SCRIPT_STRING_ANALYSIS analysis = nullptr;
        if (SUCCEEDED(ScriptStringAnalyse(target, text.data(), static_cast<int>(text.size()),
                                          static_cast<int>(text.size() * 3 / 2 + 16), -1,
                                          SSA_GLYPHS | SSA_FALLBACK, 0, nullptr, nullptr,
                                          nullptr, nullptr, nullptr, &analysis))) {
            const SIZE size = ScriptString_pSize(analysis) ? *ScriptString_pSize(analysis) : SIZE{};
            int x = bounds.left;
            if ((flags & DT_CENTER) != 0) x += ((bounds.right - bounds.left) - size.cx) / 2;
            else if ((flags & DT_RIGHT) != 0) x = bounds.right - size.cx;
            int y = bounds.top;
            if ((flags & DT_VCENTER) != 0) y += ((bounds.bottom - bounds.top) - size.cy) / 2;
            success = SUCCEEDED(ScriptStringOut(analysis, x, y, 0, &bounds, 0, 0, FALSE));
            ScriptStringFree(&analysis);
        }
    } else {
        RECT target_bounds = bounds;
        success = ::DrawTextW(target, text.data(), static_cast<int>(text.size()), &target_bounds,
                              flags) != 0;
    }
    if (previous && previous != HGDI_ERROR) SelectObject(target, previous);
    return success;
}

void RenderRuntime::BeginPaintScope() noexcept { in_paint_scope_ = true; }
void RenderRuntime::EndPaintScope() noexcept { in_paint_scope_ = false; }

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
    const CornerTileKey key{radius, border_width, opaque_fill, opaque_border, opaque_background, dpi,
                            visual_state};
    if (!in_paint_scope_ && radius > 0) corner_tiles_.Prepare(key);
    return corner_tiles_.Paint(
        target, bounds, key,
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
    if (measurement_dc_) DeleteDC(measurement_dc_);
    measurement_dc_ = nullptr;
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
