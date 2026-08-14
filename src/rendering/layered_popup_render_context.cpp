#include "rendering/layered_popup_render_context.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "rendering/software_compositor.h"

namespace rendering {
namespace {

bool CreateSurface(int width, int height, HDC& dc, HBITMAP& bitmap, HGDIOBJ& previous,
                   std::uint32_t*& pixels) noexcept {
    HDC next_dc = CreateCompatibleDC(nullptr);
    if (!next_dc) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP next_bitmap = CreateDIBSection(next_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!next_bitmap || !bits) {
        if (next_bitmap) DeleteObject(next_bitmap);
        DeleteDC(next_dc);
        return false;
    }
    HGDIOBJ next_previous = SelectObject(next_dc, next_bitmap);
    if (!next_previous || next_previous == HGDI_ERROR) {
        DeleteObject(next_bitmap);
        DeleteDC(next_dc);
        return false;
    }
    dc = next_dc;
    bitmap = next_bitmap;
    previous = next_previous;
    pixels = static_cast<std::uint32_t*>(bits);
    return true;
}

void DestroySurface(HDC& dc, HBITMAP& bitmap, HGDIOBJ& previous,
                    std::uint32_t*& pixels) noexcept {
    if (dc && previous) SelectObject(dc, previous);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    dc = nullptr;
    bitmap = nullptr;
    previous = nullptr;
    pixels = nullptr;
}

}  // namespace

LayeredPopupRenderContext::LayeredPopupRenderContext(RenderRuntime& runtime)
    : runtime_(runtime), resource_epoch_(runtime.resource_epoch()) {
    runtime_.RegisterLayeredPopupContext(this);
}

LayeredPopupRenderContext::~LayeredPopupRenderContext() {
    runtime_.UnregisterLayeredPopupContext(this);
    Reset();
}

bool LayeredPopupRenderContext::EnsureSize(int width, int height) noexcept {
    if (width <= 0 || height <= 0) return false;
    if (valid() && width_ == width && height_ == height) return true;
    HDC next_dc = nullptr;
    HBITMAP next_bitmap = nullptr;
    HGDIOBJ next_previous = nullptr;
    std::uint32_t* next_pixels = nullptr;
    if (!CreateSurface(width, height, next_dc, next_bitmap, next_previous, next_pixels)) return false;
    Reset();
    memory_dc_ = next_dc;
    bitmap_ = next_bitmap;
    previous_bitmap_ = next_previous;
    pixels_ = next_pixels;
    width_ = width;
    height_ = height;
    return true;
}

bool LayeredPopupRenderContext::EnsureMaskSurface() noexcept {
    if (mask_dc_ && mask_pixels_) return true;
    return CreateSurface(width_, height_, mask_dc_, mask_bitmap_, previous_mask_bitmap_, mask_pixels_);
}

void LayeredPopupRenderContext::Clear() noexcept {
    if (valid()) std::memset(pixels_, 0, static_cast<std::size_t>(width_) * height_ * sizeof(*pixels_));
}

void LayeredPopupRenderContext::SourceOver(const RECT& region, const RgbaColor& color) noexcept {
    if (valid()) SourceOverSolid(pixels_, width_, height_, width_, region, color);
}

void LayeredPopupRenderContext::SourceOverRounded(const RECT& region, int radius, int border_width,
                                                   const RgbaColor& fill,
                                                   const RgbaColor& border) noexcept {
    if (!valid()) return;
    const RECT clipped{std::max(0L, region.left), std::max(0L, region.top),
                       std::min(static_cast<LONG>(width_), region.right),
                       std::min(static_cast<LONG>(height_), region.bottom)};
    const double width = static_cast<double>(region.right - region.left);
    const double height = static_cast<double>(region.bottom - region.top);
    if (clipped.right <= clipped.left || clipped.bottom <= clipped.top || width <= 0 || height <= 0) return;
    const double outer_radius = std::clamp(static_cast<double>(radius), 0.0, std::min(width, height) / 2.0);
    const double inset = std::clamp(static_cast<double>(border_width), 0.0, outer_radius);
    const double center_x = (region.left + region.right) / 2.0;
    const double center_y = (region.top + region.bottom) / 2.0;
    const auto coverage = [](double x, double y, double half_width, double half_height,
                             double shape_radius) {
        const double qx = std::abs(x) - (half_width - shape_radius);
        const double qy = std::abs(y) - (half_height - shape_radius);
        const double outside = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0));
        const double inside = std::min(std::max(qx, qy), 0.0);
        return std::clamp(0.5 - (outside + inside - shape_radius), 0.0, 1.0);
    };
    for (LONG y = clipped.top; y < clipped.bottom; ++y) {
        std::uint32_t* row = pixels_ + static_cast<std::size_t>(y) * width_;
        for (LONG x = clipped.left; x < clipped.right; ++x) {
            const double local_x = x + 0.5 - center_x;
            const double local_y = y + 0.5 - center_y;
            const double outer = coverage(local_x, local_y, width / 2.0, height / 2.0, outer_radius);
            if (outer <= 0.0) continue;
            double inner = 0.0;
            if (inset > 0.0) {
                inner = coverage(local_x, local_y, width / 2.0 - inset, height / 2.0 - inset,
                                 std::max(0.0, outer_radius - inset));
                RgbaColor edge = border;
                edge.alpha = static_cast<BYTE>(edge.alpha * outer * (1.0 - inner) + 0.5);
                row[x] = SourceOverPremultiplied(row[x], Premultiply(edge));
            }
            RgbaColor inner_fill = fill;
            inner_fill.alpha = static_cast<BYTE>(inner_fill.alpha * (inset > 0.0 ? inner : outer) + 0.5);
            row[x] = SourceOverPremultiplied(row[x], Premultiply(inner_fill));
        }
    }
}

