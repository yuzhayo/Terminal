#pragma once

#include "ui/components/component.h"

namespace ui::components {

class InputComponent final : public Component, public EditableParticipant {
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
    bool CanFocus() const noexcept override;
    bool FocusNativePeer() override;
    void SetLogicalFocus(bool focused, bool window_active) override;
    void OnDpiChanged() override;
    bool SuspendNativePeers(std::wstring& diagnostic) override;
    void ResumeNativePeers() override;
    void CollectEditableParticipants(std::vector<EditableParticipant*>& participants) override;

    bool IsDirty() const noexcept override;
    bool StageDiscard() override;
    void CommitDiscard() noexcept override;
    void RollbackDiscard() override;
    void ApplySaveResult(bool success) override;

private:
    static LRESULT CALLBACK EditSubclassProcedure(HWND window, UINT message, WPARAM wparam,
                                                   LPARAM lparam, UINT_PTR subclass_id,
                                                   DWORD_PTR reference_data);
    const config::InputProperties& Properties() const;
    config::VisualState State() const noexcept;
    void ApplyNativeStyle();
    void PaintPlaceholder();
    void PaintSuspendedSnapshot(HDC dc);
    std::wstring ReadPeerText() const;
    void WriteDraftToPeer();
    bool CompleteActiveIme(std::wstring& diagnostic);

    HWND edit_ = nullptr;
    rendering::NativePeerGdiResourceCache::FontLease font_lease_;
    rendering::NativePeerGdiResourceCache::BrushLease brush_lease_;
    COLORREF native_foreground_ = RGB(0, 0, 0);
    COLORREF native_background_ = RGB(255, 255, 255);
    EditableDraftState draft_;
    RECT native_peer_content_rect_{};
    std::wstring suspended_display_text_;
    DWORD suspended_selection_start_ = 0;
    DWORD suspended_selection_end_ = 0;
    int suspended_first_visible_line_ = 0;
    bool native_focused_ = false;
    bool logically_focused_ = false;
    bool window_active_ = true;
    bool geometry_valid_ = true;
    bool suspended_ = false;
    bool restore_focus_after_resume_ = false;
};

}  // namespace ui::components
