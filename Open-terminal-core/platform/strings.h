// Small string helpers shared by every layer.
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace str {

std::wstring FromUtf8(std::string_view utf8);
std::string ToUtf8(std::wstring_view text);

std::wstring Trim(std::wstring_view text);
std::vector<std::wstring> SplitLines(std::wstring_view text);

bool IEquals(std::wstring_view a, std::wstring_view b);
bool StartsWith(std::wstring_view text, std::wstring_view prefix);
bool IContains(std::wstring_view text, std::wstring_view needle);

// Wraps an argument so CommandLineToArgvW-style parsers see it as one token.
std::wstring QuoteArg(std::wstring_view value);

// Escapes a value for embedding inside a PowerShell single-quoted string.
std::wstring EscapePowerShellSingleQuoted(std::wstring_view value);

// Escapes a value for embedding inside a POSIX shell single-quoted string.
std::wstring EscapePosixSingleQuoted(std::wstring_view value);

}  // namespace str
