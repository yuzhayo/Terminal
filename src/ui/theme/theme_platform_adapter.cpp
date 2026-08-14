#include "ui/theme/theme_platform_adapter.h"

#include <windows.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/base.h>

#include <atomic>
#include <memory>
#include <utility>

namespace ui::theme {
namespace {

PlatformAppTheme ThemeFromForegroundColor(const winrt::Windows::UI::Color& color) noexcept {
    const int luminance = static_cast<int>(color.R) * 299 + static_cast<int>(color.G) * 587 +
                          static_cast<int>(color.B) * 114;
    return luminance >= 128000 ? PlatformAppTheme::Dark : PlatformAppTheme::Light;
}

}  // namespace

struct ThemePlatformAdapter::Impl {
    winrt::Windows::UI::ViewManagement::UISettings settings{nullptr};
    winrt::event_token color_values_changed{};
    bool subscribed = false;
    bool apartment_initialized = false;
    std::shared_ptr<std::atomic_bool> signal_pending =
        std::make_shared<std::atomic_bool>(false);

    ~Impl() {
        if (subscribed && settings) {
            try {
                settings.ColorValuesChanged(color_values_changed);
            } catch (...) {
            }
        }
        settings = nullptr;
        if (apartment_initialized) {
            winrt::uninit_apartment();
        }
    }
};

ThemePlatformAdapter::ThemePlatformAdapter(ThemePlatformSnapshot initial_snapshot)
    : snapshot_(initial_snapshot), impl_(std::make_unique<Impl>()) {}

ThemePlatformAdapter::~ThemePlatformAdapter() = default;

ThemePlatformSnapshot ThemePlatformAdapter::ReadInitialSnapshot() noexcept {
    ThemePlatformSnapshot snapshot;

    HIGHCONTRASTW high_contrast{};
    high_contrast.cbSize = sizeof(high_contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0)) {
        snapshot.high_contrast = (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }

    DWORD apps_use_light_theme = 1;
    DWORD bytes = sizeof(apps_use_light_theme);
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &apps_use_light_theme, &bytes);
    if (status == ERROR_SUCCESS && bytes == sizeof(apps_use_light_theme)) {
        snapshot.app_theme = apps_use_light_theme == 0 ? PlatformAppTheme::Dark
                                                       : PlatformAppTheme::Light;
    } else {
        snapshot.app_theme = PlatformAppTheme::Light;
        snapshot.used_light_fallback = true;
    }
    return snapshot;
}

COLORREF ThemePlatformAdapter::MaterializeSystemColor(config::SystemColorSlot slot) noexcept {
    switch (slot) {
        case config::SystemColorSlot::Window: return GetSysColor(COLOR_WINDOW);
        case config::SystemColorSlot::WindowText: return GetSysColor(COLOR_WINDOWTEXT);
        case config::SystemColorSlot::GrayText: return GetSysColor(COLOR_GRAYTEXT);
        case config::SystemColorSlot::Highlight: return GetSysColor(COLOR_HIGHLIGHT);
        case config::SystemColorSlot::HighlightText: return GetSysColor(COLOR_HIGHLIGHTTEXT);
    }
    return GetSysColor(COLOR_WINDOW);
}

config::ThemeKind ThemePlatformAdapter::Select(config::ThemePreference preference) const noexcept {
    if (snapshot_.high_contrast) return config::ThemeKind::HighContrast;
    if (preference == config::ThemePreference::Dark) return config::ThemeKind::Dark;
    if (preference == config::ThemePreference::Light) return config::ThemeKind::Light;
    return snapshot_.app_theme == PlatformAppTheme::Dark ? config::ThemeKind::Dark
                                                         : config::ThemeKind::Light;
}

bool ThemePlatformAdapter::Reconcile(ThemePlatformSnapshot snapshot) noexcept {
    if (snapshot == snapshot_) return false;
    snapshot_ = snapshot;
    ++resource_epoch_;
    return true;
}

bool ThemePlatformAdapter::QueueBackgroundSnapshot(ThemePlatformSnapshot snapshot) noexcept {
    const bool signal_required = !queued_snapshot_.has_value();
    queued_snapshot_ = snapshot;
    return signal_required;
}

bool ThemePlatformAdapter::ApplyQueuedSnapshot() noexcept {
    if (!queued_snapshot_) return false;
    const ThemePlatformSnapshot snapshot = *queued_snapshot_;
    queued_snapshot_.reset();
    return Reconcile(snapshot);
}

bool ThemePlatformAdapter::StartPostFirstFrameMonitoring(HWND infrastructure_window,
                                                         UINT signal_message,
                                                         std::wstring& diagnostic) noexcept {
    if (!infrastructure_window || signal_message < WM_APP) {
        diagnostic = L"Theme monitoring membutuhkan infrastructure window dan WM_APP signal.";
        return false;
    }
    if (impl_->subscribed) {
        diagnostic.clear();
        return true;
    }
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        impl_->apartment_initialized = true;
        impl_->settings = winrt::Windows::UI::ViewManagement::UISettings();
        const std::shared_ptr<std::atomic_bool> signal_pending = impl_->signal_pending;
        impl_->color_values_changed = impl_->settings.ColorValuesChanged(
            [infrastructure_window, signal_message, signal_pending](const auto&, const auto&) noexcept {
                if (!signal_pending->exchange(true)) {
                    if (!PostMessageW(infrastructure_window, signal_message, 0, 0)) {
                        signal_pending->store(false);
                    }
                }
            });
        impl_->subscribed = true;
        diagnostic.clear();
        return true;
    } catch (const winrt::hresult_error& error) {
        diagnostic = L"UISettings theme monitoring tidak tersedia: " + error.message();
    } catch (...) {
        diagnostic = L"UISettings theme monitoring tidak tersedia.";
    }
    return false;
}

ThemePlatformSnapshot ThemePlatformAdapter::ReadPostFirstFrameSnapshot() const noexcept {
    impl_->signal_pending->store(false);
    ThemePlatformSnapshot snapshot = ReadInitialSnapshot();
    if (!impl_->settings) return snapshot;
    try {
        const auto foreground = impl_->settings.GetColorValue(
            winrt::Windows::UI::ViewManagement::UIColorType::Foreground);
        snapshot.app_theme = ThemeFromForegroundColor(foreground);
        snapshot.used_light_fallback = false;
    } catch (...) {
    }
    return snapshot;
}

const ThemePlatformSnapshot& ThemePlatformAdapter::snapshot() const noexcept {
    return snapshot_;
}

std::uint64_t ThemePlatformAdapter::resource_epoch() const noexcept {
    return resource_epoch_;
}

}  // namespace ui::theme
