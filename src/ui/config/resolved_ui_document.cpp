#include "ui/config/resolved_ui_document.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "app/app_identity.h"

namespace ui::config {
namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumDocumentBytes = 4U * 1024U * 1024U;
constexpr int kMaximumNestingDepth = 64;

class ValidationError final : public std::runtime_error {
public:
    ValidationError(std::string code, std::string path, std::string message,
                    bool rollback_incompatible = false)
        : std::runtime_error(std::move(message)), code_(std::move(code)), path_(std::move(path)),
          rollback_incompatible_(rollback_incompatible) {}

    const std::string& code() const noexcept { return code_; }
    const std::string& path() const noexcept { return path_; }
    bool rollback_incompatible() const noexcept { return rollback_incompatible_; }

private:
    std::string code_;
    std::string path_;
    bool rollback_incompatible_ = false;
};

[[noreturn]] void Fail(std::string code, std::string path, std::string message,
                       bool rollback_incompatible = false) {
    throw ValidationError(std::move(code), std::move(path), std::move(message),
                          rollback_incompatible);
}

std::string ChildPath(std::string_view path, std::string_view child) {
    std::string result(path);
    result.push_back('/');
    for (const char character : child) {
        if (character == '~') {
            result += "~0";
        } else if (character == '/') {
            result += "~1";
        } else {
            result.push_back(character);
        }
    }
    return result;
}

bool IsLowerKebab(std::string_view value) {
    if (value.empty() || value.front() == '-' || value.back() == '-') {
        return false;
    }
    bool previous_dash = false;
    for (const char character : value) {
        if (character == '-') {
            if (previous_dash) {
                return false;
            }
            previous_dash = true;
        } else if ((character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9')) {
            previous_dash = false;
        } else {
            return false;
        }
    }
    return true;
}

bool IsLowerCamelPath(std::string_view value) {
    if (value.empty() || value.front() < 'a' || value.front() > 'z') {
        return false;
    }
    bool segment_start = false;
    for (const char character : value) {
        if (character == '.') {
            if (segment_start) {
                return false;
            }
            segment_start = true;
        } else if ((character >= 'a' && character <= 'z') ||
                   (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9')) {
            segment_start = false;
        } else {
            return false;
        }
    }
    return !segment_start;
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
    return metadata.size() >= 2 && metadata.back() != '.' && metadata.back() != '-' &&
           metadata.back() != '+' &&
           std::all_of(metadata.begin(), metadata.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= 'a' && character <= 'z') || character == '-' ||
                      character == '+' || character == '.';
           });
}

void RequireObject(const Json& value, std::string_view path) {
    if (!value.is_object()) {
        Fail("wrong-type", std::string(path), "Expected object.");
    }
}

void RequireExactKeys(const Json& object, std::initializer_list<std::string_view> required,
                      std::initializer_list<std::string_view> optional, std::string_view path) {
    RequireObject(object, path);
    for (const auto& item : object.items()) {
        const auto is_key = [&](std::string_view key) { return item.key() == key; };
        if (std::none_of(required.begin(), required.end(), is_key) &&
            std::none_of(optional.begin(), optional.end(), is_key)) {
            Fail("unknown-field", ChildPath(path, item.key()), "Unknown field.");
        }
    }
    for (const std::string_view key : required) {
        if (!object.contains(key)) {
            Fail("missing-field", ChildPath(path, key), "Required field is missing.");
        }
    }
}

const Json& Required(const Json& object, std::string_view key, std::string_view path) {
    if (!object.contains(key)) {
        Fail("missing-field", ChildPath(path, key), "Required field is missing.");
    }
    return object.at(key);
}

std::string ReadString(const Json& object, std::string_view key, std::string_view path) {
    const Json& value = Required(object, key, path);
    if (!value.is_string()) {
        Fail("wrong-type", ChildPath(path, key), "Expected string.");
    }
    return value.get<std::string>();
}

std::string ReadStringOr(const Json& object, std::string_view key, std::string default_value,
                         std::string_view path) {
    if (!object.contains(key)) {
        return default_value;
    }
    return ReadString(object, key, path);
}

bool ReadBoolOr(const Json& object, std::string_view key, bool default_value,
                std::string_view path) {
    if (!object.contains(key)) {
        return default_value;
    }
    const Json& value = object.at(key);
    if (!value.is_boolean()) {
        Fail("wrong-type", ChildPath(path, key), "Expected Boolean.");
    }
    return value.get<bool>();
}

int ReadIntegerOr(const Json& object, std::string_view key, int default_value, int minimum,
                  int maximum, std::string_view path) {
    if (!object.contains(key)) {
        return default_value;
    }
    const Json& value = object.at(key);
    if (!value.is_number_integer()) {
        Fail("wrong-type", ChildPath(path, key), "Expected integer.");
    }
    const auto number = value.get<std::int64_t>();
    if (number < minimum || number > maximum) {
        Fail("out-of-range", ChildPath(path, key), "Integer is outside the allowed range.");
    }
    return static_cast<int>(number);
}

Json ParseDocument(std::string_view bytes, std::string_view source) {
    if (bytes.empty() || bytes.size() > kMaximumDocumentBytes) {
        Fail("document-size", std::string(source), "Document size must be between 1 byte and 4 MiB.");
    }

    std::vector<std::unordered_set<std::string>> object_keys;
    const auto callback = [&](int depth, Json::parse_event_t event, Json& parsed) {
        if (depth > kMaximumNestingDepth) {
            throw ValidationError("nesting-depth", std::string(source),
                                  "Maximum JSON nesting depth exceeded.");
        }
        if (event == Json::parse_event_t::object_start) {
            object_keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            if (object_keys.empty()) {
                throw ValidationError("parse-error", std::string(source), "Key outside object.");
            }
            const std::string& key = parsed.get_ref<const std::string&>();
            if (!object_keys.back().insert(key).second) {
                throw ValidationError("duplicate-key", std::string(source),
                                      "Duplicate JSON key: " + key);
            }
        } else if (event == Json::parse_event_t::object_end && !object_keys.empty()) {
            object_keys.pop_back();
        }
        return true;
    };

    try {
        return Json::parse(bytes.begin(), bytes.end(), callback, true, false);
    } catch (const ValidationError&) {
        throw;
    } catch (const Json::parse_error& error) {
        Fail("parse-error", std::string(source), error.what());
    }
}

UiConfigMetadata ValidateMetadata(const Json& document, std::string_view expected_kind,
                                  std::string_view source) {
    RequireExactKeys(document,
                     {"schema", "version", "documentKind", "minimumReaderContract", "writtenBy",
                      "tokens", "styles", "windows", "screens"},
                     {}, source);

    UiConfigMetadata metadata;
    metadata.schema = ReadString(document, "schema", source);
    if (metadata.schema != app_identity::kUiSchema) {
        Fail("unsupported-schema", ChildPath(source, "schema"), "Unsupported schema identity.");
    }
    metadata.version = ReadIntegerOr(document, "version", -1, 1,
                                     std::numeric_limits<int>::max(), source);
    if (metadata.version != app_identity::kUiSchemaVersion) {
        Fail("unsupported-version", ChildPath(source, "version"), "Unsupported schema version.");
    }
    if (ReadString(document, "documentKind", source) != expected_kind) {
        Fail("document-kind", ChildPath(source, "documentKind"),
             "Unexpected documentKind.");
    }
    metadata.minimum_reader_contract = ReadIntegerOr(
        document, "minimumReaderContract", -1, 1, std::numeric_limits<int>::max(), source);
    if (metadata.minimum_reader_contract > app_identity::kReaderContract) {
        Fail("reader-contract", ChildPath(source, "minimumReaderContract"),
             "Document requires a newer reader contract.", true);
    }

    const Json& written_by = Required(document, "writtenBy", source);
    const std::string written_path = ChildPath(source, "writtenBy");
    RequireExactKeys(written_by, {"appVersion", "configContract"}, {}, written_path);
    metadata.written_by_app_version = ReadString(written_by, "appVersion", written_path);
    if (!IsDiagnosticSemVer(metadata.written_by_app_version)) {
        Fail("invalid-semver", ChildPath(written_path, "appVersion"),
             "writtenBy.appVersion must be SemVer.");
    }
    metadata.written_by_config_contract = ReadIntegerOr(
        written_by, "configContract", -1, 1, std::numeric_limits<int>::max(), written_path);
    if (expected_kind == "default" &&
        metadata.written_by_config_contract != app_identity::kWriterContract) {
        Fail("writer-contract", ChildPath(written_path, "configContract"),
             "Embedded configContract does not match the binary writer contract.");
    }
    return metadata;
}

void MergeObject(Json& target, const Json& patch) {
    for (const auto& item : patch.items()) {
        if (target.contains(item.key()) && target.at(item.key()).is_object() && item.value().is_object()) {
            MergeObject(target[item.key()], item.value());
        } else {
            target[item.key()] = item.value();
        }
    }
}

Json MergeDocuments(const Json& embedded, const Json& override_document) {
    Json merged = embedded;
    for (const char* section : {"tokens", "styles", "windows", "screens"}) {
        const Json& patch = override_document.at(section);
        RequireObject(patch, std::string("override/") + section);
        MergeObject(merged[section], patch);
    }
    return merged;
}

int HexDigit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

LiteralRgba ParseLiteralColor(std::string_view text, std::string_view path) {
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#') {
        Fail("invalid-color", std::string(path), "Color must use #RRGGBB or #RRGGBBAA.");
    }
    std::array<std::uint8_t, 4> channels{0, 0, 0, 255};
    const std::size_t channel_count = text.size() == 9 ? 4 : 3;
    for (std::size_t index = 0; index < channel_count; ++index) {
        const int high = HexDigit(text[1 + index * 2]);
        const int low = HexDigit(text[2 + index * 2]);
        if (high < 0 || low < 0) {
            Fail("invalid-color", std::string(path), "Color contains a non-hex digit.");
        }
        channels[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return {channels[0], channels[1], channels[2], channels[3]};
}

SystemColorSlot ParseSystemColor(std::string_view value, std::string_view path) {
    if (value == "window") return SystemColorSlot::Window;
    if (value == "windowText") return SystemColorSlot::WindowText;
    if (value == "grayText") return SystemColorSlot::GrayText;
    if (value == "highlight") return SystemColorSlot::Highlight;
    if (value == "highlightText") return SystemColorSlot::HighlightText;
    Fail("invalid-system-color", std::string(path), "Unknown system color slot.");
}

std::string ReadReference(const Json& value, std::string_view prefix, std::string_view path) {
    RequireExactKeys(value, {"$ref"}, {}, path);
    const std::string reference = ReadString(value, "$ref", path);
    const std::string expected(prefix);
    if (!reference.starts_with(expected) || reference.size() == expected.size()) {
        Fail("invalid-reference", std::string(path), "Reference has the wrong namespace.");
    }
    return reference.substr(expected.size());
}

Insets ParseInsets(const Json& value, std::string_view path);

class ThemeResolver final {
public:
    ThemeResolver(ThemeKind kind, const Json& tokens, const Json& styles, std::string path)
        : kind_(kind), token_json_(tokens), styles_json_(styles), path_(std::move(path)) {}

    ResolvedTheme Resolve() {
        RequireObject(token_json_, ChildPath(path_, "tokens"));
        RequireObject(styles_json_, ChildPath(path_, "styles"));
        ValidateRequiredTokens();
        for (const auto& item : token_json_.items()) {
            if (!IsLowerCamelPath(item.key())) {
                Fail("invalid-token-id", ChildPath(ChildPath(path_, "tokens"), item.key()),
                     "Token ID must be a lowerCamelCase path.");
            }
            ResolveToken(item.key());
        }
        if (kind_ == ThemeKind::HighContrast) {
            ValidateHighContrastMappings();
        }

        ResolvedTheme theme;
        theme.kind = kind_;
        theme.tokens = resolved_tokens_;
        for (const auto& item : styles_json_.items()) {
            if (!IsLowerKebab(item.key())) {
                Fail("invalid-style-id", ChildPath(ChildPath(path_, "styles"), item.key()),
                     "Style ID must be lower-kebab-case.");
            }
            theme.styles.push_back(ResolveStyle(item.key(), item.value()));
        }
        std::sort(theme.styles.begin(), theme.styles.end(),
                  [](const ResolvedStyle& left, const ResolvedStyle& right) {
                      return left.id < right.id;
                  });
        return theme;
    }

private:
    void ValidateHighContrastMappings() const {
        static constexpr std::array mappings = {
            std::pair{"window", SystemColorSlot::Window},
            std::pair{"surface", SystemColorSlot::Window},
            std::pair{"surfaceAlt", SystemColorSlot::Window},
            std::pair{"input", SystemColorSlot::Window},
            std::pair{"scrim", SystemColorSlot::Window},
            std::pair{"transparent", SystemColorSlot::Window},
            std::pair{"text", SystemColorSlot::WindowText},
            std::pair{"textDim", SystemColorSlot::WindowText},
            std::pair{"border", SystemColorSlot::WindowText},
            std::pair{"borderStrong", SystemColorSlot::WindowText},
            std::pair{"textMuted", SystemColorSlot::GrayText},
            std::pair{"accent", SystemColorSlot::Highlight},
            std::pair{"accentHover", SystemColorSlot::Highlight},
            std::pair{"accentPressed", SystemColorSlot::Highlight},
            std::pair{"selection", SystemColorSlot::Highlight},
            std::pair{"danger", SystemColorSlot::Highlight},
            std::pair{"success", SystemColorSlot::Highlight},
            std::pair{"accentText", SystemColorSlot::HighlightText},
        };
        for (const auto& [name, expected] : mappings) {
            const auto found = resolved_tokens_.find(name);
            const auto* actual = found == resolved_tokens_.end()
                                     ? nullptr
                                     : std::get_if<SystemColorSlot>(&found->second);
            if (!actual || *actual != expected) {
                Fail("high-contrast-mapping",
                     ChildPath(ChildPath(path_, "tokens"), name),
                     "High Contrast semantic token uses the wrong system color slot.");
            }
        }
    }

    void ValidateRequiredTokens() const {
        static constexpr std::array required = {
            "window",       "surface",       "surfaceAlt", "border",  "borderStrong",
            "text",         "textDim",       "textMuted",  "accent",  "accentHover",
            "accentPressed", "accentText",    "danger",     "success", "input",
            "selection",    "scrim",
        };
        for (const char* token : required) {
            if (!token_json_.contains(token)) {
                Fail("missing-token", ChildPath(ChildPath(path_, "tokens"), token),
                     "Required semantic token is missing.");
            }
        }
    }

    ResolvedColor ResolveToken(const std::string& name) {
        if (const auto found = resolved_tokens_.find(name); found != resolved_tokens_.end()) {
            return found->second;
        }
        if (!token_json_.contains(name)) {
            Fail("missing-reference", ChildPath(ChildPath(path_, "tokens"), name),
                 "Referenced token does not exist.");
        }
        if (!resolving_.insert(name).second) {
            Fail("reference-cycle", ChildPath(ChildPath(path_, "tokens"), name),
                 "Token reference cycle detected.");
        }

        const Json& value = token_json_.at(name);
        const std::string value_path = ChildPath(ChildPath(path_, "tokens"), name);
        ResolvedColor resolved;
        if (value.is_string()) {
            if (kind_ == ThemeKind::HighContrast) {
                Fail("high-contrast-literal", value_path,
                     "High Contrast tokens must remain symbolic system colors.");
            }
            resolved = ParseLiteralColor(value.get_ref<const std::string&>(), value_path);
        } else if (value.is_object() && value.contains("$ref")) {
            resolved = ResolveToken(ReadReference(value, "tokens.", value_path));
        } else if (value.is_object() && value.contains("$systemColor")) {
            if (kind_ != ThemeKind::HighContrast) {
                Fail("system-color-theme", value_path,
                     "System color slots are only valid in High Contrast tokens.");
            }
            RequireExactKeys(value, {"$systemColor"}, {}, value_path);
            resolved = ParseSystemColor(ReadString(value, "$systemColor", value_path), value_path);
        } else {
            Fail("wrong-type", value_path, "Token must be a color, token reference, or system color.");
        }
        resolving_.erase(name);
        resolved_tokens_.emplace(name, resolved);
        return resolved;
    }

    ResolvedColor ResolveStyleColor(const Json& value, std::string_view path) {
        if (value.is_string()) {
            if (kind_ == ThemeKind::HighContrast) {
                Fail("high-contrast-literal", std::string(path),
                     "High Contrast styles must use symbolic token references.");
            }
            return ParseLiteralColor(value.get_ref<const std::string&>(), path);
        }
        return ResolveToken(ReadReference(value, "tokens.", path));
    }

    ResolvedStyle ResolveStyle(const std::string& id, const Json& value) {
        const std::string style_path = ChildPath(ChildPath(path_, "styles"), id);
        RequireExactKeys(value,
                         {"font", "minimumHeight", "contentPadding", "radius", "borderWidth",
                          "focusWidth", "states"},
                         {}, style_path);
        ResolvedStyle style;
        style.id = id;
        const Json& font = value.at("font");
        const std::string font_path = ChildPath(style_path, "font");
        RequireExactKeys(font, {"family", "fallbackFamily", "pointSize", "weight"}, {},
                         font_path);
        style.font.family = ReadString(font, "family", font_path);
        style.font.fallback_family = ReadString(font, "fallbackFamily", font_path);
        if (style.font.family.empty()) {
            Fail("font-family", ChildPath(font_path, "family"), "Font family cannot be empty.");
        }
        style.font.point_size = ReadIntegerOr(font, "pointSize", 9, 1, 96, font_path);
        style.font.weight = ReadIntegerOr(font, "weight", 400, 100, 900, font_path);
        style.minimum_height = ReadIntegerOr(value, "minimumHeight", 0, 0, 8192, style_path);
        style.content_padding = ParseInsets(value.at("contentPadding"),
                                            ChildPath(style_path, "contentPadding"));
        style.radius = ReadIntegerOr(value, "radius", 0, 0, 64, style_path);
        style.border_width = ReadIntegerOr(value, "borderWidth", 1, 0, 8, style_path);
        style.focus_width = ReadIntegerOr(value, "focusWidth", 2, 1, 8, style_path);

        const Json& states = value.at("states");
        const std::string states_path = ChildPath(style_path, "states");
        RequireExactKeys(states, {"normal", "hover", "pressed", "selected", "disabled", "focus"},
                         {}, states_path);
        static constexpr std::array names = {"normal", "hover", "pressed", "selected", "disabled",
                                              "focus"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            const Json& state = states.at(names[index]);
            const std::string state_path = ChildPath(states_path, names[index]);
            RequireExactKeys(state, {"background", "foreground", "border"}, {}, state_path);
            style.states[index] = {
                ResolveStyleColor(state.at("background"), ChildPath(state_path, "background")),
                ResolveStyleColor(state.at("foreground"), ChildPath(state_path, "foreground")),
                ResolveStyleColor(state.at("border"), ChildPath(state_path, "border")),
            };
        }
        return style;
    }

    ThemeKind kind_;
    const Json& token_json_;
    const Json& styles_json_;
    std::string path_;
    std::map<std::string, ResolvedColor, std::less<>> resolved_tokens_;
    std::unordered_set<std::string> resolving_;
};

template <typename Enum>
Enum ParseEnum(std::string_view value,
               std::initializer_list<std::pair<std::string_view, Enum>> choices,
               std::string_view path) {
    for (const auto& [name, parsed] : choices) {
        if (value == name) {
            return parsed;
        }
    }
    Fail("invalid-enum", std::string(path), "Value is not in the allowed enum.");
}

ValueBinding ParseBinding(const Json& value, std::string_view path) {
    RequireExactKeys(value, {"$bind"}, {}, path);
    const std::string binding = ReadString(value, "$bind", path);
    constexpr std::string_view prefix = "viewState.";
    if (!binding.starts_with(prefix) || !IsLowerCamelPath(binding.substr(prefix.size()))) {
        Fail("invalid-binding", std::string(path), "Binding must use viewState.<path>.");
    }
    return {binding.substr(prefix.size())};
}

TextValue ParseTextValue(const Json& value, std::string_view path) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return ParseBinding(value, path);
}

BooleanValue ParseBooleanValue(const Json& value, std::string_view path) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    return ParseBinding(value, path);
}

