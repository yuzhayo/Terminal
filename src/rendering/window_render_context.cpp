#include "rendering/window_render_context.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

#include "rendering/render_runtime.h"
#include "rendering/software_compositor.h"

namespace rendering {

WindowRenderContext::WindowRenderContext(RenderRuntime* runtime) : runtime_(runtime) {
    if (runtime_) {
        resource_epoch_ = runtime_->resource_epoch();
        runtime_->RegisterWindowContext(this);
    }
}

WindowRenderContext::~WindowRenderContext() {
    Reset();
    if (runtime_) runtime_->UnregisterWindowContext(this);
}

bool WindowRenderContext::EnsureSize(HDC reference, int width, int height) noexcept {
    if (width <= 0 || height <= 0) return false;
    if (valid() && width_ == width && height_ == height) return true;

    HDC candidate_dc = CreateCompatibleDC(reference);
    if (!candidate_dc) return false;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* candidate_pixels = nullptr;
    HBITMAP candidate_bitmap =
        CreateDIBSection(reference, &info, DIB_RGB_COLORS, &candidate_pixels, nullptr, 0);
    if (!candidate_bitmap || !candidate_pixels) {
        if (candidate_bitmap) DeleteObject(candidate_bitmap);
        DeleteDC(candidate_dc);
        return false;
    }

    HGDIOBJ candidate_previous = SelectObject(candidate_dc, candidate_bitmap);
    if (!candidate_previous || candidate_previous == HGDI_ERROR) {
        DeleteObject(candidate_bitmap);
        DeleteDC(candidate_dc);
        return false;
    }

    Reset();
    memory_dc_ = candidate_dc;
    bitmap_ = candidate_bitmap;
    previous_bitmap_ = candidate_previous;
    pixels_ = static_cast<std::uint32_t*>(candidate_pixels);
    width_ = width;
    height_ = height;
    ++allocation_generation_;
    InvalidateAll();
    return true;
}

HDC WindowRenderContext::dc() const noexcept {
    return memory_dc_;
}

int WindowRenderContext::width() const noexcept {
    return width_;
}

int WindowRenderContext::height() const noexcept {
    return height_;
}

bool WindowRenderContext::valid() const noexcept {
    return memory_dc_ && bitmap_ && pixels_ && width_ > 0 && height_ > 0;
}

std::uint64_t WindowRenderContext::allocation_generation() const noexcept {
    return allocation_generation_;
}

std::uint64_t WindowRenderContext::resource_epoch() const noexcept {
    return resource_epoch_;
}

void WindowRenderContext::Invalidate(const RECT& region) noexcept {
    if (!valid()) return;
    const RECT clipped{std::max(0L, region.left), std::max(0L, region.top),
                       std::min(static_cast<LONG>(width_), region.right),
                       std::min(static_cast<LONG>(height_), region.bottom)};
    if (clipped.right <= clipped.left || clipped.bottom <= clipped.top) return;
    if (!has_invalidation_) {
        invalidation_ = clipped;
        has_invalidation_ = true;
        return;
    }
    invalidation_.left = std::min(invalidation_.left, clipped.left);
    invalidation_.top = std::min(invalidation_.top, clipped.top);
    invalidation_.right = std::max(invalidation_.right, clipped.right);
    invalidation_.bottom = std::max(invalidation_.bottom, clipped.bottom);
}

void WindowRenderContext::InvalidateAll() noexcept {
    if (!valid()) return;
    invalidation_ = {0, 0, width_, height_};
    has_invalidation_ = true;
}

bool WindowRenderContext::TakeInvalidation(RECT& region) noexcept {
    if (!has_invalidation_) return false;
    region = invalidation_;
    invalidation_ = {};
    has_invalidation_ = false;
    return true;
}

bool WindowRenderContext::has_invalidation() const noexcept {
    return has_invalidation_;
}

void WindowRenderContext::OnResourceEpochChanged(std::uint64_t epoch) {
    if (epoch <= resource_epoch_) return;
    resource_epoch_ = epoch;
    InvalidateAll();
    if (redraw_request_) redraw_request_();
}

void WindowRenderContext::SetRedrawRequest(std::function<void()> request) {
    redraw_request_ = std::move(request);
}

void WindowRenderContext::SourceOver(const RECT& region, const RgbaColor& color) noexcept {
    if (!valid()) return;
    SourceOverSolid(pixels_, width_, height_, width_, region, color);
}

void WindowRenderContext::SourceOverRounded(const RECT& region, int radius, int border_width,
                                            const RgbaColor& fill,
                                            const RgbaColor& border) noexcept {
    if (!valid()) return;
    const RECT clipped{std::max(0L, region.left), std::max(0L, region.top),
                       std::min(static_cast<LONG>(width_), region.right),
                       std::min(static_cast<LONG>(height_), region.bottom)};
    if (clipped.right <= clipped.left || clipped.bottom <= clipped.top) return;
    const double width = static_cast<double>(region.right - region.left);
    const double height = static_cast<double>(region.bottom - region.top);
    const double outer_radius = std::clamp(static_cast<double>(radius), 0.0,
                                           std::min(width, height) / 2.0);
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
            const double outer = coverage(local_x, local_y, width / 2.0, height / 2.0,
                                          outer_radius);
            if (outer <= 0.0) continue;
            double inner = 0.0;
            if (inset > 0.0) {
                inner = coverage(local_x, local_y, width / 2.0 - inset,
                                 height / 2.0 - inset,
                                 std::max(0.0, outer_radius - inset));
                const double border_coverage = outer * (1.0 - inner);
                if (border_coverage > 0.0) {
                    RgbaColor edge = border;
                    edge.alpha = static_cast<BYTE>(edge.alpha * border_coverage + 0.5);
                    row[x] = SourceOverPremultiplied(row[x], Premultiply(edge));
                }
            }
            const double fill_coverage = inset > 0.0 ? inner : outer;
            if (fill_coverage > 0.0) {
                RgbaColor inner_fill = fill;
                inner_fill.alpha = static_cast<BYTE>(inner_fill.alpha * fill_coverage + 0.5);
                row[x] = SourceOverPremultiplied(row[x], Premultiply(inner_fill));
            }
        }
    }
}

