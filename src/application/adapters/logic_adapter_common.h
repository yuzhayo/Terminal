#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

#include "logic/core_gate.h"
#include "ui/application/stub_application_bridge.h"

namespace application::adapters {

inline std::wstring ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

inline std::string ToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

inline std::optional<std::string> PayloadScalar(
    const ui::application::UiEvent& event, std::string_view key) {
    const auto found = event.payload.find(key);
    if (found == event.payload.end()) return std::nullopt;
    if (const auto* value = std::get_if<std::string>(&found->second.value)) return *value;
    if (const auto* value = std::get_if<bool>(&found->second.value)) {
        return *value ? "true" : "false";
    }
    if (const auto* value = std::get_if<std::int64_t>(&found->second.value)) {
        return std::to_string(*value);
    }
    return std::nullopt;
}

inline bool ScalarBool(std::string_view value) {
    return value == "true" || value == "1";
}

inline void Put(ui::application::StubApplicationBridge& bridge,
                ui::application::UiPatch& patch, std::string binding,
                std::string value) {
    bridge.SetStringValue(binding, value);
    patch.view_state.insert_or_assign(std::move(binding), std::move(value));
    patch.request_repaint = true;
}

inline ui::application::UiPatch StatusPatch(
    ui::application::StubApplicationBridge& bridge, std::string binding,
    const logic::core::Status& status, std::wstring_view fallback = {}) {
    ui::application::UiPatch patch;
    const std::wstring text = status.text.empty() ? std::wstring(fallback) : status.text;
    Put(bridge, patch, std::move(binding), ToUtf8(text));
    return patch;
}

inline ui::application::UiPatch TextPatch(
    ui::application::StubApplicationBridge& bridge, std::string binding,
    std::wstring_view text) {
    ui::application::UiPatch patch;
    Put(bridge, patch, std::move(binding), ToUtf8(text));
    return patch;
}

}  // namespace application::adapters