bool LayeredPopupRenderContext::DrawTextMask(std::wstring_view text,
                                              const ui::config::ResolvedFont& font, UINT dpi,
                                              const RECT& bounds, UINT flags,
                                              const RgbaColor& color) {
    if (!valid() || text.empty() || !EnsureMaskSurface()) return false;
    std::memset(mask_pixels_, 0, static_cast<std::size_t>(width_) * height_ * sizeof(*mask_pixels_));
    HFONT native_font = runtime_.Font(font, dpi);
    if (!native_font) return false;
    const HGDIOBJ previous_font = SelectObject(mask_dc_, native_font);
    SetBkMode(mask_dc_, OPAQUE);
    SetBkColor(mask_dc_, RGB(0, 0, 0));
    SetTextColor(mask_dc_, RGB(255, 255, 255));
    RECT target = bounds;
    const bool drawn = DrawTextW(mask_dc_, text.data(), static_cast<int>(text.size()), &target, flags) != 0;
    if (previous_font && previous_font != HGDI_ERROR) SelectObject(mask_dc_, previous_font);
    if (!drawn) return false;
    const RECT clipped{std::max(0L, bounds.left), std::max(0L, bounds.top),
                       std::min(static_cast<LONG>(width_), bounds.right),
                       std::min(static_cast<LONG>(height_), bounds.bottom)};
    for (LONG y = clipped.top; y < clipped.bottom; ++y) {
        for (LONG x = clipped.left; x < clipped.right; ++x) {
            const std::uint32_t mask = mask_pixels_[static_cast<std::size_t>(y) * width_ + x];
            const BYTE coverage = static_cast<BYTE>(std::max({mask & 0xFFu, (mask >> 8) & 0xFFu,
                                                              (mask >> 16) & 0xFFu}));
            if (!coverage) continue;
            RgbaColor glyph = color;
            glyph.alpha = static_cast<BYTE>((static_cast<unsigned int>(glyph.alpha) * coverage + 127u) / 255u);
            auto& destination = pixels_[static_cast<std::size_t>(y) * width_ + x];
            destination = SourceOverPremultiplied(destination, Premultiply(glyph));
        }
    }
    return true;
}

bool LayeredPopupRenderContext::Present(HWND popup, POINT screen_origin) const noexcept {
    if (!valid() || !popup) return false;
    SIZE size{width_, height_};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    return UpdateLayeredWindow(popup, nullptr, &screen_origin, &size, memory_dc_, &source, 0,
                               &blend, ULW_ALPHA) != FALSE;
}

void LayeredPopupRenderContext::OnResourceEpochChanged(std::uint64_t epoch) {
    if (epoch <= resource_epoch_) return;
    resource_epoch_ = epoch;
    if (redraw_request_) redraw_request_();
}

void LayeredPopupRenderContext::SetRedrawRequest(std::function<void()> request) {
    redraw_request_ = std::move(request);
}

std::uint32_t LayeredPopupRenderContext::PixelAt(int x, int y) const noexcept {
    return valid() && x >= 0 && y >= 0 && x < width_ && y < height_
               ? pixels_[static_cast<std::size_t>(y) * width_ + x]
               : 0;
}
int LayeredPopupRenderContext::width() const noexcept { return width_; }
int LayeredPopupRenderContext::height() const noexcept { return height_; }
bool LayeredPopupRenderContext::valid() const noexcept { return memory_dc_ && bitmap_ && pixels_; }

void LayeredPopupRenderContext::Reset() noexcept {
    DestroySurface(mask_dc_, mask_bitmap_, previous_mask_bitmap_, mask_pixels_);
    DestroySurface(memory_dc_, bitmap_, previous_bitmap_, pixels_);
    width_ = 0;
    height_ = 0;
}

}  // namespace rendering