Dimension ParseDimension(const Json& value, std::string_view path) {
    if (value.is_string()) {
        const std::string& text = value.get_ref<const std::string&>();
        if (text == "auto") return {DimensionKind::Auto, 0};
        if (text == "fill") return {DimensionKind::Fill, 0};
        Fail("invalid-dimension", std::string(path), "Dimension string must be auto or fill.");
    }
    if (!value.is_number_integer()) {
        Fail("wrong-type", std::string(path), "Dimension must be auto, fill, or an integer.");
    }
    const auto number = value.get<std::int64_t>();
    if (number < 0 || number > 8192) {
        Fail("out-of-range", std::string(path), "Dimension is outside 0..8192.");
    }
    return {DimensionKind::Pixels, static_cast<int>(number)};
}

Insets ParseInsets(const Json& value, std::string_view path) {
    RequireExactKeys(value, {}, {"left", "top", "right", "bottom"}, path);
    return {
        ReadIntegerOr(value, "left", 0, 0, 256, path),
        ReadIntegerOr(value, "top", 0, 0, 256, path),
        ReadIntegerOr(value, "right", 0, 0, 256, path),
        ReadIntegerOr(value, "bottom", 0, 0, 256, path),
    };
}

LayoutDefinition ParseLayout(const Json& value, std::string_view path) {
    RequireExactKeys(value, {}, {"width", "height", "minWidth", "minHeight", "maxWidth",
                                 "maxHeight", "margin"}, path);
    LayoutDefinition layout;
    if (value.contains("width")) layout.width = ParseDimension(value.at("width"), ChildPath(path, "width"));
    if (value.contains("height")) layout.height = ParseDimension(value.at("height"), ChildPath(path, "height"));
    layout.minimum_width = ReadIntegerOr(value, "minWidth", 0, 0, 8192, path);
    layout.minimum_height = ReadIntegerOr(value, "minHeight", 0, 0, 8192, path);
    layout.maximum_width = ReadIntegerOr(value, "maxWidth", 8192, 0, 8192, path);
    layout.maximum_height = ReadIntegerOr(value, "maxHeight", 8192, 0, 8192, path);
    if (layout.minimum_width > layout.maximum_width || layout.minimum_height > layout.maximum_height) {
        Fail("invalid-layout-range", std::string(path), "Minimum layout size exceeds maximum size.");
    }
    if (value.contains("margin")) layout.margin = ParseInsets(value.at("margin"), ChildPath(path, "margin"));
    return layout;
}

