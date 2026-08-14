#include "storage/json.h"

#include <cmath>
#include <cstdio>
#include <cwchar>

namespace json {
namespace {

const std::wstring& EmptyString() {
    static const std::wstring instance;
    return instance;
}

class Parser {
  public:
    Parser(std::wstring_view text) : text_(text) {}

    bool Run(Value* out, std::wstring* error) {
        SkipWhitespace();
        if (!ParseValue(out)) {
            *error = error_;
            return false;
        }
        SkipWhitespace();
        if (pos_ != text_.size()) {
            *error = Message(L"Unexpected text after the top-level value");
            return false;
        }
        return true;
    }

  private:
    std::wstring_view text_;
    size_t pos_ = 0;
    int depth_ = 0;
    std::wstring error_;

    std::wstring Message(std::wstring_view what) const {
        size_t line = 1;
        size_t column = 1;
        for (size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == L'\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        return std::wstring(what) + L" (line " + std::to_wstring(line) + L", column " + std::to_wstring(column) + L")";
    }

    bool Fail(std::wstring_view what) {
        if (error_.empty()) error_ = Message(what);
        return false;
    }

    void SkipWhitespace() {
        while (pos_ < text_.size()) {
            const wchar_t c = text_[pos_];
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool Literal(std::wstring_view word) {
        if (text_.compare(pos_, word.size(), word) != 0) return false;
        pos_ += word.size();
        return true;
    }

    bool ParseValue(Value* out) {
        if (depth_ > 200) return Fail(L"JSON nesting is too deep");
        if (pos_ >= text_.size()) return Fail(L"Unexpected end of input");
        switch (text_[pos_]) {
            case L'{':
                return ParseObject(out);
            case L'[':
                return ParseArray(out);
            case L'"': {
                std::wstring text;
                if (!ParseString(&text)) return false;
                *out = Value::String(std::move(text));
                return true;
            }
            case L't':
                if (!Literal(L"true")) return Fail(L"Invalid literal");
                *out = Value::Bool(true);
                return true;
            case L'f':
                if (!Literal(L"false")) return Fail(L"Invalid literal");
                *out = Value::Bool(false);
                return true;
            case L'n':
                if (!Literal(L"null")) return Fail(L"Invalid literal");
                *out = Value::Null();
                return true;
            default:
                return ParseNumber(out);
        }
    }

    bool ParseObject(Value* out) {
        ++pos_;  // '{'
        ++depth_;
        Value object = Value::Object();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == L'}') {
            ++pos_;
            --depth_;
            *out = std::move(object);
            return true;
        }
        for (;;) {
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != L'"') return Fail(L"Expected a quoted member name");
            std::wstring key;
            if (!ParseString(&key)) return false;
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != L':') return Fail(L"Expected ':' after the member name");
            ++pos_;
            SkipWhitespace();
            Value member;
            if (!ParseValue(&member)) return false;
            object.Set(key, std::move(member));
            SkipWhitespace();
            if (pos_ < text_.size() && text_[pos_] == L',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == L'}') {
                ++pos_;
                --depth_;
                *out = std::move(object);
                return true;
            }
            return Fail(L"Expected ',' or '}'");
        }
    }

