#include "rendering/window_render_context.h"

#include <algorithm>
#include <cstddef>

namespace rendering {

WindowRenderContext::~WindowRenderContext() {
    Reset();
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

void WindowRenderContext::ForceOpaqueAlpha() noexcept {
    if (!valid()) return;
    const std::size_t count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    for (std::size_t index = 0; index < count; ++index) pixels_[index] |= 0xFF000000u;
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
}

}  // namespace rendering