AutomationDefinition ParseAutomation(const Json& value, std::string_view path) {
    RequireExactKeys(value, {}, {"name", "helpText", "live"}, path);
    AutomationDefinition automation;
    if (value.contains("name")) {
        const Json& name = value.at("name");
        if (name.is_string() && name.get_ref<const std::string&>() == "auto") {
            automation.automatic_name = true;
        } else {
            automation.automatic_name = false;
            const TextValue parsed = ParseTextValue(name, ChildPath(path, "name"));
            if (const auto* text = std::get_if<std::string>(&parsed)) {
                automation.name = *text;
            } else {
                automation.name = std::get<ValueBinding>(parsed);
            }
        }
    }
    automation.help_text = ReadStringOr(value, "helpText", "", path);
    automation.live = ParseEnum<AutomationLive>(
        ReadStringOr(value, "live", "off", path),
        {{"off", AutomationLive::Off}, {"polite", AutomationLive::Polite},
         {"assertive", AutomationLive::Assertive}},
        ChildPath(path, "live"));
    return automation;
}

EventPayloadValue ParsePayloadValue(const Json& value, std::string_view path, int depth) {
    if (depth > 16) {
        Fail("event-payload-depth", std::string(path), "Event payload nesting exceeds 16.");
    }
    EventPayloadValue result;
    if (value.is_null()) result.value = nullptr;
    else if (value.is_boolean()) result.value = value.get<bool>();
    else if (value.is_number_integer()) result.value = value.get<std::int64_t>();
    else if (value.is_number_float()) {
        const double number = value.get<double>();
        if (!std::isfinite(number)) Fail("non-finite-number", std::string(path), "Number must be finite.");
        result.value = number;
    } else if (value.is_string()) result.value = value.get<std::string>();
    else if (value.is_object() && value.contains("$bind")) result.value = ParseBinding(value, path);
    else if (value.is_object()) {
        EventPayloadValue::Object object;
        for (const auto& item : value.items()) {
            object.emplace(item.key(), ParsePayloadValue(item.value(), ChildPath(path, item.key()), depth + 1));
        }
        result.value = std::move(object);
    } else if (value.is_array()) {
        EventPayloadValue::Array array;
        for (std::size_t index = 0; index < value.size(); ++index) {
            array.push_back(ParsePayloadValue(value[index], ChildPath(path, std::to_string(index)), depth + 1));
        }
        result.value = std::move(array);
    } else {
        Fail("wrong-type", std::string(path), "Unsupported event payload value.");
    }
    return result;
}

