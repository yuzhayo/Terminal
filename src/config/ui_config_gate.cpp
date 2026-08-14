#include "config/ui_config_gate.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "app/app_identity.h"
#include "resource.h"

namespace config {
namespace {

using Json = nlohmann::json;

constexpr DWORD kMaximumDocumentBytes = 4U * 1024U * 1024U;
constexpr int kMaximumNestingDepth = 64;
constexpr wchar_t kEmbeddedSource[] = L"Assets\\ui\\open-terminal-native.ui.default.v1.json";

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return L"Detail error tidak dapat dikonversi ke Unicode.";
    }

    std::wstring converted(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), converted.data(), required);
    if (written != required) {
        return L"Detail error tidak dapat dikonversi ke Unicode.";
    }
    return converted;
}

bool HasExactKeys(const Json& object, std::initializer_list<std::string_view> required,
                  std::string& error) {
    if (!object.is_object()) {
        error = "expected object";
        return false;
    }

    for (const auto& item : object.items()) {
        const bool known = std::any_of(required.begin(), required.end(), [&](std::string_view key) {
            return item.key() == key;
        });
        if (!known) {
            error = "unknown field: " + item.key();
            return false;
        }
    }

    for (const std::string_view key : required) {
        if (!object.contains(key)) {
            error = "missing field: " + std::string(key);
            return false;
        }
    }
    return true;
}

bool ReadInteger(const Json& object, std::string_view key, int& value, std::string& error) {
    const Json& field = object.at(key);
    if (!field.is_number_integer()) {
        error = std::string(key) + " must be an integer";
        return false;
    }
    value = field.get<int>();
    return true;
}

bool IsNumericIdentifier(std::string_view value) {
    if (value.empty() || (value.size() > 1 && value.front() == '0')) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](char character) {
        return character >= '0' && character <= '9';
    });
}

bool IsDiagnosticSemVer(std::string_view value) {
    const std::size_t suffix = value.find_first_of("-+");
    const std::string_view core = value.substr(0, suffix);
    const std::size_t first_dot = core.find('.');
    const std::size_t second_dot =
        first_dot == std::string_view::npos ? std::string_view::npos : core.find('.', first_dot + 1);
    if (first_dot == std::string_view::npos || second_dot == std::string_view::npos ||
        core.find('.', second_dot + 1) != std::string_view::npos ||
        !IsNumericIdentifier(core.substr(0, first_dot)) ||
        !IsNumericIdentifier(core.substr(first_dot + 1, second_dot - first_dot - 1)) ||
        !IsNumericIdentifier(core.substr(second_dot + 1))) {
        return false;
    }

    if (suffix == std::string_view::npos) {
        return true;
    }
    const std::string_view metadata = value.substr(suffix);
    if (metadata.size() < 2 || metadata.back() == '.' || metadata.back() == '-' || metadata.back() == '+') {
        return false;
    }
    return std::all_of(metadata.begin(), metadata.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= 'a' && character <= 'z') || character == '-' || character == '+' ||
               character == '.';
    });
}

