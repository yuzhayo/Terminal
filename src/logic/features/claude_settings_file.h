// Claude Code settings file editor state — business logic only, no UI types.
//
// The OS edit control owns the text buffer, cursor, selection, and undo. This
// module owns the per-target draft (text + loaded + dirty), the load/save/restore
// orchestration, and the WSL readiness gate.
//
// Async pattern: StartLoad is a blocking call that the frontend should run on a
// worker thread. It returns a LoadResult that the frontend hands back via
// ApplyLoad on its own thread.
#pragma once
#include <string>

#include "core/status.h"

namespace features {

enum class EditorTarget { Windows, UbuntuWsl };

std::wstring EditorTargetName(EditorTarget target);
EditorTarget EditorTargetFromName(const std::wstring& name);

// --- per-target draft ---

struct EditorDraft {
    std::wstring text;
    bool loaded = false;
    bool dirty  = false;
};

// --- load result (returned from StartLoad, passed to ApplyLoad) ---

struct EditorLoadResult {
    EditorTarget target  = EditorTarget::Windows;
    std::wstring path;     // resolved path, or the error message when resolved==false
    std::wstring text;
    std::wstring error;
    bool resolved    = false;
    bool read_ok     = false;
    bool has_backup  = false;
    bool wanted_text = false;
    bool created     = false;  // true when the file was missing and text was seeded
};

// --- service ---

// Runs a load synchronously (call from a worker thread for WSL targets).
// Sets result.path / text / has_backup. An empty file yields "{\n}\n".
EditorLoadResult StartLoad(EditorTarget target, bool force, const EditorDraft& draft);

// Applies a load result to the draft. Returns the status text for the UI.
// On success, sets draft.loaded = true, draft.dirty = false.
core::Status ApplyLoad(const EditorLoadResult& result, EditorDraft* draft);

// Returns true if a file action (save / restore) can proceed. When false the
// returned status explains why; the caller may start a background probe.
bool ReadyForFileAction(EditorTarget target, core::Status* status);

// Saves the draft text. The draft holds edit-control text (CRLF); Save is the
// single owner that converts CRLF -> LF (canonical on disk) before validating and
// writing, then stores the CRLF form back into the draft. Appends a trailing
// newline when missing. Validates JSON, backs up, atomically replaces. Sets
// draft.dirty = false on success.
core::Status Save(EditorTarget target, EditorDraft* draft);

// Restores the backup file.
core::Status RestoreBackup(EditorTarget target);

// Persists the target choice and returns the new active target.
EditorTarget SwitchTarget(EditorTarget target);

// Drops dirty drafts without prompting (called on screen enter). Clears text and
// loaded flag so the next load reads fresh from disk.
void DropUnsavedDrafts(EditorDraft* windows_draft, EditorDraft* wsl_draft);

// LF is canonical on disk. These adapt between disk and a CRLF edit control.
// They stay in core so the frontend doesn't have to know the on-disk convention.
std::wstring ToEditorText(const std::wstring& lf_text);
std::wstring FromEditorText(const std::wstring& crlf_text);

}  // namespace features
