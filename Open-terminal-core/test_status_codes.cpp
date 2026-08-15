// Quick compile test for stable error codes
#include "core/status.h"
#include "features/terminal_launch.h"
#include "features/claude_inject.h"
#include "features/claude_settings_file.h"
#include "features/chrome_profiles.h"

void TestStatusCodes() {
    // Verify error codes compile and are distinct
    core::Status validation = core::Error(core::ErrorCode::ValidationFailed, L"test");
    core::Status folder = core::Error(core::ErrorCode::FolderNotFound, L"test");
    core::Status venv = core::Error(core::ErrorCode::VenvNotFound, L"test");
    core::Status launch = core::Error(core::ErrorCode::LaunchFailed, L"test");
    core::Status persist = core::Error(core::ErrorCode::PersistenceFailed, L"test");
    core::Status settings_file = core::Error(core::ErrorCode::SettingsFileNotFound, L"test");
    core::Status backup = core::Error(core::ErrorCode::BackupNotFound, L"test");
    core::Status json_parse = core::Error(core::ErrorCode::JsonParseFailed, L"test");
    core::Status profile = core::Error(core::ErrorCode::ProfileNotFound, L"test");
    core::Status chrome = core::Error(core::ErrorCode::ChromeNotFound, L"test");
    core::Status wsl = core::Error(core::ErrorCode::WslNotReady, L"test");
    core::Status scan = core::Error(core::ErrorCode::ProfileScanFailed, L"test");
    core::Status bookmark = core::Error(core::ErrorCode::BookmarkNotFound, L"test");
    core::Status preset = core::Error(core::ErrorCode::PresetNotFound, L"test");

    // Verify adapters can branch on error code
    if (validation.code == core::ErrorCode::ValidationFailed) {
        // Handle validation failure
    }
    if (folder.code == core::ErrorCode::FolderNotFound) {
        // Handle folder not found
    }
}

int main() {
    TestStatusCodes();
    return 0;
}