    bool ParseArray(Value* out) {
        ++pos_;  // '['
        ++depth_;
        Value array = Value::Array();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == L']') {
            ++pos_;
            --depth_;
            *out = std::move(array);
            return true;
        }
        for (;;) {
            SkipWhitespace();
            Value item;
            if (!ParseValue(&item)) return false;
            array.Append(std::move(item));
            SkipWhitespace();
            if (pos_ < text_.size() && text_[pos_] == L',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == L']') {
                ++pos_;
                --depth_;
                *out = std::move(array);
                return true;
            }
            return Fail(L"Expected ',' or ']'");
        }
    }

    bool ParseString(std::wstring* out) {
        ++pos_;  // opening quote
        out->clear();
        while (pos_ < text_.size()) {
            const wchar_t c = text_[pos_++];
            if (c == L'"') return true;
            if (c == L'\\') {
                if (pos_ >= text_.size()) break;
                const wchar_t esc = text_[pos_++];
                switch (esc) {
                    case L'"': out->push_back(L'"'); break;
                    case L'\\': out->push_back(L'\\'); break;
                    case L'/': out->push_back(L'/'); break;
                    case L'b': out->push_back(L'\b'); break;
                    case L'f': out->push_back(L'\f'); break;
                    case L'n': out->push_back(L'\n'); break;
                    case L'r': out->push_back(L'\r'); break;
                    case L't': out->push_back(L'\t'); break;
                    case L'u': {
                        if (pos_ + 4 > text_.size()) return Fail(L"Truncated \\u escape");
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            const wchar_t digit = text_[pos_ + i];
                            unsigned nibble = 0;
                            if (digit >= L'0' && digit <= L'9') {
                                nibble = static_cast<unsigned>(digit - L'0');
                            } else if (digit >= L'a' && digit <= L'f') {
                                nibble = static_cast<unsigned>(digit - L'a') + 10;
                            } else if (digit >= L'A' && digit <= L'F') {
                                nibble = static_cast<unsigned>(digit - L'A') + 10;
                            } else {
                                return Fail(L"Invalid \\u escape");
                            }
                            code = code * 16 + nibble;
                        }
                        pos_ += 4;
                        out->push_back(static_cast<wchar_t>(code));
                        break;
                    }
                    default:
                        return Fail(L"Invalid escape sequence");
                }
                continue;
            }
            if (c < 0x20) return Fail(L"Control character inside a string");
            out->push_back(c);
        }
        return Fail(L"Unterminated string");
    }

    bool ParseNumber(Value* out) {
        const size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == L'-' || text_[pos_] == L'+')) ++pos_;
        bool any_digit = false;
        while (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
            ++pos_;
            any_digit = true;
        }
        if (pos_ < text_.size() && text_[pos_] == L'.') {
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
                ++pos_;
                any_digit = true;
            }
        }
        if (any_digit && pos_ < text_.size() && (text_[pos_] == L'e' || text_[pos_] == L'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == L'-' || text_[pos_] == L'+')) ++pos_;
            bool exp_digit = false;
            while (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
                ++pos_;
                exp_digit = true;
            }
            if (!exp_digit) return Fail(L"Invalid exponent");
        }
        if (!any_digit) {
            pos_ = start;
            return Fail(L"Unexpected character");
        }
        const std::wstring raw(text_.substr(start, pos_ - start));
        *out = Value::Number(raw, wcstod(raw.c_str(), nullptr));
        return true;
    }
};

void Write(const Value& value, bool pretty, int indent, std::wstring* out) {
    const std::wstring pad = pretty ? std::wstring(static_cast<size_t>(indent) * 2, L' ') : std::wstring();
    const std::wstring pad_inner =
        pretty ? std::wstring(static_cast<size_t>(indent + 1) * 2, L' ') : std::wstring();
    const wchar_t* newline = pretty ? L"\n" : L"";
    const wchar_t* colon = pretty ? L": " : L":";

    switch (value.type()) {
        case Type::Null:
            out->append(L"null");
            break;
        case Type::Bool:
            out->append(value.AsBool() ? L"true" : L"false");
            break;
        case Type::Number: {
            const std::wstring& raw = value.AsString();
            if (!raw.empty()) {
                out->append(raw);
                break;
            }
            const double number = value.AsNumber();
            wchar_t buffer[64];
            if (number == std::floor(number) && std::fabs(number) < 1e15) {
                swprintf(buffer, 64, L"%lld", static_cast<long long>(number));
            } else {
                swprintf(buffer, 64, L"%.17g", number);
            }
            out->append(buffer);
            break;
        }
        case Type::String:
            out->append(EscapeString(value.AsString()));
            break;
        case Type::Array: {
            if (value.items().empty()) {
                out->append(L"[]");
                break;
            }
            out->append(L"[").append(newline);
            for (size_t i = 0; i < value.items().size(); ++i) {
                out->append(pad_inner);
                Write(value.items()[i], pretty, indent + 1, out);
                if (i + 1 < value.items().size()) out->append(L",");
                out->append(newline);
            }
            out->append(pad).append(L"]");
            break;
        }
        case Type::Object: {
            if (value.members().empty()) {
                out->append(L"{}");
                break;
            }
            out->append(L"{").append(newline);
            for (size_t i = 0; i < value.members().size(); ++i) {
                out->append(pad_inner);
                out->append(EscapeString(value.members()[i].first));
                out->append(colon);
                Write(value.members()[i].second, pretty, indent + 1, out);
                if (i + 1 < value.members().size()) out->append(L",");
                out->append(newline);
            }
            out->append(pad).append(L"}");
            break;
        }
    }
}

}  // namespace

