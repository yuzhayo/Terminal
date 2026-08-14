// UI configuration draft editor — business logic only, no UI types.
//
// Subject matter: the app's own ui.json (fonts, window sizing, layout, radii,
// theme palettes, per-component color slots, density, corner preference).
// The validation rules and the draft/opened/preview state machine live here so
// the frontend only handles rendering and user input.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/status.h"
#include "storage/json.h"

namespace features {

// ---- field schema ----

enum class UiFieldKind { Text, Integer, Color };

struct UiField {
    std::wstring section;
    std::wstring label;
    std::vector<std::wstring> path;  // JSON pointer segments
    UiFieldKind kind = UiFieldKind::Text;
    int min_value = 0;
    int max_value = 0;
    std::wstring error;              // set by ValidateField on failure
    std::wstring current_text;      // read from / written to by the frontend
};

// Builds the canonical field table for one theme side (dark=false, light=true).
// Call again with the other value when the user flips the dark/light toggle.
std::vector<UiField> BuildFieldSchema(bool editing_light);

// ---- draft ----

struct UiDraft {
    json::Value root;
    bool loaded = false;
};

// ---- file operations ----

// Loads ui.json from disk into `out`. Missing file yields the built-in defaults.
// On failure, `error` contains a user-facing message.
bool LoadDraft(UiDraft* out, std::wstring* error);

// Loads the built-in defaults into `out`.
bool DefaultsDraft(UiDraft* out, std::wstring* error);

// Serializes `draft` to a pretty-printed JSON string for writing.
std::wstring SerializeDraft(const UiDraft& draft);

// Path to the user's ui.json. Creates the app-data directory when needed.
bool EnsureUserConfigPath(std::wstring* path, std::wstring* error);
std::wstring UserConfigPath();
bool UserConfigExists();

// ---- validation ----

// Validates and writes one field's current_text into draft.root.
// Sets field.error and returns false on any validation error.
// `first_error` receives the first error label+message (not overwritten if already set).
bool ValidateField(UiField* field, UiDraft* draft, std::wstring* first_error);

// Validates all fields (non-short-circuiting). Returns false if any failed.
bool PullFields(std::vector<UiField>* fields, UiDraft* draft, std::wstring* first_error);

// Reads values from the draft back into field.current_text. Call after any draft change.
void PopulateFields(std::vector<UiField>* fields, const UiDraft& draft);

// ---- enumerated constraints (not in the field table) ----
// Density: "compact" | "default" | "comfortable"
// cornerPreference: "default" | "square" | "rounded" | "small-rounded"
void SetDensity(UiDraft* draft, const std::wstring& value);
void SetCornerPreference(UiDraft* draft, const std::wstring& value);
std::wstring GetDensity(const UiDraft& draft);
std::wstring GetCornerPreference(const UiDraft& draft);

// ---- state machine ----

struct UiEditorState {
    UiDraft draft;
    UiDraft opened;   // last successfully saved version
    bool previewed = false;
};

// Applies the draft to in-memory config (no file write). Sets previewed = true.
// Returns false + error when validation or preview fails.
bool Preview(UiEditorState* state, std::vector<UiField>* fields, std::wstring* error);

// Validates, applies to memory, backs up, writes to disk with rollback on failure.
core::Status ApplyAndSave(UiEditorState* state, std::vector<UiField>* fields);

// Resets draft to opened.
core::Status ResetDraft(UiEditorState* state);

// Loads defaults into draft (does not write anything).
core::Status UseDefaults(UiEditorState* state);

// Called when leaving the screen. When restore_file=true and previewed=true,
// reloads the last saved file to undo the in-memory preview.
void Leave(UiEditorState* state, bool restore_file);

}  // namespace features
