#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ui::components {

class EditableDraftState final {
public:
    explicit EditableDraftState(std::wstring initial_value = {});

    void Update(std::wstring value);
    void ApplySaveResult(bool success);
    bool StageDiscard();
    void CommitDiscard() noexcept;
    void RollbackDiscard();

    const std::wstring& value() const noexcept;
    const std::wstring& baseline() const noexcept;
    bool is_dirty() const noexcept;
    bool discard_staged() const noexcept;

private:
    std::wstring baseline_;
    std::wstring value_;
    std::optional<std::wstring> staged_value_;
};

class EditableParticipant {
public:
    virtual ~EditableParticipant() = default;
    virtual bool IsDirty() const noexcept = 0;
    virtual bool StageDiscard() = 0;
    virtual void CommitDiscard() noexcept = 0;
    virtual void RollbackDiscard() = 0;
    virtual void ApplySaveResult(bool success) = 0;
};

}  // namespace ui::components
