#include "features/claude_settings_file.h"

#include "platform/files.h"
#include "platform/paths.h"
#include "platform/strings.h"
#include "platform/wsl.h"
#include "storage/json.h"
#include "storage/settings.h"

namespace features {
namespace {

bool ResolveSettingsPath(EditorTarget target, std::wstring* path, std::wstring* error) {
    if (target == EditorTarget::Windows) {
        const std::wstring home = paths::UserProfileDir();
        if (home.empty()) {
            if (error) *error = L"Cannot determine the Windows user profile folder.";
            return false;
        }
        *path = paths::Join(home, L".claude\\settings.json");
        return true;
    }
    return wsl::HomeFile(L".claude/settings.json", path, error);
}

}  // namespace

std::wstring EditorTargetName(EditorTarget target) {
    return target == EditorTarget::UbuntuWsl ? L"wsl" : L"windows";
}

EditorTarget EditorTargetFromName(const std::wstring& name) {
    return name == L"wsl" ? EditorTarget::UbuntuWsl : EditorTarget::Windows;
}

std::wstring ToEditorText(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r')) out.push_back(L'\r');
        out.push_back(text[i]);
    }
    return out;
}

std::wstring FromEditorText(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t c : text)
        if (c != L'\r') out.push_back(c);
    return out;
}

EditorLoadResult StartLoad(EditorTarget target, bool force, const EditorDraft& draft) {
    EditorLoadResult result;
    result.target      = target;
    result.wanted_text = force || !draft.loaded;

    std::wstring resolve_error;
    if (!ResolveSettingsPath(target, &result.path, &resolve_error)) {
        result.path  = resolve_error;
        result.error = resolve_error;
        return result;
    }
    result.resolved    = true;
    result.has_backup  = files::HasBackup(result.path);

    if (!result.wanted_text) {
        result.read_ok = true;
        return result;
    }

    std::wstring text;
    std::wstring read_error;
    if (!paths::FileExists(result.path)) {
        // Missing file — seed with empty object so Save always writes valid JSON.
        result.text    = L"{\n}\n";
        result.read_ok = true;
        return result;
    }
    if (!files::ReadText(result.path, &text, &read_error)) {
        result.error = read_error;
        return result;
    }
    if (str::Trim(text).empty()) {
        result.text    = L"{\n}\n";
        result.read_ok = true;
        return result;
    }
    result.text    = std::move(text);
    result.read_ok = true;
    return result;
}

core::Status ApplyLoad(const EditorLoadResult& result, EditorDraft* draft) {
    if (!result.resolved)
        return core::Error(result.error);
    if (!result.wanted_text)
        return core::NoStatus();
    if (!result.read_ok) {
        draft->loaded = true;
        draft->text.clear();
        draft->dirty = false;
        return core::Error(core::ErrorCode::FileReadFailed, result.error);
    }
    const bool created = (result.text == L"{\n}\n" && !paths::FileExists(result.path));
    draft->text   = result.text;
    draft->loaded = true;
    draft->dirty  = false;
    return created
        ? core::Info(L"No settings file yet — a new one will be created on save.")
        : core::Success(L"Loaded " + result.path);
}

bool ReadyForFileAction(EditorTarget target, core::Status* status) {
    if (target == EditorTarget::UbuntuWsl && !wsl::IsResolved()) {
        if (status) *status = core::Info(
            L"Still looking for the Ubuntu WSL home directory — try again in a moment.");
        return false;
    }
    return true;
}

core::Status Save(EditorTarget target, EditorDraft* draft) {
    core::Status gate;
    if (!ReadyForFileAction(target, &gate)) return gate;

    std::wstring text = draft->text;
    if (!text.empty() && text.back() != L'\n') text.push_back(L'\n');

    std::wstring path;
    std::wstring path_error;
    if (!ResolveSettingsPath(target, &path, &path_error))
        return core::Error(core::ErrorCode::SettingsFileNotFound, path_error);

    json::Value parsed;
    std::wstring parse_error;
    if (!json::Parse(text, &parsed, &parse_error))
        return core::Error(core::ErrorCode::InvalidJson, L"The editor content is not valid JSON:\n" + parse_error);

    std::wstring io_error;
    if (!files::MakeBackup(path, &io_error))
        return core::Error(core::ErrorCode::FileWriteFailed, io_error);
    if (!files::WriteTextAtomic(path, text, &io_error))
        return core::Error(core::ErrorCode::FileWriteFailed, io_error);

    draft->text  = text;
    draft->dirty = false;
    return core::Success(L"Saved " + path);
}

core::Status RestoreBackup(EditorTarget target) {
    core::Status gate;
    if (!ReadyForFileAction(target, &gate)) return gate;

    std::wstring path;
    std::wstring path_error;
    if (!ResolveSettingsPath(target, &path, &path_error))
        return core::Error(core::ErrorCode::SettingsFileNotFound, path_error);

    std::wstring io_error;
    if (!files::RestoreBackup(path, &io_error))
        return core::Error(core::ErrorCode::BackupNotFound, io_error);
    return core::Success(L"Restored from backup.");
}

EditorTarget SwitchTarget(EditorTarget target) {
    storage::CurrentSettings().json_target = EditorTargetName(target);
    storage::SaveSettings();
    return target;
}

void DropUnsavedDrafts(EditorDraft* windows_draft, EditorDraft* wsl_draft) {
    for (EditorDraft* draft : {windows_draft, wsl_draft}) {
        if (!draft->dirty) continue;
        draft->text.clear();
        draft->loaded = false;
        draft->dirty  = false;
    }
}

}  // namespace features