bool ValidateAndReadMetadata(const Json& document, UiConfigMetadata& metadata, std::string& error) {
    if (!HasExactKeys(document,
                      {"schema", "version", "documentKind", "minimumReaderContract", "writtenBy",
                       "tokens", "styles", "windows", "screens"},
                      error)) {
        return false;
    }

    if (!document.at("schema").is_string() ||
        document.at("schema").get_ref<const std::string&>() != app_identity::kUiSchema) {
        error = "unsupported schema identity";
        return false;
    }

    if (!ReadInteger(document, "version", metadata.version, error) ||
        metadata.version != app_identity::kUiSchemaVersion) {
        error = "unsupported schema version";
        return false;
    }

    if (!document.at("documentKind").is_string() ||
        document.at("documentKind").get_ref<const std::string&>() != "default") {
        error = "embedded documentKind must be default";
        return false;
    }

    if (!ReadInteger(document, "minimumReaderContract", metadata.minimum_reader_contract, error) ||
        metadata.minimum_reader_contract <= 0 ||
        metadata.minimum_reader_contract > app_identity::kReaderContract) {
        error = "minimumReaderContract is incompatible";
        return false;
    }

    const Json& written_by = document.at("writtenBy");
    if (!HasExactKeys(written_by, {"appVersion", "configContract"}, error)) {
        error = "writtenBy: " + error;
        return false;
    }
    if (!written_by.at("appVersion").is_string()) {
        error = "writtenBy.appVersion must be a string";
        return false;
    }
    metadata.written_by_app_version = written_by.at("appVersion").get<std::string>();
    if (!IsDiagnosticSemVer(metadata.written_by_app_version)) {
        error = "writtenBy.appVersion must be SemVer";
        return false;
    }
    if (!ReadInteger(written_by, "configContract", metadata.written_by_config_contract, error) ||
        metadata.written_by_config_contract != app_identity::kWriterContract) {
        error = "embedded writtenBy.configContract does not match the binary";
        return false;
    }

    for (const char* section : {"tokens", "styles", "windows", "screens"}) {
        if (!document.at(section).is_object()) {
            error = std::string(section) + " must be an object";
            return false;
        }
    }

    metadata.schema = document.at("schema").get<std::string>();
    return true;
}

Json ParseDocument(const char* bytes, std::size_t size) {
    std::vector<std::unordered_set<std::string>> object_keys;
    const auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
        if (depth > kMaximumNestingDepth) {
            throw std::runtime_error("maximum nesting depth exceeded");
        }

        switch (event) {
            case Json::parse_event_t::object_start:
                object_keys.emplace_back();
                break;
            case Json::parse_event_t::key: {
                if (object_keys.empty()) {
                    throw std::runtime_error("key outside object");
                }
                const std::string& key = parsed.get_ref<const std::string&>();
                if (!object_keys.back().insert(key).second) {
                    throw std::runtime_error("duplicate key: " + key);
                }
                break;
            }
            case Json::parse_event_t::object_end:
                if (!object_keys.empty()) {
                    object_keys.pop_back();
                }
                break;
            default:
                break;
        }
        return true;
    };

    return Json::parse(bytes, bytes + size, callback, true, false);
}

}  // namespace

UiConfigGate::UiConfigGate(HINSTANCE instance, platform::AppPaths paths)
    : instance_(instance), paths_(std::move(paths)) {}

bool UiConfigGate::ResolveBootstrap(std::wstring& diagnostic) {
    const HRSRC resource = FindResourceW(instance_, MAKEINTRESOURCEW(IDR_UI_DEFAULT_JSON), RT_RCDATA);
    if (!resource) {
        diagnostic = std::wstring(L"Embedded UI default tidak ditemukan.\n\nSource: ") + kEmbeddedSource;
        return false;
    }

    const DWORD size = SizeofResource(instance_, resource);
    if (size == 0 || size > kMaximumDocumentBytes) {
        diagnostic = std::wstring(L"Ukuran embedded UI default tidak valid.\n\nSource: ") + kEmbeddedSource;
        return false;
    }

    const HGLOBAL loaded = LoadResource(instance_, resource);
    const auto* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    if (!bytes) {
        diagnostic = std::wstring(L"Embedded UI default tidak dapat dibaca.\n\nSource: ") + kEmbeddedSource;
        return false;
    }

    try {
        const Json document = ParseDocument(bytes, size);
        std::string error;
        UiConfigMetadata candidate;
        if (!ValidateAndReadMetadata(document, candidate, error)) {
            diagnostic = std::wstring(L"Embedded UI default tidak valid.\n\nSource: ") + kEmbeddedSource +
                         L"\nError: " + Utf8ToWide(error);
            return false;
        }
        metadata_ = std::move(candidate);
        diagnostic.clear();
        return true;
    } catch (const std::exception& error) {
        diagnostic = std::wstring(L"Embedded UI default tidak valid.\n\nSource: ") + kEmbeddedSource +
                     L"\nError: " + Utf8ToWide(error.what());
        return false;
    }
}

const UiConfigMetadata& UiConfigGate::metadata() const noexcept {
    return metadata_;
}

const platform::AppPaths& UiConfigGate::paths() const noexcept {
    return paths_;
}

}  // namespace config