std::map<std::string, EventDefinition, std::less<>> ParseEvents(
    const Json& value, const std::set<std::string, std::less<>>& allowed, std::string_view path) {
    RequireObject(value, path);
    std::map<std::string, EventDefinition, std::less<>> events;
    for (const auto& item : value.items()) {
        if (!allowed.contains(item.key())) {
            Fail("unsupported-event", ChildPath(path, item.key()), "Event is not allowed for this component.");
        }
        const std::string event_path = ChildPath(path, item.key());
        RequireExactKeys(item.value(), {"action", "payload"}, {}, event_path);
        EventDefinition event;
        event.action = ReadString(item.value(), "action", event_path);
        if (!IsLowerKebab(event.action)) {
            Fail("invalid-action", ChildPath(event_path, "action"), "Action must be lower-kebab-case.");
        }
        const Json& payload = item.value().at("payload");
        RequireObject(payload, ChildPath(event_path, "payload"));
        for (const auto& payload_item : payload.items()) {
            event.payload.emplace(payload_item.key(), ParsePayloadValue(
                payload_item.value(), ChildPath(ChildPath(event_path, "payload"), payload_item.key()), 0));
        }
        events.emplace(item.key(), std::move(event));
    }
    return events;
}

ComponentType ParseComponentType(std::string_view value, std::string_view path) {
    return ParseEnum<ComponentType>(
        value,
        {{"Window", ComponentType::Window}, {"Screen", ComponentType::Screen},
         {"Container", ComponentType::Container}, {"Text", ComponentType::Text},
         {"Button", ComponentType::Button}, {"Input", ComponentType::Input},
         {"Combo", ComponentType::Combo}, {"Checkbox", ComponentType::Checkbox},
         {"Toggle", ComponentType::Toggle}, {"Card", ComponentType::Card},
         {"List", ComponentType::List}, {"Scrollbar", ComponentType::Scrollbar},
         {"Dialog", ComponentType::Dialog}},
        path);
}

