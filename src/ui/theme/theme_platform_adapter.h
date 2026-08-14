#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "ui/config/resolved_ui_document.h"

namespace ui::theme {

enum class PlatformAppTheme { Dark, Light };

struct ThemePlatformSnapshot {
    bool high_contrast = false;
    PlatformAppTheme app_theme = PlatformAppTheme::Light;
    bool used_light_fallback = false;

    bool operator==(const ThemePlatformSnapshot&) const = default;
};

class ThemePlatformAdapter final {
public:
    explicit ThemePlatformAdapter(ThemePlatformSnapshot initial_snapshot);
    ~ThemePlatformAdapter();

    ThemePlatformAdapter(const ThemePlatformAdapter&) = delete;
    ThemePlatformAdapter& operator=(const ThemePlatformAdapter&) = delete;

    static ThemePlatformSnapshot ReadInitialSnapshot() noexcept;
    static COLORREF MaterializeSystemColor(config::SystemColorSlot slot) noexcept;

    config::ThemeKind Select(config::ThemePreference preference) const noexcept;
    bool Reconcile(ThemePlatformSnapshot snapshot) noexcept;
    bool QueueBackgroundSnapshot(ThemePlatformSnapshot snapshot) noexcept;
    bool ApplyQueuedSnapshot() noexcept;
    bool StartPostFirstFrameMonitoring(HWND infrastructure_window, UINT signal_message,
                                       std::wstring& diagnostic) noexcept;
    ThemePlatformSnapshot ReadPostFirstFrameSnapshot() const noexcept;

    const ThemePlatformSnapshot& snapshot() const noexcept;
    std::uint64_t resource_epoch() const noexcept;

private:
    struct Impl;

    ThemePlatformSnapshot snapshot_;
    std::optional<ThemePlatformSnapshot> queued_snapshot_;
    std::uint64_t resource_epoch_ = 1;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ui::theme
