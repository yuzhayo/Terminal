#include "platform/strings.h"

#include <windows.h>

#include <algorithm>

namespace str {

std::wstring FromUtf8(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Trim(std::wstring_view text) {
    size_t begin = 0;
    size_t end = text.size();
    const auto is_space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    while (begin < end && is_space(text[begin])) ++begin;
    while (end > begin && is_space(text[end - 1])) --end;
    return std::wstring(text.substr(begin, end - begin));
}

std::vector<std::wstring> SplitLines(std::wstring_view text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n') {
            std::wstring_view line = text.substr(start, i - start);
            if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
            lines.emplace_back(line);
            start = i + 1;
        }
    }
    return lines;
}

bool IEquals(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) ==
           CSTR_EQUAL;
}

bool StartsWith(std::wstring_view text, std::wstring_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool IContains(std::wstring_view text, std::wstring_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > text.size()) return false;
    for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
        if (IEquals(text.substr(i, needle.size()), needle)) return true;
    }
    return false;
}

std::wstring QuoteArg(std::wstring_view value) {
    const bool needs_quotes =
        value.empty() || value.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
    if (!needs_quotes) return std::wstring(value);

    std::wstring out;
    out.reserve(value.size() + 2);
    out.push_back(L'"');
    for (size_t i = 0; i < value.size(); ++i) {
        size_t backslashes = 0;
        while (i < value.size() && value[i] == L'\\') {
            ++backslashes;
            ++i;
        }
        if (i == value.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (value[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out.push_back(value[i]);
    }
    out.push_back(L'"');
    return out;
}

std::wstring EscapePowerShellSingleQuoted(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t c : value) {
        out.push_back(c);
        if (c == L'\'') out.push_back(L'\'');
    }
    return out;
}

std::wstring EscapePosixSingleQuoted(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t c : value) {
        if (c == L'\'') {
            out.append(L"'\\''");
        } else {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace str