std::set<std::string_view> SpecificKeys(ComponentType type) {
    switch (type) {
        case ComponentType::Window: return {"title", "initialWidth", "initialHeight", "minWidth", "minHeight", "resizable", "children"};
        case ComponentType::Screen: return {"routeId", "children"};
        case ComponentType::Container: return {"direction", "gap", "padding", "align", "justify", "wrap", "overflow", "children"};
        case ComponentType::Text: return {"text", "textBinding", "variant", "wrap", "selectable", "align"};
        case ComponentType::Button: return {"label", "variant", "selected", "tabStop"};
        case ComponentType::Input: return {"valueBinding", "mode", "placeholder", "readOnly", "password", "maxLength", "horizontalAlign", "scrollbar", "tabStop"};
        case ComponentType::Combo: return {"itemsBinding", "selectedValueBinding", "placeholder", "maxVisibleItems", "popupMaxHeight", "allowEmpty", "tabStop"};
        case ComponentType::Checkbox: return {"label", "checkedBinding", "triState", "tabStop"};
        case ComponentType::Toggle: return {"label", "checkedBinding", "variant", "tabStop"};
        case ComponentType::Card: return {"interactive", "selected", "tabStop", "children"};
        case ComponentType::List: return {"itemsBinding", "itemTemplate", "selectedIdBinding", "rowHeight", "overscanRows", "selection", "emptyText", "scrollbar", "tabStop"};
        case ComponentType::Scrollbar: return {"orientation", "thickness", "minThumbLength", "lineStep", "pageStep"};
        case ComponentType::Dialog: return {"title", "modality", "width", "maxHeight", "dismissPolicy", "children"};
    }
    return {};
}

std::set<std::string, std::less<>> AllowedEvents(ComponentType type) {
    switch (type) {
        case ComponentType::Button: return {"click"};
        case ComponentType::Input: return {"blur", "changed", "commit", "focus"};
        case ComponentType::Combo: return {"changed", "closed", "opened"};
        case ComponentType::Checkbox:
        case ComponentType::Toggle: return {"changed"};
        case ComponentType::Card: return {"activate"};
        case ComponentType::List: return {"activate", "selectionChanged"};
        case ComponentType::Dialog: return {"accept", "cancel", "dismiss"};
        default: return {};
    }
}

class ComponentResolver final {
public:
    ComponentResolver(const std::vector<ResolvedStyle>& styles,
                      std::set<std::string, std::less<>> route_ids)
        : route_ids_(std::move(route_ids)) {
        for (std::size_t index = 0; index < styles.size(); ++index) {
            style_indices_.emplace(styles[index].id, index);
        }
    }

    ResolvedComponent Resolve(const Json& value, std::string path) {
        return ResolveInternal(value, std::move(path), 0);
    }

private:
    ResolvedComponent ResolveInternal(const Json& value, std::string path, int depth) {
        if (depth > kMaximumNestingDepth) {
            Fail("component-depth", path, "Component nesting exceeds 64.");
        }
        RequireObject(value, path);
        const ComponentType type = ParseComponentType(ReadString(value, "type", path), ChildPath(path, "type"));

        static const std::set<std::string_view> common = {"id", "type", "visible", "enabled", "style", "layout", "automation", "events"};
        const auto specific = SpecificKeys(type);
        for (const auto& item : value.items()) {
            if (!common.contains(item.key()) && !specific.contains(item.key())) {
                Fail("unknown-field", ChildPath(path, item.key()), "Unknown component field.");
            }
        }

        ResolvedComponent component;
        component.id = ReadString(value, "id", path);
        if (!IsLowerKebab(component.id)) {
            Fail("invalid-component-id", ChildPath(path, "id"), "Component ID must be lower-kebab-case.");
        }
        if (!ids_.insert(component.id).second) {
            Fail("duplicate-component-id", ChildPath(path, "id"), "Component ID is duplicated in this tree.");
        }
        component.type = type;
        component.visible = ReadBoolOr(value, "visible", true, path);
        component.enabled = ReadBoolOr(value, "enabled", true, path);

        const Json& style = Required(value, "style", path);
        component.style_id = ReadReference(style, "styles.", ChildPath(path, "style"));
        const auto style_found = style_indices_.find(component.style_id);
        if (style_found == style_indices_.end()) {
            Fail("missing-reference", ChildPath(path, "style"), "Referenced style does not exist.");
        }
        component.style_index = style_found->second;
        if (value.contains("layout")) component.layout = ParseLayout(value.at("layout"), ChildPath(path, "layout"));
        if (value.contains("automation")) component.automation = ParseAutomation(value.at("automation"), ChildPath(path, "automation"));
        if (value.contains("events")) component.events = ParseEvents(value.at("events"), AllowedEvents(type), ChildPath(path, "events"));

        component.properties = ResolveProperties(component, value, path, depth);
        if (specific.contains("children")) {
            const Json& children = value.contains("children") ? value.at("children") : Json::array();
            if (!children.is_array()) Fail("wrong-type", ChildPath(path, "children"), "children must be an array.");
            for (std::size_t index = 0; index < children.size(); ++index) {
                component.children.push_back(ResolveInternal(children[index], ChildPath(ChildPath(path, "children"), std::to_string(index)), depth + 1));
            }
        }
        ValidateAutomationName(component, path);
        return component;
    }