std::uint32_t WindowRenderContext::PixelAt(int x, int y) const noexcept {
    if (!valid() || x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
    return pixels_[static_cast<std::size_t>(y) * width_ + x];
}

void WindowRenderContext::ForceOpaqueAlpha(const RECT& region) noexcept {
    if (!valid()) return;
    const RECT clipped{std::max(0L, region.left), std::max(0L, region.top),
                       std::min(static_cast<LONG>(width_), region.right),
                       std::min(static_cast<LONG>(height_), region.bottom)};
    for (LONG y = clipped.top; y < clipped.bottom; ++y) {
        std::uint32_t* row = pixels_ + static_cast<std::size_t>(y) * width_;
        for (LONG x = clipped.left; x < clipped.right; ++x) row[x] |= 0xFF000000u;
    }
}

void WindowRenderContext::ForceOpaqueAlpha() noexcept {
    ForceOpaqueAlpha(RECT{0, 0, width_, height_});
}

bool WindowRenderContext::Present(HDC target, const RECT& region) const noexcept {
    if (!valid() || !target) return false;
    RECT clipped{std::max(0L, region.left), std::max(0L, region.top),
                 std::min(static_cast<LONG>(width_), region.right),
                 std::min(static_cast<LONG>(height_), region.bottom)};
    if (clipped.right <= clipped.left || clipped.bottom <= clipped.top) return true;
    return BitBlt(target, clipped.left, clipped.top, clipped.right - clipped.left,
                  clipped.bottom - clipped.top, memory_dc_, clipped.left, clipped.top, SRCCOPY) != FALSE;
}

bool WindowRenderContext::PresentScaled(HDC target, const RECT& target_region) const noexcept {
    if (!valid() || !target || target_region.right <= target_region.left ||
        target_region.bottom <= target_region.top) return false;
    const int previous_mode = SetStretchBltMode(target, COLORONCOLOR);
    const bool presented = StretchBlt(target, target_region.left, target_region.top,
                                      target_region.right - target_region.left,
                                      target_region.bottom - target_region.top,
                                      memory_dc_, 0, 0, width_, height_, SRCCOPY) != FALSE;
    if (previous_mode != 0) SetStretchBltMode(target, previous_mode);
    return presented;
}

void WindowRenderContext::Reset() noexcept {
    if (memory_dc_ && previous_bitmap_) SelectObject(memory_dc_, previous_bitmap_);
    if (bitmap_) DeleteObject(bitmap_);
    if (memory_dc_) DeleteDC(memory_dc_);
    memory_dc_ = nullptr;
    bitmap_ = nullptr;
    previous_bitmap_ = nullptr;
    pixels_ = nullptr;
    width_ = 0;
    height_ = 0;
    invalidation_ = {};
    has_invalidation_ = false;
}

}  // namespace rendering
