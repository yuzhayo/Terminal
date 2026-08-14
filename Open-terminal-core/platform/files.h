// File IO with validate-then-atomic-replace semantics.
#pragma once
#include <string>
#include <string_view>

namespace files {

// Reads a whole file as UTF-8 text. Strips a UTF-8 BOM when present.
bool ReadText(std::wstring_view path, std::wstring* out, std::wstring* error);

// Writes to "<path>.tmp" then replaces the target in one step.
bool WriteTextAtomic(std::wstring_view path, std::wstring_view text, std::wstring* error);

// Creates `path` and writes `text`. Fails when the file already exists, so an
// existing file can never be replaced, renamed, or deleted through this call.
bool WriteTextNew(std::wstring_view path, std::wstring_view text, std::wstring* error);

// Overwrites the bytes of an existing file through its current file object, then
// truncates and flushes it. Unlike WriteTextAtomic, this never replaces, renames,
// or deletes the target, so an external editor holding ui.json keeps the same
// file identity. Creates the file when it does not exist yet.
bool WriteTextInPlace(std::wstring_view path, std::wstring_view text, std::wstring* error);

// Backup path used before every destructive write.
std::wstring BackupPath(std::wstring_view path);

// Copies path -> BackupPath(path). Missing source is not an error.
bool MakeBackup(std::wstring_view path, std::wstring* error);
bool HasBackup(std::wstring_view path);
bool RestoreBackup(std::wstring_view path, std::wstring* error);

}  // namespace files
