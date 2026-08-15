// "Start with Windows" registry entry. Per-user only; nothing machine-wide.
#pragma once
#include <string>

namespace platform {

bool IsStartWithWindowsEnabled();

// Writes (or removes) HKCU Run\Terminal pointing at this exe --tray.
bool SetStartWithWindows(bool enabled, std::wstring* error);

}  // namespace platform
