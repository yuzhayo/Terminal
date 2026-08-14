#pragma once

#include <windows.h>

#include <cstdint>

namespace rendering {

class WindowRenderContext final {
public:
    WindowRenderContext() = default;
    ~WindowRenderContext();

    WindowRenderContext(const WindowRenderContext&) = delete;
    WindowRenderContext& operator=(const WindowRenderContext&) = delete;

    bool EnsureSize(HDC reference, int width, int height) noexcept;
    HDC dc() const noexcept;
    int width() const noexcept;
    int height() const noexcept;
    bool valid() const noexcept;
    std::uint64_t allocation_generation() const noexcept;

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
};

}  // namespace rendering
