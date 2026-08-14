#include "platform/files.h"

#include <windows.h>

#include <algorithm>
#include <string>

#include "platform/paths.h"
#include "platform/process.h"
#include "platform/strings.h"

namespace files {
namespace {

std::wstring Fail(std::wstring_view what, DWORD code) {
    return std::wstring(what) + L": " + process::ErrorMessage(code);
}

bool WriteUtf8(HANDLE handle, std::string_view utf8, std::wstring* error, std::wstring_view what) {
    size_t written_total = 0;
    while (written_total < utf8.size()) {
        DWORD written = 0;
        const DWORD want = static_cast<DWORD>((std::min)(utf8.size() - written_total, static_cast<size_t>(1u << 20)));
        if (!WriteFile(handle, utf8.data() + written_total, want, &written, nullptr)) {
            if (error) *error = Fail(what, GetLastError());
            return false;
        }
        if (written == 0) {
            if (error) *error = std::wstring(what) + L": no bytes were written.";
            return false;
        }
        written_total += written;
    }
    return true;
}

}  // namespace

bool ReadText(std::wstring_view path, std::wstring* out, std::wstring* error) {
    out->clear();
    const std::wstring file(path);
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) *error = Fail(L"Cannot open file", GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size)) {
        if (error) *error = Fail(L"Cannot read file size", GetLastError());
        CloseHandle(handle);
        return false;
    }
    if (size.QuadPart > 64LL * 1024 * 1024) {
        if (error) *error = L"File is too large to open (over 64 MB).";
        CloseHandle(handle);
        return false;
    }
    std::string raw(static_cast<size_t>(size.QuadPart), '\0');
    size_t read_total = 0;
    while (read_total < raw.size()) {
        DWORD chunk = 0;
        const DWORD want = static_cast<DWORD>((std::min)(raw.size() - read_total, static_cast<size_t>(1u << 20)));
        if (!ReadFile(handle, raw.data() + read_total, want, &chunk, nullptr)) {
            if (error) *error = Fail(L"Cannot read file", GetLastError());
            CloseHandle(handle);
            return false;
        }
        if (chunk == 0) break;
        read_total += chunk;
    }
    raw.resize(read_total);
    CloseHandle(handle);

    std::string_view body(raw);
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
        static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF) {
        body.remove_prefix(3);
    }
    *out = str::FromUtf8(body);
    return true;
}

bool WriteTextAtomic(std::wstring_view path, std::wstring_view text, std::wstring* error) {
    const std::wstring target(path);
    const std::wstring parent = paths::Parent(target);
    if (!parent.empty() && !paths::EnsureDirectory(parent)) {
        if (error) *error = L"Cannot create folder: " + parent;
        return false;
    }

    const std::wstring temp = target + L".tmp";
    const std::string utf8 = str::ToUtf8(text);

    HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) *error = Fail(L"Cannot create temporary file", GetLastError());
        return false;
    }
    if (!WriteUtf8(handle, utf8, error, L"Cannot write temporary file")) {
        CloseHandle(handle);
        DeleteFileW(temp.c_str());
        return false;
    }
    FlushFileBuffers(handle);
    CloseHandle(handle);

    if (paths::FileExists(target)) {
        if (!ReplaceFileW(target.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            const DWORD code = GetLastError();
            if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                if (error) *error = Fail(L"Cannot replace file", code);
                DeleteFileW(temp.c_str());
                return false;
            }
        }
    } else if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        if (error) *error = Fail(L"Cannot move file into place", GetLastError());
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

// Creates the file or fails. CREATE_NEW means an existing target is never
// touched: no replace, no rename, no delete, so a file already open in an
// editor keeps its identity.
bool WriteTextNew(std::wstring_view path, std::wstring_view text, std::wstring* error) {
    const std::wstring target(path);
    const std::wstring parent = paths::Parent(target);
    if (!parent.empty() && !paths::EnsureDirectory(parent)) {
        if (error) *error = L"Cannot create folder: " + parent;
        return false;
    }

    HANDLE handle =
        CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (error) {
            *error = (code == ERROR_FILE_EXISTS) ? L"The file already exists." : Fail(L"Cannot create file", code);
        }
        return false;
    }

    const std::string utf8 = str::ToUtf8(text);
    if (!WriteUtf8(handle, utf8, error, L"Cannot write file")) {
        CloseHandle(handle);
        return false;
    }
    FlushFileBuffers(handle);
    CloseHandle(handle);
    return true;
}

bool WriteTextInPlace(std::wstring_view path, std::wstring_view text, std::wstring* error) {
    const std::wstring target(path);
    const std::wstring parent = paths::Parent(target);
    if (!parent.empty() && !paths::EnsureDirectory(parent)) {
        if (error) *error = L"Cannot create folder: " + parent;
        return false;
    }

    // OPEN_ALWAYS preserves an existing file object and creates the first file
    // when there is none. FILE_SHARE_READ keeps an external editor able to read
    // while this handle is open; no replace/rename/delete occurs.
    HANDLE handle = CreateFileW(target.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) *error = Fail(L"Cannot open file for writing", GetLastError());
        return false;
    }
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(handle, start, nullptr, FILE_BEGIN)) {
        if (error) *error = Fail(L"Cannot seek file", GetLastError());
        CloseHandle(handle);
        return false;
    }

    const std::string utf8 = str::ToUtf8(text);
    if (!WriteUtf8(handle, utf8, error, L"Cannot write file")) {
        CloseHandle(handle);
        return false;
    }
    // The new JSON can be shorter than the old one. Without this, stale trailing
    // bytes remain and make the next parse fail despite a successful write.
    if (!SetEndOfFile(handle)) {
        if (error) *error = Fail(L"Cannot truncate file", GetLastError());
        CloseHandle(handle);
        return false;
    }
    if (!FlushFileBuffers(handle)) {
        if (error) *error = Fail(L"Cannot flush file", GetLastError());
        CloseHandle(handle);
        return false;
    }
    CloseHandle(handle);
    return true;
}

std::wstring BackupPath(std::wstring_view path) { return std::wstring(path) + L".otn.bak"; }

bool MakeBackup(std::wstring_view path, std::wstring* error) {
    if (!paths::FileExists(path)) return true;
    const std::wstring source(path);
    const std::wstring backup = BackupPath(path);
    if (!CopyFileW(source.c_str(), backup.c_str(), FALSE)) {
        if (error) *error = Fail(L"Cannot create backup", GetLastError());
        return false;
    }
    return true;
}

bool HasBackup(std::wstring_view path) { return paths::FileExists(BackupPath(path)); }

bool RestoreBackup(std::wstring_view path, std::wstring* error) {
    const std::wstring backup = BackupPath(path);
    if (!paths::FileExists(backup)) {
        if (error) *error = L"No backup exists yet.";
        return false;
    }
    std::wstring text;
    if (!ReadText(backup, &text, error)) return false;
    return WriteTextAtomic(path, text, error);
}

}  // namespace files
