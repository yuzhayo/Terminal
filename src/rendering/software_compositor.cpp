#include "rendering/software_compositor.h"

#include <algorithm>

#include "rendering/render_runtime.h"

namespace rendering {
namespace {

constexpr std::uint32_t Channel(std::uint32_t pixel, int shift) noexcept {
    return (pixel >> shift) & 0xFFu;
}

constexpr std::uint32_t DivideBy255(std::uint32_t value) noexcept {
    return (value + 128u + ((value + 128u) >> 8u)) >> 8u;
}

}  // namespace

std::uint32_t Premultiply(const RgbaColor& color) noexcept {
    const std::uint32_t alpha = color.alpha;
    const std::uint32_t red = DivideBy255(static_cast<std::uint32_t>(color.red) * alpha);
    const std::uint32_t green = DivideBy255(static_cast<std::uint32_t>(color.green) * alpha);
    const std::uint32_t blue = DivideBy255(static_cast<std::uint32_t>(color.blue) * alpha);
    return (alpha << 24u) | (red << 16u) | (green << 8u) | blue;
}

std::uint32_t SourceOverPremultiplied(std::uint32_t destination,
                                      std::uint32_t source) noexcept {
    const std::uint32_t inverse_alpha = 255u - Channel(source, 24);
    const std::uint32_t blue = Channel(source, 0) +
                               DivideBy255(Channel(destination, 0) * inverse_alpha);
    const std::uint32_t green = Channel(source, 8) +
                                DivideBy255(Channel(destination, 8) * inverse_alpha);
    const std::uint32_t red = Channel(source, 16) +
                              DivideBy255(Channel(destination, 16) * inverse_alpha);
    const std::uint32_t alpha = Channel(source, 24) +
                                DivideBy255(Channel(destination, 24) * inverse_alpha);
    return (std::min(255u, alpha) << 24u) | (std::min(255u, red) << 16u) |
           (std::min(255u, green) << 8u) | std::min(255u, blue);
}

void SourceOverSolid(std::uint32_t* pixels, int width, int height, int stride_pixels,
                     const RECT& region, const RgbaColor& color) noexcept {
    if (!pixels || width <= 0 || height <= 0 || stride_pixels < width || color.alpha == 0) return;
    const RECT clipped{std::max(0L, region.left), std::max(0L, region.top),
                       std::min(static_cast<LONG>(width), region.right),
                       std::min(static_cast<LONG>(height), region.bottom)};
    if (clipped.right <= clipped.left || clipped.bottom <= clipped.top) return;
    const std::uint32_t source = Premultiply(color);
    for (LONG y = clipped.top; y < clipped.bottom; ++y) {
        std::uint32_t* row = pixels + static_cast<std::size_t>(y) * stride_pixels;
        for (LONG x = clipped.left; x < clipped.right; ++x) {
            row[x] = SourceOverPremultiplied(row[x], source);
        }
    }
}

}  // namespace rendering
