#pragma once

#include "app/app_version.h"

namespace app_identity {

inline constexpr wchar_t kProductName[] = L"Terminal";
inline constexpr wchar_t kPublisher[] = L"Yuzha";
inline constexpr wchar_t kApplicationId[] = L"Yuzha.Terminal";
inline constexpr wchar_t kExecutableName[] = L"Terminal.exe";
inline constexpr wchar_t kAppVersion[] = TERMINAL_VERSION_STRING_W;
inline constexpr char kAppVersionUtf8[] = TERMINAL_VERSION_STRING;

inline constexpr char kUiSchema[] = "yuzha.terminal.ui";
inline constexpr int kUiSchemaVersion = 1;
inline constexpr int kReaderContract = 1;
inline constexpr int kWriterContract = 1;

}  // namespace app_identity