    ComponentProperties ResolveProperties(ResolvedComponent& component, const Json& value,
                                          const std::string& path, int depth) {
        switch (component.type) {
            case ComponentType::Window: {
                WindowProperties properties;
                properties.title = ParseTextValue(Required(value, "title", path), ChildPath(path, "title"));
                properties.initial_width = ReadIntegerOr(value, "initialWidth", 760, 1, 8192, path);
                properties.initial_height = ReadIntegerOr(value, "initialHeight", 520, 1, 8192, path);
                properties.minimum_width = ReadIntegerOr(value, "minWidth", 620, 0, 8192, path);
                properties.minimum_height = ReadIntegerOr(value, "minHeight", 420, 0, 8192, path);
                properties.resizable = ReadBoolOr(value, "resizable", true, path);
                return properties;
            }
            case ComponentType::Screen: {
                ScreenProperties properties{ReadString(value, "routeId", path)};
                if (!route_ids_.contains(properties.route_id)) {
                    Fail("invalid-route", ChildPath(path, "routeId"), "routeId is not in the V1 route inventory.");
                }
                return properties;
            }
            case ComponentType::Container: {
                ContainerProperties properties;
                properties.direction = ParseEnum<ContainerDirection>(ReadStringOr(value, "direction", "column", path),
                    {{"row", ContainerDirection::Row}, {"column", ContainerDirection::Column}, {"grid", ContainerDirection::Grid}, {"flow", ContainerDirection::Flow}}, ChildPath(path, "direction"));
                properties.gap = ReadIntegerOr(value, "gap", 8, 0, 128, path);
                if (value.contains("padding")) properties.padding = ParseInsets(value.at("padding"), ChildPath(path, "padding"));
                properties.align = ParseEnum<ContainerAlign>(ReadStringOr(value, "align", "stretch", path),
                    {{"start", ContainerAlign::Start}, {"center", ContainerAlign::Center}, {"end", ContainerAlign::End}, {"stretch", ContainerAlign::Stretch}}, ChildPath(path, "align"));
                properties.justify = ParseEnum<ContainerJustify>(ReadStringOr(value, "justify", "start", path),
                    {{"start", ContainerJustify::Start}, {"center", ContainerJustify::Center}, {"end", ContainerJustify::End}, {"spaceBetween", ContainerJustify::SpaceBetween}}, ChildPath(path, "justify"));
                properties.wrap = ReadBoolOr(value, "wrap", false, path);
                properties.overflow = ParseEnum<OverflowMode>(ReadStringOr(value, "overflow", "visible", path),
                    {{"visible", OverflowMode::Visible}, {"clip", OverflowMode::Clip}, {"scroll", OverflowMode::Scroll}}, ChildPath(path, "overflow"));
                return properties;
            }
            case ComponentType::Text: {
                const bool has_text = value.contains("text");
                const bool has_binding = value.contains("textBinding");
                if (has_text == has_binding) Fail("text-source", path, "Text requires exactly one of text or textBinding.");
                TextProperties properties;
                properties.text = has_text ? TextValue(ReadString(value, "text", path))
                                           : TextValue(ParseBinding(value.at("textBinding"), ChildPath(path, "textBinding")));
                properties.variant = ParseEnum<TextVariant>(ReadStringOr(value, "variant", "body", path),
                    {{"body", TextVariant::Body}, {"title", TextVariant::Title}, {"caption", TextVariant::Caption}, {"monospace", TextVariant::Monospace}}, ChildPath(path, "variant"));
                properties.wrap = ReadBoolOr(value, "wrap", true, path);
                properties.selectable = ReadBoolOr(value, "selectable", false, path);
                properties.align = ParseEnum<TextAlign>(ReadStringOr(value, "align", "start", path),
                    {{"start", TextAlign::Start}, {"center", TextAlign::Center}, {"end", TextAlign::End}}, ChildPath(path, "align"));
                return properties;
            }
            case ComponentType::Button: {
                ButtonProperties properties;
                properties.label = ParseTextValue(Required(value, "label", path), ChildPath(path, "label"));
                properties.variant = ParseEnum<ButtonVariant>(ReadStringOr(value, "variant", "default", path),
                    {{"default", ButtonVariant::Default}, {"primary", ButtonVariant::Primary}, {"subtle", ButtonVariant::Subtle}, {"danger", ButtonVariant::Danger}, {"navigation", ButtonVariant::Navigation}, {"bookmark", ButtonVariant::Bookmark}}, ChildPath(path, "variant"));
                if (value.contains("selected")) properties.selected = ParseBooleanValue(value.at("selected"), ChildPath(path, "selected"));
                properties.tab_stop = ReadBoolOr(value, "tabStop", true, path);
                return properties;
            }
            case ComponentType::Input: {
                InputProperties properties;
                properties.value_binding = ParseBinding(Required(value, "valueBinding", path), ChildPath(path, "valueBinding"));
                properties.mode = ParseEnum<InputMode>(ReadStringOr(value, "mode", "singleLine", path),
                    {{"singleLine", InputMode::SingleLine}, {"multiline", InputMode::Multiline}}, ChildPath(path, "mode"));
                properties.placeholder = ReadStringOr(value, "placeholder", "", path);
                properties.read_only = ReadBoolOr(value, "readOnly", false, path);
                properties.password = ReadBoolOr(value, "password", false, path);
                properties.maximum_length = ReadIntegerOr(value, "maxLength", 4096, 0, std::numeric_limits<int>::max(), path);
                properties.horizontal_align = ParseEnum<TextAlign>(ReadStringOr(value, "horizontalAlign", "start", path),
                    {{"start", TextAlign::Start}}, ChildPath(path, "horizontalAlign"));
                properties.scrollbar = ParseEnum<ScrollbarMode>(ReadStringOr(value, "scrollbar", "auto", path),
                    {{"auto", ScrollbarMode::Auto}, {"never", ScrollbarMode::Never}}, ChildPath(path, "scrollbar"));
                properties.tab_stop = ReadBoolOr(value, "tabStop", true, path);
                if (properties.password && properties.mode != InputMode::SingleLine) Fail("password-mode", ChildPath(path, "password"), "Password is only valid for singleLine Input.");
                if (properties.maximum_length == 0 && properties.mode != InputMode::Multiline) Fail("max-length", ChildPath(path, "maxLength"), "Unlimited maxLength is only valid for multiline Input.");
                return properties;
            }
            case ComponentType::Combo: {
                ComboProperties properties;
                properties.items_binding = ParseBinding(Required(value, "itemsBinding", path), ChildPath(path, "itemsBinding"));
                properties.selected_value_binding = ParseBinding(Required(value, "selectedValueBinding", path), ChildPath(path, "selectedValueBinding"));
                properties.placeholder = ReadStringOr(value, "placeholder", "", path);
                properties.maximum_visible_items = ReadIntegerOr(value, "maxVisibleItems", 10, 1, 50, path);
                properties.popup_maximum_height = ReadIntegerOr(value, "popupMaxHeight", 480, 64, 1024, path);
                properties.allow_empty = ReadBoolOr(value, "allowEmpty", true, path);
                properties.tab_stop = ReadBoolOr(value, "tabStop", true, path);
                return properties;
            }
            case ComponentType::Checkbox: {
                CheckboxProperties properties;
                properties.label = ParseTextValue(Required(value, "label", path), ChildPath(path, "label"));
                properties.checked_binding = ParseBinding(Required(value, "checkedBinding", path), ChildPath(path, "checkedBinding"));
                properties.tri_state = ReadBoolOr(value, "triState", false, path);
                properties.tab_stop = ReadBoolOr(value, "tabStop", true, path);
                return properties;
            }
            case ComponentType::Toggle: {
                ToggleProperties properties;
                properties.label = ParseTextValue(Required(value, "label", path), ChildPath(path, "label"));
                properties.checked_binding = ParseBinding(Required(value, "checkedBinding", path), ChildPath(path, "checkedBinding"));
                properties.variant = ReadStringOr(value, "variant", "default", path);
                if (properties.variant != "default") Fail("invalid-enum", ChildPath(path, "variant"), "Toggle variant must be default.");
                properties.tab_stop = ReadBoolOr(value, "tabStop", true, path);
                return properties;
            }
            case ComponentType::Card: {
                CardProperties properties;
                properties.interactive = ReadBoolOr(value, "interactive", false, path);
                if (value.contains("selected")) properties.selected = ParseBooleanValue(value.at("selected"), ChildPath(path, "selected"));
                properties.tab_stop = ReadBoolOr(value, "tabStop", properties.interactive, path);
                if (component.events.contains("activate") && !properties.interactive) Fail("card-event", ChildPath(path, "events/activate"), "Card activate requires interactive=true.");
                return properties;
            }
            case ComponentType::List: {
                ListProperties properties;
                properties.items_binding = ParseBinding(Required(value, "itemsBinding", path), ChildPath(path, "itemsBinding"));
                properties.item_template = std::make_shared<const ResolvedComponent>(ResolveInternal(Required(value, "itemTemplate", path), ChildPath(path, "itemTemplate"), depth + 1));
                if (value.contains("selectedIdBinding")) properties.selected_id_binding = ParseBinding(value.at("selectedIdBinding"), ChildPath(path, "selectedIdBinding"));
                properties.row_height = ReadIntegerOr(value, "rowHeight", 32, 20, 256, path);
                properties.overscan_rows = ReadIntegerOr(value, "overscanRows", 2, 0, 20, path);
                properties.selection = ParseEnum<SelectionMode>(ReadStringOr(value, "selection", "single", path),
                    {{"none", SelectionMode::None}, {"single", SelectionMode::Single}}, ChildPath(path, "selection"));
                properties.empty_text = ReadStringOr(value, "emptyText", "Tidak ada data", path);
                properties.scrollbar = ParseEnum<ScrollbarMode>(ReadStringOr(value, "scrollbar", "auto", path),
                    {{"auto", ScrollbarMode::Auto}, {"never", ScrollbarMode::Never}}, ChildPath(path, "scrollbar"));
                properties.tab_stop = ReadBoolOr(value, "tabStop", true, path);
                return properties;
            }
            case ComponentType::Scrollbar: {
                ScrollbarProperties properties;
                properties.orientation = ParseEnum<Orientation>(ReadStringOr(value, "orientation", "vertical", path),
                    {{"vertical", Orientation::Vertical}, {"horizontal", Orientation::Horizontal}}, ChildPath(path, "orientation"));
                properties.thickness = ReadIntegerOr(value, "thickness", 12, 8, 32, path);
                properties.minimum_thumb_length = ReadIntegerOr(value, "minThumbLength", 24, 12, 128, path);
                properties.line_step = ReadIntegerOr(value, "lineStep", 1, 1, 1000, path);
                if (value.contains("pageStep")) {
                    if (value.at("pageStep").is_string() && value.at("pageStep").get_ref<const std::string&>() == "auto") properties.page_step.reset();
                    else if (value.at("pageStep").is_number_integer()) properties.page_step = ReadIntegerOr(value, "pageStep", 1, 1, std::numeric_limits<int>::max(), path);
                    else Fail("wrong-type", ChildPath(path, "pageStep"), "pageStep must be auto or a positive integer.");
                }
                return properties;
            }
            case ComponentType::Dialog: {
                DialogProperties properties;
                properties.title = ParseTextValue(Required(value, "title", path), ChildPath(path, "title"));
                if (ReadStringOr(value, "modality", "modal", path) != "modal") Fail("invalid-enum", ChildPath(path, "modality"), "Dialog modality must be modal.");
                properties.width = ReadIntegerOr(value, "width", 480, 240, 1200, path);
                properties.maximum_height = ReadIntegerOr(value, "maxHeight", 720, 160, 1200, path);
                if (value.contains("dismissPolicy")) {
                    const Json& dismiss = value.at("dismissPolicy");
                    const std::string dismiss_path = ChildPath(path, "dismissPolicy");
                    RequireExactKeys(dismiss, {}, {"escape", "outsideClick", "explicitAction"}, dismiss_path);
                    properties.dismiss_escape = ReadBoolOr(dismiss, "escape", true, dismiss_path);
                    properties.dismiss_outside_click = ReadBoolOr(dismiss, "outsideClick", false, dismiss_path);
                    properties.dismiss_explicit_action = ReadBoolOr(dismiss, "explicitAction", true, dismiss_path);
                }
                return properties;
            }
        }
        Fail("component-type", path, "Unsupported component type.");
    }

