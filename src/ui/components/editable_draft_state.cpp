#include "ui/components/editable_draft_state.h"

#include <utility>

namespace ui::components {

EditableDraftState::EditableDraftState(std::wstring initial_value)
    : baseline_(initial_value), value_(std::move(initial_value)) {}

void EditableDraftState::Update(std::wstring value) {
    value_ = std::move(value);
}

void EditableDraftState::ApplySaveResult(bool success) {
    if (success) baseline_ = value_;
}

bool EditableDraftState::StageDiscard() {
    if (staged_value_) return false;
    staged_value_ = value_;
    value_ = baseline_;
    return true;
}

void EditableDraftState::CommitDiscard() noexcept {
    staged_value_.reset();
}

void EditableDraftState::RollbackDiscard() {
    if (!staged_value_) return;
    value_ = std::move(*staged_value_);
    staged_value_.reset();
}

const std::wstring& EditableDraftState::value() const noexcept {
    return value_;
}

const std::wstring& EditableDraftState::baseline() const noexcept {
    return baseline_;
}

bool EditableDraftState::is_dirty() const noexcept {
    return value_ != baseline_;
}

bool EditableDraftState::discard_staged() const noexcept {
    return staged_value_.has_value();
}

}  // namespace ui::components
