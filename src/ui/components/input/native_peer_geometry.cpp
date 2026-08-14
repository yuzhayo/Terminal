#include "ui/components/input/native_peer_geometry.h"

namespace ui::components {
namespace {

bool Intersects(const RECT& left, const RECT& right) noexcept {
    return left.left < right.right && left.right > right.left && left.top < right.bottom &&
           left.bottom > right.top;
}

}  // namespace

bool ValidateNativePeerGeometry(const RECT& component_bounds, const RECT& peer_bounds,
                                std::span<const RECT> reserved_regions) noexcept {
    if (peer_bounds.left < component_bounds.left || peer_bounds.top < component_bounds.top ||
        peer_bounds.right > component_bounds.right || peer_bounds.bottom > component_bounds.bottom ||
        peer_bounds.right <= peer_bounds.left || peer_bounds.bottom <= peer_bounds.top) {
        return false;
    }
    for (const RECT& reserved : reserved_regions) {
        if (Intersects(peer_bounds, reserved)) return false;
    }
    return true;
}

}  // namespace ui::components
