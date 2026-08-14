#include "rendering/corner_tile_cache.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>

namespace rendering {
namespace {

std::uint32_t ToDibPixel(COLORREF color) noexcept {
    return 0xFF000000u | (static_cast<std::uint32_t>(GetRValue(color)) << 16u) |
           (static_cast<std::uint32_t>(GetGValue(color)) << 8u) |
           static_cast<std::uint32_t>(GetBValue(color));
}

COLORREF Blend(COLORREF base, COLORREF over, double weight) noexcept {
    const double clamped = std::clamp(weight, 0.0, 1.0);
    const auto channel = [clamped](BYTE bottom, BYTE top) {
        return static_cast<BYTE>(bottom + (top - bottom) * clamped + 0.5);
    };
    return RGB(channel(GetRValue(base), GetRValue(over)),
               channel(GetGValue(base), GetGValue(over)),
               channel(GetBValue(base), GetBValue(over)));
}

}  // namespace

bool CornerTileKey::operator<(const CornerTileKey& other) const noexcept {
    return std::tie(radius, thickness, fill, border, background, dpi, state) <
           std::tie(other.radius, other.thickness, other.fill, other.border, other.background,
                    other.dpi, other.state);
}

CornerTileCache::~CornerTileCache() {
    Clear();
    if (source_dc_) DeleteDC(source_dc_);
}

bool CornerTileCache::Paint(HDC target, const RECT& bounds, const CornerTileKey& requested,
                            HBRUSH fill_brush, HBRUSH border_brush,
                            HPEN fallback_pen) noexcept {
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (!target || !fill_brush || width <= 0 || height <= 0) return false;

    CornerTileKey key = requested;
    key.radius = std::clamp(key.radius, 0, std::min(width, height) / 2);
    key.thickness = std::clamp(key.thickness, 0, key.radius);
    if (key.radius == 0) {
        FillRect(target, &bounds, key.thickness > 0 && border_brush ? border_brush : fill_brush);
        if (key.thickness > 0) {
            RECT inner{bounds.left + key.thickness, bounds.top + key.thickness,
                       bounds.right - key.thickness, bounds.bottom - key.thickness};
            if (inner.right > inner.left && inner.bottom > inner.top) {
                FillRect(target, &inner, fill_brush);
            }
        }
        return true;
    }

    HBITMAP disc = DiscFor(key, false);
    HDC source = disc ? source_dc_ : nullptr;
    if (!source) {
        HGDIOBJ previous_pen = SelectObject(
            target, key.thickness > 0 && fallback_pen ? fallback_pen : GetStockObject(NULL_PEN));
        HGDIOBJ previous_brush = SelectObject(target, fill_brush);
        RoundRect(target, bounds.left, bounds.top, bounds.right, bounds.bottom,
                  key.radius * 2, key.radius * 2);
        SelectObject(target, previous_brush);
        SelectObject(target, previous_pen);
        return false;
    }

    HGDIOBJ previous_bitmap = SelectObject(source, disc);
    if (!previous_bitmap || previous_bitmap == HGDI_ERROR) return false;
    const int radius = key.radius;
    BitBlt(target, bounds.left, bounds.top, radius, radius, source, 0, 0, SRCCOPY);
    BitBlt(target, bounds.right - radius, bounds.top, radius, radius, source, radius, 0, SRCCOPY);
    BitBlt(target, bounds.left, bounds.bottom - radius, radius, radius, source, 0, radius, SRCCOPY);
    BitBlt(target, bounds.right - radius, bounds.bottom - radius, radius, radius, source, radius,
           radius, SRCCOPY);
    SelectObject(source, previous_bitmap);

    const LONG inner_left = bounds.left + radius;
    const LONG inner_right = bounds.right - radius;
    const LONG inner_top = bounds.top + radius;
    const LONG inner_bottom = bounds.bottom - radius;
    if (inner_right > inner_left) {
        if (key.thickness > 0 && border_brush) {
            RECT top{inner_left, bounds.top, inner_right, bounds.top + key.thickness};
            RECT bottom{inner_left, bounds.bottom - key.thickness, inner_right, bounds.bottom};
            FillRect(target, &top, border_brush);
            FillRect(target, &bottom, border_brush);
        }
        RECT top_fill{inner_left, bounds.top + key.thickness, inner_right, inner_top};
        RECT bottom_fill{inner_left, inner_bottom, inner_right, bounds.bottom - key.thickness};
        if (top_fill.bottom > top_fill.top) FillRect(target, &top_fill, fill_brush);
        if (bottom_fill.bottom > bottom_fill.top) FillRect(target, &bottom_fill, fill_brush);
    }
    if (inner_bottom > inner_top) {
        if (key.thickness > 0 && border_brush) {
            RECT left{bounds.left, inner_top, bounds.left + key.thickness, inner_bottom};
            RECT right{bounds.right - key.thickness, inner_top, bounds.right, inner_bottom};
            FillRect(target, &left, border_brush);
            FillRect(target, &right, border_brush);
        }
        RECT middle{bounds.left + key.thickness, inner_top,
                    bounds.right - key.thickness, inner_bottom};
        if (middle.right > middle.left) FillRect(target, &middle, fill_brush);
    }
    return true;
}

bool CornerTileCache::Prepare(const CornerTileKey& requested) noexcept {
    CornerTileKey key = requested;
    if (key.radius <= 0) return true;
    if (!SourceDc()) return false;
    return DiscFor(key, true) != nullptr;
}

void CornerTileCache::Clear() noexcept {
    for (const auto& [key, bitmap] : tiles_) {
        (void)key;
        DeleteObject(bitmap);
    }
    tiles_.clear();
}

std::size_t CornerTileCache::entry_count() const noexcept {
    return tiles_.size();
}

HBITMAP CornerTileCache::BuildDisc(const CornerTileKey& key) noexcept {
    const int size = key.radius * 2;
    if (size <= 0) return nullptr;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        return nullptr;
    }
    const double outer = key.radius;
    const double inner = outer - key.thickness;
    auto* pixels = static_cast<std::uint32_t*>(bits);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double dx = (x + 0.5) - outer;
            const double dy = (y + 0.5) - outer;
            const double distance = std::sqrt(dx * dx + dy * dy);
            const double shape = std::clamp(outer + 0.5 - distance, 0.0, 1.0);
            const double fill = std::clamp(inner + 0.5 - distance, 0.0, 1.0);
            COLORREF color = Blend(key.background, key.border, shape);
            color = Blend(color, key.fill, fill);
            pixels[static_cast<std::size_t>(y) * size + x] = ToDibPixel(color);
        }
    }
    return bitmap;
}

HBITMAP CornerTileCache::DiscFor(const CornerTileKey& key, bool allow_create) noexcept {
    if (const auto existing = tiles_.find(key); existing != tiles_.end()) return existing->second;
    if (!allow_create) return nullptr;
    if (tiles_.size() >= kMaximumEntries) Clear();
    HBITMAP bitmap = BuildDisc(key);
    if (bitmap) tiles_.emplace(key, bitmap);
    return bitmap;
}

HDC CornerTileCache::SourceDc() noexcept {
    if (!source_dc_) source_dc_ = CreateCompatibleDC(nullptr);
    return source_dc_;
}

}  // namespace rendering
