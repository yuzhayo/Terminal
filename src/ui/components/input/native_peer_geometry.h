#pragma once

#include <windows.h>

#include <span>

namespace ui::components {

bool ValidateNativePeerGeometry(const RECT& component_bounds, const RECT& peer_bounds,
                                std::span<const RECT> reserved_regions) noexcept;

}  // namespace ui::components
