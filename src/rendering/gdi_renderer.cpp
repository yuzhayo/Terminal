#include "rendering/gdi_renderer.h"

namespace rendering {

GdiRenderer::~GdiRenderer() {
    Reset();
}

bool GdiRenderer::Ensure(HDC reference, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (memory_dc_ && width_ == width && height_ == height) {
        return true;
    }

    HDC candidate_dc = CreateCompatibleDC(reference);
    if (!candidate_dc) {
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP candidate_bitmap =
        CreateDIBSection(reference, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!candidate_bitmap || !pixels) {
        if (candidate_bitmap) {
            DeleteObject(candidate_bitmap);
        }
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
    width_ = width;
    height_ = height;
    return true;
}

bool GdiRenderer::Paint(HDC target, int width, int height, const RECT& invalid_region) {
    if (!Ensure(target, width, height)) {
        return false;
    }

    RECT bounds{0, 0, width_, height_};
    FillRect(memory_dc_, &bounds, GetSysColorBrush(COLOR_WINDOW));

    const int paint_width = invalid_region.right - invalid_region.left;
    const int paint_height = invalid_region.bottom - invalid_region.top;
    return BitBlt(target, invalid_region.left, invalid_region.top, paint_width, paint_height, memory_dc_,
                  invalid_region.left, invalid_region.top, SRCCOPY) != FALSE;
}

void GdiRenderer::Reset() {
    if (memory_dc_ && previous_bitmap_) {
        SelectObject(memory_dc_, previous_bitmap_);
    }
    if (bitmap_) {
        DeleteObject(bitmap_);
    }
    if (memory_dc_) {
        DeleteDC(memory_dc_);
    }

    memory_dc_ = nullptr;
    bitmap_ = nullptr;
    previous_bitmap_ = nullptr;
    width_ = 0;
    height_ = 0;
}

}  // namespace rendering
