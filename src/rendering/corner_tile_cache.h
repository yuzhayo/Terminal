#pragma once

#include <windows.h>

#include <cstddef>
#include <map>

namespace rendering {

struct CornerTileKey {
    int radius = 0;
    int thickness = 0;
    COLORREF fill = 0;
    COLORREF border = 0;
    COLORREF background = 0;
    UINT dpi = 96;
    unsigned int state = 0;

    bool operator<(const CornerTileKey& other) const noexcept;
};

class CornerTileCache final {
public:
    static constexpr std::size_t kMaximumEntries = 96;

    CornerTileCache() = default;
    ~CornerTileCache();

    CornerTileCache(const CornerTileCache&) = delete;
    CornerTileCache& operator=(const CornerTileCache&) = delete;

    bool Paint(HDC target, const RECT& bounds, const CornerTileKey& key, HBRUSH fill_brush,
               HBRUSH border_brush, HPEN fallback_pen) noexcept;
    bool Prepare(const CornerTileKey& key) noexcept;
    void Clear() noexcept;
    std::size_t entry_count() const noexcept;

private:
    HBITMAP BuildDisc(const CornerTileKey& key) noexcept;
    HBITMAP DiscFor(const CornerTileKey& key, bool allow_create) noexcept;
    HDC SourceDc() noexcept;

    HDC source_dc_ = nullptr;
    std::map<CornerTileKey, HBITMAP> tiles_;
};

}  // namespace rendering