    void ValidateAutomationName(const ResolvedComponent& component, const std::string& path) const {
        if (!component.automation.automatic_name) return;
        const bool has_source = std::visit([](const auto& properties) -> bool {
            using Type = std::decay_t<decltype(properties)>;
            if constexpr (std::is_same_v<Type, WindowProperties> ||
                          std::is_same_v<Type, TextProperties> ||
                          std::is_same_v<Type, ButtonProperties> ||
                          std::is_same_v<Type, CheckboxProperties> ||
                          std::is_same_v<Type, ToggleProperties> ||
                          std::is_same_v<Type, DialogProperties>) {
                return true;
            } else if constexpr (std::is_same_v<Type, InputProperties> ||
                                 std::is_same_v<Type, ComboProperties>) {
                return !properties.placeholder.empty();
            } else {
                return false;
            }
        }, component.properties);
        const bool interactive = component.type == ComponentType::Button || component.type == ComponentType::Input ||
            component.type == ComponentType::Combo || component.type == ComponentType::Checkbox ||
            component.type == ComponentType::Toggle || component.type == ComponentType::List ||
            component.type == ComponentType::Scrollbar ||
            (component.type == ComponentType::Card && std::get<CardProperties>(component.properties).interactive);
        if (interactive && !has_source) {
            Fail("automation-name", ChildPath(path, "automation/name"),
                 "Interactive component with auto name has no naming source.");
        }
    }

