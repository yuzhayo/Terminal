// Result of one business operation, in a form any frontend can render.
//
// The old screens each owned a `status_` string plus a `status_is_error_` bool and
// called InvalidateRect from the setter, so the message text and the decision to
// repaint were welded together in six places. Core returns this instead: the text
// stays here (one source of wording), the frontend picks the colour and decides
// when to redraw.
#pragma once
#include <string>
#include <utility>

namespace core {

enum class StatusKind {
    None,     // nothing to show
    Info,     // in progress, or a neutral fact
    Success,  // the operation completed
    Error,    // the operation did not happen
};

// Stable error codes for adapter branching. Text changes, codes don't.
enum class ErrorCode {
    None = 0,
    // Terminal
    FolderNotFound,
    WslNotReady,
    LaunchFailed,
    // Inject
    InvalidBaseUrl,
    InvalidApiKey,
    SettingsFileNotFound,
    SettingsFileMalformed,
    SettingsFileWriteFailed,
    // Editor
    FileReadFailed,
    FileWriteFailed,
    InvalidJson,
    BackupNotFound,
    // Chrome
    ProfileScanFailed,
    ProfileNotFound,
    ChromeNotFound,
    BookmarkAlreadyExists,
    BookmarkNotFound,
    PresetNotFound,
    // Settings
    InvalidTheme,
    RegistryWriteFailed,
    // General
    ValidationFailed,
    PersistenceFailed,
};

struct Status {
    StatusKind kind = StatusKind::None;
    ErrorCode code  = ErrorCode::None;
    std::wstring text;

    bool ok() const { return kind != StatusKind::Error; }
    bool empty() const { return kind == StatusKind::None && text.empty(); }
};

inline Status NoStatus() { return {}; }
inline Status Info(std::wstring text) { return {StatusKind::Info, ErrorCode::None, std::move(text)}; }
inline Status Success(std::wstring text) { return {StatusKind::Success, ErrorCode::None, std::move(text)}; }
inline Status Error(ErrorCode code, std::wstring text) { return {StatusKind::Error, code, std::move(text)}; }

// Convenience for mutations whose only outcome is "saved or not": returns
// NoStatus on success, PersistenceFailed otherwise. Call sites that carry their
// own success text should check the bool directly instead.
inline Status PersistOr(bool ok, std::wstring what) {
    return ok ? NoStatus()
              : Error(ErrorCode::PersistenceFailed, L"Could not save " + std::move(what) + L".");
}

}  // namespace core
