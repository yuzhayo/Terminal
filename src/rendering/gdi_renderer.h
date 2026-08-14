#pragma once

#include <windows.h>

namespace rendering {

class GdiRenderer final {
public:
    GdiRenderer() = default;
    ~GdiRenderer();

    GdiRenderer(const GdiRenderer&) = delete;
    GdiRenderer& operator=(const GdiRenderer&) = delete;

    bool Paint(HDC target, int width, int height, const RECT& invalid_region);
    void Reset();

private:
    bool Ensure(HDC reference, int width, int height);

    HDC memory_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_bitmap_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace rendering
