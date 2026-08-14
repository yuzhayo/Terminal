#pragma once

#include "ui/components/component.h"

namespace ui::components {

class InputComponent final : public Component {
public:
    InputComponent(const config::ResolvedComponent& definition, ComponentHost& host);
    ~InputComponent() override;

    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Arrange(const RECT& bounds) override;
    void Paint(HDC dc) override;
    bool PointerDown(POINT point) override;
    bool HandleCommand(HWND source, WORD notification) override;
    HBRUSH HandleControlColor(HDC dc, HWND source) override;
    bool OwnsNativePeer(HWND source) const noexcept override;

private:
    static LRESULT CALLBACK EditSubclassProcedure(HWND window, UINT message, WPARAM wparam,
                                                   LPARAM lparam, UINT_PTR subclass_id,
                                                   DWORD_PTR reference_data);
    const config::InputProperties& Properties() const;
    config::VisualState State() const noexcept;
    void ApplyNativeStyle();
    void PaintPlaceholder();

    HWND edit_ = nullptr;
    bool focused_ = false;
};

}  // namespace ui::components
