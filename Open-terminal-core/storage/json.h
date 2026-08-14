// Minimal JSON model. Object member order is preserved so rewriting a config
// file keeps every field the user (or another tool) already had there.
#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
  public:
    Value() = default;
    static Value Null();
    static Value Bool(bool value);
    static Value Number(double value);
    static Value Number(std::wstring raw, double value);
    static Value String(std::wstring value);
    static Value Array();
    static Value Object();

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool AsBool(bool fallback = false) const;
    double AsNumber(double fallback = 0.0) const;
    const std::wstring& AsString() const;

    // Array access.
    size_t size() const { return items_.size(); }
    void Append(Value value);

    // Object access. Find returns nullptr when the key is absent.
    const Value* Find(std::wstring_view key) const;
    Value* Find(std::wstring_view key);
    std::wstring StringField(std::wstring_view key, std::wstring_view fallback = {}) const;
    bool BoolField(std::wstring_view key, bool fallback = false) const;
    const Value* ArrayField(std::wstring_view key) const;
    const Value* ObjectField(std::wstring_view key) const;

    // Sets a member, keeping its original position when the key already exists.
    void Set(std::wstring_view key, Value value);
    void Remove(std::wstring_view key);

    const std::vector<std::pair<std::wstring, Value>>& members() const { return members_; }
    const std::vector<Value>& items() const { return items_; }

  private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::wstring text_;  // string payload, or verbatim number text
    std::vector<Value> items_;
    std::vector<std::pair<std::wstring, Value>> members_;
};

// Parses strict JSON. `error` gets a human-readable message with a 1-based line.
bool Parse(std::wstring_view text, Value* out, std::wstring* error);

// Pretty-prints with two-space indent and a trailing newline.
std::wstring Serialize(const Value& value, bool pretty = true);

// Quotes and escapes a bare string as a JSON string literal.
std::wstring EscapeString(std::wstring_view value);

}  // namespace json