Value Value::Null() { return Value(); }

Value Value::Bool(bool value) {
    Value out;
    out.type_ = Type::Bool;
    out.bool_ = value;
    return out;
}

Value Value::Number(double value) {
    Value out;
    out.type_ = Type::Number;
    out.number_ = value;
    return out;
}

Value Value::Number(std::wstring raw, double value) {
    Value out;
    out.type_ = Type::Number;
    out.number_ = value;
    out.text_ = std::move(raw);
    return out;
}

Value Value::String(std::wstring value) {
    Value out;
    out.type_ = Type::String;
    out.text_ = std::move(value);
    return out;
}

Value Value::Array() {
    Value out;
    out.type_ = Type::Array;
    return out;
}

Value Value::Object() {
    Value out;
    out.type_ = Type::Object;
    return out;
}

bool Value::AsBool(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }
double Value::AsNumber(double fallback) const { return type_ == Type::Number ? number_ : fallback; }

const std::wstring& Value::AsString() const {
    if (type_ == Type::String || type_ == Type::Number) return text_;
    return EmptyString();
}

void Value::Append(Value value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        members_.clear();
    }
    items_.push_back(std::move(value));
}

const Value* Value::Find(std::wstring_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : members_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

Value* Value::Find(std::wstring_view key) {
    if (type_ != Type::Object) return nullptr;
    for (auto& member : members_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

std::wstring Value::StringField(std::wstring_view key, std::wstring_view fallback) const {
    const Value* found = Find(key);
    if (!found || !found->is_string()) return std::wstring(fallback);
    return found->AsString();
}

bool Value::BoolField(std::wstring_view key, bool fallback) const {
    const Value* found = Find(key);
    if (!found || !found->is_bool()) return fallback;
    return found->AsBool();
}

const Value* Value::ArrayField(std::wstring_view key) const {
    const Value* found = Find(key);
    return (found && found->is_array()) ? found : nullptr;
}

const Value* Value::ObjectField(std::wstring_view key) const {
    const Value* found = Find(key);
    return (found && found->is_object()) ? found : nullptr;
}

void Value::Set(std::wstring_view key, Value value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        items_.clear();
    }
    for (auto& member : members_) {
        if (member.first == key) {
            member.second = std::move(value);
            return;
        }
    }
    members_.emplace_back(std::wstring(key), std::move(value));
}

void Value::Remove(std::wstring_view key) {
    for (size_t i = 0; i < members_.size(); ++i) {
        if (members_[i].first == key) {
            members_.erase(members_.begin() + static_cast<ptrdiff_t>(i));
            return;
        }
    }
}

bool Parse(std::wstring_view text, Value* out, std::wstring* error) {
    std::wstring local_error;
    Parser parser(text);
    Value parsed;
    if (!parser.Run(&parsed, &local_error)) {
        if (error) *error = local_error.empty() ? L"Invalid JSON" : local_error;
        return false;
    }
    *out = std::move(parsed);
    return true;
}

std::wstring Serialize(const Value& value, bool pretty) {
    std::wstring out;
    Write(value, pretty, 0, &out);
    if (pretty) out.push_back(L'\n');
    return out;
}

std::wstring EscapeString(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size() + 2);
    out.push_back(L'"');
    for (wchar_t c : value) {
        switch (c) {
            case L'"': out.append(L"\\\""); break;
            case L'\\': out.append(L"\\\\"); break;
            case L'\b': out.append(L"\\b"); break;
            case L'\f': out.append(L"\\f"); break;
            case L'\n': out.append(L"\\n"); break;
            case L'\r': out.append(L"\\r"); break;
            case L'\t': out.append(L"\\t"); break;
            default:
                if (c < 0x20) {
                    wchar_t buffer[8];
                    swprintf(buffer, 8, L"\\u%04x", static_cast<unsigned>(c));
                    out.append(buffer);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back(L'"');
    return out;
}

}  // namespace json
