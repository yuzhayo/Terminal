#pragma once

#include <windows.h>

#include <cstddef>
#include <map>
#include <string>

#include "ui/config/resolved_ui_document.h"

namespace rendering {

class NativePeerGdiResourceCache final {
private:
    struct FontKey {
        std::string family;
        std::string fallback_family;
        int point_size = 0;
        int weight = 0;
        UINT dpi = 96;

        bool operator<(const FontKey& other) const noexcept;
    };

public:
    class FontLease final {
    public:
        FontLease() = default;
        ~FontLease();
        FontLease(FontLease&& other) noexcept;
        FontLease& operator=(FontLease&& other) noexcept;
        FontLease(const FontLease&) = delete;
        FontLease& operator=(const FontLease&) = delete;

        HFONT get() const noexcept;
        explicit operator bool() const noexcept;
        void Reset() noexcept;

    private:
        friend class NativePeerGdiResourceCache;
        FontLease(NativePeerGdiResourceCache* owner, FontKey key, HFONT font) noexcept;

        NativePeerGdiResourceCache* owner_ = nullptr;
        FontKey key_;
        HFONT font_ = nullptr;
        bool leased_ = false;
    };

    class BrushLease final {
    public:
        BrushLease() = default;
        ~BrushLease();
        BrushLease(BrushLease&& other) noexcept;
        BrushLease& operator=(BrushLease&& other) noexcept;
        BrushLease(const BrushLease&) = delete;
        BrushLease& operator=(const BrushLease&) = delete;

        HBRUSH get() const noexcept;
        explicit operator bool() const noexcept;
        void Reset() noexcept;

    private:
        friend class NativePeerGdiResourceCache;
        BrushLease(NativePeerGdiResourceCache* owner, COLORREF color, HBRUSH brush) noexcept;

        NativePeerGdiResourceCache* owner_ = nullptr;
        COLORREF color_ = 0;
        HBRUSH brush_ = nullptr;
    };

    NativePeerGdiResourceCache() = default;
    ~NativePeerGdiResourceCache();
    NativePeerGdiResourceCache(const NativePeerGdiResourceCache&) = delete;
    NativePeerGdiResourceCache& operator=(const NativePeerGdiResourceCache&) = delete;

    FontLease AcquireFont(const ui::config::ResolvedFont& descriptor, UINT dpi);
    BrushLease AcquireBrush(COLORREF color);

    std::size_t physical_font_count() const noexcept;
    std::size_t physical_brush_count() const noexcept;
    std::size_t active_font_lease_count() const noexcept;
    std::size_t active_brush_lease_count() const noexcept;

private:
    struct FontEntry {
        HFONT handle = nullptr;
        std::size_t leases = 0;
    };

    struct BrushEntry {
        HBRUSH handle = nullptr;
        std::size_t leases = 0;
    };

    void ReleaseFont(const FontKey& key) noexcept;
    void ReleaseBrush(COLORREF color) noexcept;

    std::map<FontKey, FontEntry> fonts_;
    std::map<COLORREF, BrushEntry> brushes_;
};

}  // namespace rendering
