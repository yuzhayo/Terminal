#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>

namespace rendering {

class RenderRuntime;
struct RgbaColor;

class WindowRenderContext final {
public:
    explicit WindowRenderContext(RenderRuntime* runtime = nullptr);
    ~WindowRenderContext();

    WindowRenderContext(const WindowRenderContext&) = delete;
    WindowRenderContext& operator=(const WindowRenderContext&) = delete;

    bool EnsureSize(HDC reference, int width, int height) noexcept;
    HDC dc() const noexcept;
    int width() const noexcept;
    int height() const noexcept;
    bool valid() const noexcept;
    std::uint64_t allocation_generation() const noexcept;
    std::uint64_t resource_epoch() const noexcept;

    void Invalidate(const RECT& region) noexcept;
    void InvalidateAll() noexcept;
    bool TakeInvalidation(RECT& region) noexcept;
    bool has_invalidation() const noexcept;
    void OnResourceEpochChanged(std::uint64_t epoch);
    void SetRedrawRequest(std::function<void()> request);

    void SourceOver(const RECT& region, const RgbaColor& color) noexcept;
    std::uint32_t PixelAt(int x, int y) const noexcept;
    void ForceOpaqueAlpha(const RECT& region) noexcept;
    void ForceOpaqueAlpha() noexcept;
    bool Present(HDC target, const RECT& region) const noexcept;
    void Reset() noexcept;

private:
    HDC memory_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_bitmap_ = nullptr;
    std::uint32_t* pixels_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::uint64_t allocation_generation_ = 0;
    std::uint64_t resource_epoch_ = 1;
    RECT invalidation_{};
    bool has_invalidation_ = false;
    RenderRuntime* runtime_ = nullptr;
    std::function<void()> redraw_request_;
};

}  // namespace rendering
