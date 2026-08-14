#pragma once

#include <windows.h>

#include <memory>
#include <optional>
#include <string>

#include "platform/app_paths.h"
#include "ui/config/resolved_ui_document.h"

namespace ui::config {

class UiConfigGate final {
public:
    UiConfigGate(HINSTANCE instance, platform::AppPaths paths);

    bool ResolveBootstrap(std::wstring& diagnostic);
    bool Reload(std::wstring& diagnostic);

    std::shared_ptr<const ResolvedUiDocument> document() const noexcept;
    const UiConfigMetadata& metadata() const noexcept;
    const std::optional<ResolveDiagnostic>& active_diagnostic() const noexcept;
    std::wstring active_diagnostic_text() const;
    const platform::AppPaths& paths() const noexcept;

private:
    bool ResolveCandidate(bool bootstrap, std::wstring& diagnostic);
    void Publish(std::shared_ptr<const ResolvedUiDocument> document,
                 std::optional<ResolveDiagnostic> override_diagnostic);
    void RecordDiagnostic(const ResolveDiagnostic& diagnostic) const noexcept;

    HINSTANCE instance_ = nullptr;
    platform::AppPaths paths_;
    std::shared_ptr<const ResolvedUiDocument> document_;
    std::optional<ResolveDiagnostic> active_diagnostic_;
};

}  // namespace ui::config
