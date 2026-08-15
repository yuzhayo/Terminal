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
    VenvActivationFailed,
    PowerShellNotFound,
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
    PresetAlreadyExists,
    PresetNotFound,
    // Settings
    InvalidTheme,
    RegistryWriteFailed,
    // General
    ValidationFailed,
    PersistenceFailed,
    PlatformFailed,
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

}  // namespace core