    std::map<std::string, std::size_t, std::less<>> style_indices_;
    std::set<std::string, std::less<>> route_ids_;
    std::set<std::string, std::less<>> ids_;
};

void RequireStyleSetsEquivalent(const std::array<ResolvedTheme, 3>& themes) {
    for (std::size_t index = 1; index < themes.size(); ++index) {
        if (themes[index].styles.size() != themes[0].styles.size()) {
            Fail("theme-style-set", "/styles", "All themes must resolve the same style IDs.");
        }
        for (std::size_t style = 0; style < themes[0].styles.size(); ++style) {
            if (themes[index].styles[style].id != themes[0].styles[style].id) {
                Fail("theme-style-set", "/styles", "All themes must resolve the same style IDs.");
            }
        }
    }
}

bool IsOpaque(const ResolvedColor& color) {
    if (const auto* literal = std::get_if<LiteralRgba>(&color)) return literal->alpha == 255;
    return true;
}

void ValidateNativeSurfaceAlpha(const ResolvedComponent& component,
                                const std::array<ResolvedTheme, 3>& themes) {
    if (component.type == ComponentType::Input) {
        for (const ResolvedTheme& theme : themes) {
            const ResolvedStyle& style = theme.styles.at(component.style_index);
            for (const ResolvedVisualState& state : style.states) {
                if (!IsOpaque(state.background)) {
                    Fail("native-surface-alpha", "/styles/" + style.id,
                         "Input native surface requires an opaque resolved background.");
                }
            }
        }
    }
    if (const auto* list = std::get_if<ListProperties>(&component.properties); list && list->item_template) {
        ValidateNativeSurfaceAlpha(*list->item_template, themes);
    }
    for (const ResolvedComponent& child : component.children) ValidateNativeSurfaceAlpha(child, themes);
}

ResolvedUiDocument ResolveMergedDocument(const Json& merged, UiConfigMetadata metadata,
                                         std::uint64_t generation) {
    const Json& tokens = merged.at("tokens");
    RequireExactKeys(tokens, {"dark", "light", "highContrast"}, {}, "/tokens");
    const Json& styles = merged.at("styles");
    RequireObject(styles, "/styles");

    ResolvedUiDocument document;
    document.metadata = std::move(metadata);
    document.generation = generation;
    document.themes[0] = ThemeResolver(ThemeKind::Dark, tokens.at("dark"), styles, "/dark").Resolve();
    document.themes[1] = ThemeResolver(ThemeKind::Light, tokens.at("light"), styles, "/light").Resolve();
    document.themes[2] = ThemeResolver(ThemeKind::HighContrast, tokens.at("highContrast"), styles, "/highContrast").Resolve();
    RequireStyleSetsEquivalent(document.themes);

    static const std::set<std::string, std::less<>> route_ids = {
        "terminal", "json-inject", "json-editor", "chrome-launcher",
        "chrome-profile-manager", "settings", "ui-editor",
    };

    const Json& windows = merged.at("windows");
    const Json& screens = merged.at("screens");
    RequireObject(windows, "/windows");
    RequireObject(screens, "/screens");
    if (windows.empty()) Fail("empty-windows", "/windows", "At least one Window is required.");
    for (const auto& item : windows.items()) {
        if (!IsLowerKebab(item.key())) Fail("invalid-window-id", ChildPath("/windows", item.key()), "Window ID must be lower-kebab-case.");
        ComponentResolver resolver(document.themes[0].styles, route_ids);
        ResolvedComponent component = resolver.Resolve(item.value(), ChildPath("/windows", item.key()));
        if (component.type != ComponentType::Window) Fail("root-component-type", ChildPath("/windows", item.key()), "windows entries must be Window components.");
        ValidateNativeSurfaceAlpha(component, document.themes);
        document.windows.emplace(item.key(), std::move(component));
    }
    for (const auto& item : screens.items()) {
        if (!route_ids.contains(item.key())) Fail("invalid-route", ChildPath("/screens", item.key()), "Screen key is not in the V1 route inventory.");
        ComponentResolver resolver(document.themes[0].styles, route_ids);
        ResolvedComponent component = resolver.Resolve(item.value(), ChildPath("/screens", item.key()));
        if (component.type != ComponentType::Screen) Fail("root-component-type", ChildPath("/screens", item.key()), "screens entries must be Screen components.");
        if (std::get<ScreenProperties>(component.properties).route_id != item.key()) Fail("route-mismatch", ChildPath("/screens", item.key()), "Screen routeId must match its map key.");
        ValidateNativeSurfaceAlpha(component, document.themes);
        document.screens.emplace(item.key(), std::move(component));
    }
    for (const std::string& route : route_ids) {
        if (!document.screens.contains(route)) Fail("missing-route", ChildPath("/screens", route), "Required V1 screen is missing.");
    }
    return document;
}

ResolveDiagnostic MakeDiagnostic(const ValidationError& error, std::string source) {
    return {error.code(), std::move(source), error.path(), error.what(), error.rollback_incompatible()};
}

}  // namespace

const ResolvedTheme& ResolvedUiDocument::theme(ThemeKind kind) const noexcept {
    switch (kind) {
        case ThemeKind::Dark: return themes[0];
        case ThemeKind::Light: return themes[1];
        case ThemeKind::HighContrast: return themes[2];
    }
    return themes[1];
}

namespace detail {

ResolveDocumentsResult ResolveDocuments(std::string_view embedded_json,
                                        std::optional<std::string_view> override_json,
                                        std::uint64_t generation) {
    const Json embedded = ParseDocument(embedded_json, "embedded");
    const UiConfigMetadata embedded_metadata = ValidateMetadata(embedded, "default", "embedded");

    if (!override_json) {
        auto document = std::make_shared<ResolvedUiDocument>(
            ResolveMergedDocument(embedded, embedded_metadata, generation));
        return {std::move(document), std::nullopt};
    }

    try {
        const Json override_document = ParseDocument(*override_json, "override");
        const UiConfigMetadata override_metadata = ValidateMetadata(override_document, "override", "override");
        const Json merged = MergeDocuments(embedded, override_document);
        auto document = std::make_shared<ResolvedUiDocument>(
            ResolveMergedDocument(merged, override_metadata, generation));
        return {std::move(document), std::nullopt};
    } catch (const ValidationError& error) {
        auto document = std::make_shared<ResolvedUiDocument>(
            ResolveMergedDocument(embedded, embedded_metadata, generation));
        return {std::move(document), MakeDiagnostic(error, "override")};
    }
}

}  // namespace detail
}  // namespace ui::config
