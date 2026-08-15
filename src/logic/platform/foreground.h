// Window focus helpers — extracted from chrome profiles so the launch logic stays
// UI-free. These are HWND/EnumWindows but window *management*, not rendering.
#pragma once

namespace platform {

// True when at least one Chrome browser window is visible on this desktop.
// Chrome maps --profile-directory to the right window itself; this only decides
// whether a launch is "attach to running Chrome" or a cold start.
bool ChromeWindowExists();

// Finds the first visible Chrome window and brings it to the foreground. No-op
// when no Chrome window is found. Used after a launch when the reused window may
// be behind the launcher.
void FocusChromeWindow();

}  // namespace platform
