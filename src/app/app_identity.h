#pragma once

#include "app/app_version.h"

namespace app_identity {

inline constexpr wchar_t kProductName[] = L"Open Terminal Native";
inline constexpr wchar_t kPublisher[] = L"Yuzha";
inline constexpr wchar_t kApplicationId[] = L"Yuzha.OpenTerminalNative";
inline constexpr wchar_t kExecutableName[] = L"OpenTerminalNative.exe";
inline constexpr wchar_t kAppVersion[] = OTN_VERSION_STRING_W;
inline constexpr char kAppVersionUtf8[] = OTN_VERSION_STRING;

inline constexpr char kUiSchema[] = "yuzha.open-terminal-native.ui";
inline constexpr int kUiSchemaVersion = 1;
inline constexpr int kReaderContract = 1;
inline constexpr int kWriterContract = 1;

}  // namespace app_identity
