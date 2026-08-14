#pragma once

#include <windows.h>

#include <map>
#include <set>
#include <string>

#include "rendering/corner_tile_cache.h"
#include "rendering/native_peer_gdi_resource_cache.h"
#include "ui/config/resolved_ui_document.h"

namespace rendering {

class WindowRenderContext;

struct RgbaColor {
    BYTE red = 0;
    BYTE green = 0;
    BYTE blue = 0;
    BYTE alpha = 255;
};

struct RenderRuntimeDiagnostics {
    std::uint64_t resource_epoch = 1;
    std::size_t active_window_contexts = 0;
    std::size_t cached_fonts = 0;
    std::size_t cached_brushes = 0;
    std::size_t cached_pens = 0;
    std::size_t cached_corner_tiles = 0;
    std::size_t native_peer_fonts = 0;
    std::size_t native_peer_brushes = 0;
    std::size_t native_peer_font_leases = 0;
    std::size_t native_peer_brush_leases = 0;
};

class RenderRuntime final {
public:
    RenderRuntime() = default;
    ~RenderRuntime();

    RenderRuntime(const RenderRuntime&) = delete;
    RenderRuntime& operator=(const RenderRuntime&) = delete;

    RgbaColor ResolveColor(const ui::config::ResolvedColor& color) const noexcept;
    HFONT Font(const ui::config::ResolvedFont& descriptor, UINT dpi);
    HBRUSH Brush(COLORREF color);
    HPEN Pen(COLORREF color, int width);
    bool PaintRoundedStyleBox(HDC target, const RECT& bounds, int radius, int border_width,
                              const RgbaColor& fill, const RgbaColor& border,
                              COLORREF opaque_background, UINT dpi,
                              unsigned int visual_state);
    NativePeerGdiResourceCache& native_peer_resources() noexcept;
    const NativePeerGdiResourceCache& native_peer_resources() const noexcept;
    void AdvanceResourceEpoch();
    std::uint64_t resource_epoch() const noexcept;
    RenderRuntimeDiagnostics diagnostics() const noexcept;
    void Reset();

    void RegisterWindowContext(WindowRenderContext* context);
    void UnregisterWindowContext(WindowRenderContext* context) noexcept;

private:
    struct FontKey {
        std::string family;
        int point_size = 0;
        int weight = 0;
        UINT dpi = 96;

        bool operator<(const FontKey& other) const noexcept;
    };

    struct PenKey {
        COLORREF color = 0;
        int width = 1;

        bool operator<(const PenKey& other) const noexcept;
    };

    std::map<FontKey, HFONT> fonts_;
    std::map<COLORREF, HBRUSH> brushes_;
    std::map<PenKey, HPEN> pens_;
    CornerTileCache corner_tiles_;
    NativePeerGdiResourceCache native_peer_resources_;
    std::set<WindowRenderContext*> window_contexts_;
    std::uint64_t resource_epoch_ = 1;
};

COLORREF ToColorRef(const RgbaColor& color) noexcept;
COLORREF CompositeOverOpaque(COLORREF background, const RgbaColor& foreground) noexcept;

}  // namespace rendering
