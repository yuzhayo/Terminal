#pragma once

#include <windows.h>

#include <map>
#include <string>

#include "ui/config/resolved_ui_document.h"

namespace rendering {

struct RgbaColor {
    BYTE red = 0;
    BYTE green = 0;
    BYTE blue = 0;
    BYTE alpha = 255;
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
    void Reset();

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
};

COLORREF ToColorRef(const RgbaColor& color) noexcept;

}  // namespace rendering
