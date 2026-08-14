#pragma once

#include <windows.h>

#include <cstdint>

namespace rendering {

struct RgbaColor;

std::uint32_t Premultiply(const RgbaColor& color) noexcept;
std::uint32_t SourceOverPremultiplied(std::uint32_t destination,
                                      std::uint32_t source) noexcept;
void SourceOverSolid(std::uint32_t* pixels, int width, int height, int stride_pixels,
                     const RECT& region, const RgbaColor& color) noexcept;

}  // namespace rendering
