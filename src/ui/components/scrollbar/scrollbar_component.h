#pragma once

#include "ui/components/component.h"

namespace ui::components {

class ScrollModel {
public:
    virtual ~ScrollModel() = default;
    virtual int ScrollMinimum() const noexcept = 0;
    virtual int ScrollMaximum() const noexcept = 0;
    virtual int ScrollPageSize() const noexcept = 0;
    virtual int ScrollValue() const noexcept = 0;
    virtual void SetScrollValue(int value) = 0;
};

struct ScrollbarMetrics {
    int track_length = 0;
    int thumb_start = 0;
    int thumb_length = 0;
};

ScrollbarMetrics CalculateScrollbarMetrics(int track_length, int minimum_thumb_length,
                                            int minimum, int maximum, int page_size,
                                            int value) noexcept;

class ScrollbarComponent final : public Component {
public:
    using Component::Component;

    void Bind(ScrollModel* model) noexcept;
    void Refresh();
    MeasuredSize Measure(HDC dc, int available_width, int available_height) override;
    void Paint(HDC dc) override;
    bool PointerMove(POINT point) override;
    bool PointerDown(POINT point) override;
    bool PointerUp(POINT point) override;
    bool PointerWheel(int delta) override;
    bool CanFocus() const noexcept override;
    bool FocusNativePeer() override;
    void SetLogicalFocus(bool focused, bool window_active) override;
    bool HandleKeyDown(UINT virtual_key) override;
    AutomationRole automation_role() const noexcept override;
    std::optional<AutomationRangeValue> automation_range_value() const noexcept override;
    bool AutomationSetRangeValue(double value) override;

private:
    const config::ScrollbarProperties& Properties() const;
    RECT ThumbBounds() const noexcept;
    int PrimaryCoordinate(POINT point) const noexcept;
    int PrimaryLength() const noexcept;
    void StepBy(int delta);

    ScrollModel* model_ = nullptr;
    bool hovered_ = false;
    bool thumb_hovered_ = false;
    bool pressed_ = false;
    bool dragging_ = false;
    bool focused_ = false;
    bool window_active_ = true;
    int drag_pointer_origin_ = 0;
    int drag_value_origin_ = 0;
};

}  // namespace ui::components
