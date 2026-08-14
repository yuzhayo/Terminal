#include "rendering/native_peer_gdi_resource_cache.h"

#include <tuple>
#include <utility>

namespace rendering {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}

HFONT CreateFontForKey(const ui::config::ResolvedFont& descriptor, UINT dpi,
                       const std::string& family) {
    const int height = -MulDiv(descriptor.point_size, static_cast<int>(dpi), 72);
    const std::wstring wide_family = Utf8ToWide(family);
    return CreateFontW(height, 0, 0, 0, descriptor.weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, wide_family.c_str());
}

}  // namespace

NativePeerGdiResourceCache::FontLease::FontLease(NativePeerGdiResourceCache* owner, FontKey key,
                                                  HFONT font) noexcept
    : owner_(owner), key_(std::move(key)), font_(font), leased_(owner && font) {}

NativePeerGdiResourceCache::FontLease::~FontLease() {
    Reset();
}

NativePeerGdiResourceCache::FontLease::FontLease(FontLease&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), key_(std::move(other.key_)),
      font_(std::exchange(other.font_, nullptr)), leased_(std::exchange(other.leased_, false)) {}

NativePeerGdiResourceCache::FontLease& NativePeerGdiResourceCache::FontLease::operator=(
    FontLease&& other) noexcept {
    if (this != &other) {
        Reset();
        owner_ = std::exchange(other.owner_, nullptr);
        key_ = std::move(other.key_);
        font_ = std::exchange(other.font_, nullptr);
        leased_ = std::exchange(other.leased_, false);
    }
    return *this;
}

HFONT NativePeerGdiResourceCache::FontLease::get() const noexcept {
    return font_;
}

NativePeerGdiResourceCache::FontLease::operator bool() const noexcept {
    return font_ != nullptr;
}

void NativePeerGdiResourceCache::FontLease::Reset() noexcept {
    if (owner_ && leased_) owner_->ReleaseFont(key_);
    owner_ = nullptr;
    font_ = nullptr;
    leased_ = false;
}

NativePeerGdiResourceCache::BrushLease::BrushLease(NativePeerGdiResourceCache* owner, COLORREF color,
                                                    HBRUSH brush) noexcept
    : owner_(owner), color_(color), brush_(brush) {}

NativePeerGdiResourceCache::BrushLease::~BrushLease() {
    Reset();
}

NativePeerGdiResourceCache::BrushLease::BrushLease(BrushLease&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), color_(other.color_),
      brush_(std::exchange(other.brush_, nullptr)) {}

NativePeerGdiResourceCache::BrushLease& NativePeerGdiResourceCache::BrushLease::operator=(
    BrushLease&& other) noexcept {
    if (this != &other) {
        Reset();
        owner_ = std::exchange(other.owner_, nullptr);
        color_ = other.color_;
        brush_ = std::exchange(other.brush_, nullptr);
    }
    return *this;
}

HBRUSH NativePeerGdiResourceCache::BrushLease::get() const noexcept {
    return brush_;
}

NativePeerGdiResourceCache::BrushLease::operator bool() const noexcept {
    return brush_ != nullptr;
}

void NativePeerGdiResourceCache::BrushLease::Reset() noexcept {
    if (owner_ && brush_) owner_->ReleaseBrush(color_);
    owner_ = nullptr;
    color_ = 0;
    brush_ = nullptr;
}

NativePeerGdiResourceCache::~NativePeerGdiResourceCache() {
    for (const auto& [key, entry] : fonts_) {
        (void)key;
        DeleteObject(entry.handle);
    }
    for (const auto& [key, entry] : brushes_) {
        (void)key;
        DeleteObject(entry.handle);
    }
}

bool NativePeerGdiResourceCache::FontKey::operator<(const FontKey& other) const noexcept {
    return std::tie(family, fallback_family, point_size, weight, dpi) <
           std::tie(other.family, other.fallback_family, other.point_size, other.weight, other.dpi);
}

NativePeerGdiResourceCache::FontLease NativePeerGdiResourceCache::AcquireFont(
    const ui::config::ResolvedFont& descriptor, UINT dpi) {
    FontKey key{descriptor.family, descriptor.fallback_family, descriptor.point_size,
                descriptor.weight, dpi};
    auto existing = fonts_.find(key);
    if (existing == fonts_.end()) {
        HFONT font = CreateFontForKey(descriptor, dpi, descriptor.family);
        if (!font && !descriptor.fallback_family.empty()) {
            font = CreateFontForKey(descriptor, dpi, descriptor.fallback_family);
        }
        if (!font) return {};
        existing = fonts_.emplace(key, FontEntry{font, 0}).first;
    }
    ++existing->second.leases;
    return FontLease(this, key, existing->second.handle);
}

NativePeerGdiResourceCache::BrushLease NativePeerGdiResourceCache::AcquireBrush(COLORREF color) {
    auto existing = brushes_.find(color);
    if (existing == brushes_.end()) {
        HBRUSH brush = CreateSolidBrush(color);
        if (!brush) return {};
        existing = brushes_.emplace(color, BrushEntry{brush, 0}).first;
    }
    ++existing->second.leases;
    return BrushLease(this, color, existing->second.handle);
}

std::size_t NativePeerGdiResourceCache::physical_font_count() const noexcept {
    return fonts_.size();
}

std::size_t NativePeerGdiResourceCache::physical_brush_count() const noexcept {
    return brushes_.size();
}

std::size_t NativePeerGdiResourceCache::active_font_lease_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [key, entry] : fonts_) {
        (void)key;
        count += entry.leases;
    }
    return count;
}

std::size_t NativePeerGdiResourceCache::active_brush_lease_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [key, entry] : brushes_) {
        (void)key;
        count += entry.leases;
    }
    return count;
}

void NativePeerGdiResourceCache::ReleaseFont(const FontKey& key) noexcept {
    const auto existing = fonts_.find(key);
    if (existing == fonts_.end() || existing->second.leases == 0) return;
    --existing->second.leases;
    if (existing->second.leases == 0) {
        DeleteObject(existing->second.handle);
        fonts_.erase(existing);
    }
}

void NativePeerGdiResourceCache::ReleaseBrush(COLORREF color) noexcept {
    const auto existing = brushes_.find(color);
    if (existing == brushes_.end() || existing->second.leases == 0) return;
    --existing->second.leases;
    if (existing->second.leases == 0) {
        DeleteObject(existing->second.handle);
        brushes_.erase(existing);
    }
}

}  // namespace rendering
