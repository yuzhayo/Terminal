#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string_view>

#include "rendering/render_runtime.h"

namespace rendering {

class LayeredPopupRenderContext final {
public:
    explicit LayeredPopupRenderContext(RenderRuntime& runtime);
    ~LayeredPopupRenderContext();

    LayeredPopupRenderContext(const LayeredPopupRenderContext&) = delete;
    LayeredPopupRenderContext& operator=(const LayeredPopupRenderContext&) = delete;

    bool EnsureSize(int width, int height) noexcept;
    void Clear() noexcept;
    void SourceOver(const RECT& region, const RgbaColor& color) noexcept;
    void SourceOverRounded(const RECT& region, int radius, int border_width,
                           const RgbaColor& fill, const RgbaColor& border) noexcept;
    bool DrawTextMask(std::wstring_view text, const ui::config::ResolvedFont& font, UINT dpi,
                      const RECT& bounds, UINT flags, const RgbaColor& color);
    bool Present(HWND popup, POINT screen_origin) const noexcept;
    void OnResourceEpochChanged(std::uint64_t epoch);
    void SetRedrawRequest(std::function<void()> request);
    std::uint32_t PixelAt(int x, int y) const noexcept;
    int width() const noexcept;
    int height() const noexcept;
    bool valid() const noexcept;
    void Reset() noexcept;

private:
    bool EnsureMaskSurface() noexcept;

    RenderRuntime& runtime_;
    HDC memory_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_bitmap_ = nullptr;
    std::uint32_t* pixels_ = nullptr;
    HDC mask_dc_ = nullptr;
    HBITMAP mask_bitmap_ = nullptr;
    HGDIOBJ previous_mask_bitmap_ = nullptr;
    std::uint32_t* mask_pixels_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::uint64_t resource_epoch_ = 1;
    std::function<void()> redraw_request_;
};

}  // namespace rendering
