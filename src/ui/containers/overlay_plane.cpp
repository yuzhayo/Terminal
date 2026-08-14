#include "ui/containers/overlay_plane.h"

#include <algorithm>

#include "rendering/window_render_context.h"

namespace ui::containers {

OverlayPlane::LayerId OverlayPlane::Push(Painter painter) {
    if (!painter) return 0;
    const LayerId id = next_id_++;
    layers_.push_back({id, std::move(painter)});
    return id;
}

bool OverlayPlane::Remove(LayerId id) {
    const auto existing = std::find_if(layers_.begin(), layers_.end(),
                                       [id](const Layer& layer) { return layer.id == id; });
    if (existing == layers_.end()) return false;
    layers_.erase(existing);
    return true;
}

void OverlayPlane::Clear() noexcept {
    layers_.clear();
}

void OverlayPlane::Paint(rendering::WindowRenderContext& context,
                         const RECT& invalid_region) const {
    for (const Layer& layer : layers_) layer.painter(context, invalid_region);
}

std::size_t OverlayPlane::size() const noexcept {
    return layers_.size();
}

}  // namespace ui::containers
