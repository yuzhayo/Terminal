#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace rendering {
class WindowRenderContext;
}

namespace ui::containers {

class OverlayPlane final {
public:
    using LayerId = std::uint64_t;
    using Painter =
        std::function<void(rendering::WindowRenderContext&, const RECT& invalid_region)>;

    LayerId Push(Painter painter);
    bool Remove(LayerId id);
    void Clear() noexcept;
    void Paint(rendering::WindowRenderContext& context, const RECT& invalid_region) const;
    std::size_t size() const noexcept;

private:
    struct Layer {
        LayerId id = 0;
        Painter painter;
    };

    LayerId next_id_ = 1;
    std::vector<Layer> layers_;
};

}  // namespace ui::containers
