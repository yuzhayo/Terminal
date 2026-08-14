#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "rendering/render_runtime.h"
#include "rendering/window_render_context.h"
#include "ui/components/component.h"
#include "ui/components/component_registry.h"
#include "ui/config/resolved_ui_document.h"

namespace ui::containers {

class WindowContainer final {
public:
    WindowContainer(HINSTANCE instance, rendering::RenderRuntime& render_runtime,
                    std::shared_ptr<const config::ResolvedUiDocument> document,
                    config::ThemeKind theme_kind);
    ~WindowContainer();

    WindowContainer(const WindowContainer&) = delete;
    WindowContainer& operator=(const WindowContainer&) = delete;

    bool Create(const std::string& window_id, std::wstring& diagnostic);
    bool PrepareFirstFrame(std::wstring& diagnostic);
    void Show(int show_command);
    HWND hwnd() const noexcept;

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    bool BuildComponentTree(std::wstring& diagnostic);
    bool RenderCompleteFrame(HDC reference);
    void Layout();
    void TrackPointer(POINT point);
    void DispatchStubEvent(const config::EventDefinition& event);

    HINSTANCE instance_ = nullptr;
    rendering::RenderRuntime& render_runtime_;
    std::shared_ptr<const config::ResolvedUiDocument> document_;
    config::ThemeKind theme_kind_ = config::ThemeKind::Light;
    const config::ResolvedComponent* window_definition_ = nullptr;
    HWND window_ = nullptr;
    UINT dpi_ = 96;
    rendering::WindowRenderContext render_context_;
    components::ComponentRegistry registry_;
    std::unique_ptr<components::ComponentHost> component_host_;
    std::unique_ptr<components::Component> root_;
    components::Component* pointer_target_ = nullptr;
    bool frame_ready_ = false;
};

}  // namespace ui::containers
