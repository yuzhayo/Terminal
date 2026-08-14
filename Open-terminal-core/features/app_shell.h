// App shell domain logic — screen registry, command routing, lifecycle rules.
// No HWND, no WM_, no HMENU. The frontend implements the window; this module
// owns the navigation state machine and all command-routing rules.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/status.h"

namespace features {

// ---- screen ids ----

enum class ScreenId {
    Terminal,
    JsonInject,
    JsonEditor,
    ChromeLauncher,
    ChromeProfiles,
    Settings,
    UiEditor,
};

// ---- command table ----
// Single source of truth for nav labels, command-line flags, and jump-list tasks.
// JsonEditor is deliberately NOT a jump-list entry (matches old behaviour).

struct AppCommand {
    const wchar_t* label;     // nav button label
    const wchar_t* arg;       // CLI flag, e.g. "--terminal"
    std::optional<ScreenId> screen;  // nullopt means no screen (--exit, --tray)
    bool jump_list = false;   // include in the Windows Jump List
};

// Returns the full command table. The order is the nav order.
const std::vector<AppCommand>& CommandTable();

// Finds the screen to navigate to for a parsed argument token, or nullopt.
std::optional<ScreenId> RouteFromArg(const std::wstring& arg);

// Returns the parent screen for Back navigation, or nullopt when there is none.
std::optional<ScreenId> ParentOf(ScreenId screen);

// ---- command-line tokeniser (fixes the old substring-search bug) ----
// Splits a raw GetCommandLineW() string into argv-style tokens.
std::vector<std::wstring> TokenizeCommandLine(const std::wstring& command_line);

// ---- navigation state ----

struct ShellState {
    std::optional<ScreenId> current;  // active screen; empty = tray-only mode
    std::optional<ScreenId> pending;  // first screen to show on next window reveal
};

// Applies a parsed command: --exit returns false (caller should quit),
// --tray leaves state unchanged, a route sets pending/current, otherwise
// show-and-focus. Returns true unless the caller should exit.
struct CommandEffect {
    bool should_exit     = false;
    bool should_show     = false;   // bring the window forward
    std::optional<ScreenId> navigate_to;
};
CommandEffect ApplyCommand(ShellState* state, const std::wstring& raw_command_line);

// Navigate to a screen. No-op when already current.
void Navigate(ShellState* state, ScreenId id);

// Back: navigates to ParentOf(current), or no-op when no parent.
void GoBack(ShellState* state);

// ---- tray / window lifecycle rules ----

enum class CloseAction { Exit, HideToTray };

// The default for WM_CLOSE can be set by the frontend; core exposes the logic.
// Returns HideToTray only when hide_to_tray policy is active.
CloseAction OnClose(CloseAction policy);

// Invariant enforced by core: icon visible ⇔ window hidden.
// Returns false when the tray icon could not be added — caller must NOT hide
// the window in that case, because the app would be unreachable.
bool CanHideToTray(bool tray_icon_added_successfully);

// ---- single-instance rules ----

enum class InstanceRole { First, Secondary, PeerNotFound };

// Named mutex token. Session-scoped.
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\OpenTerminalNative.SingleInstance";

// How long to wait for the first instance to create its window, in ms.
// 40 retries × 50 ms = 2 s ceiling (preserves original behaviour).
constexpr int kPeerWindowRetries  = 40;
constexpr int kPeerWindowRetryMs  = 50;

// Classifies the outcome of a single-instance check.
// (The Win32 mechanism — mutex + FindWindowW + WM_COPYDATA — lives in the
//  frontend; this module only documents the decision rules.)
//
// Outcomes:
//   First       — this is the first instance; proceed normally.
//   Secondary   — the command was forwarded; the caller should exit.
//   PeerNotFound— the mutex was already owned but no window was found in time;
//                 the old code returned Secondary silently; now callers know.
InstanceRole ClassifyInstance(bool mutex_was_new, bool forwarded_successfully);

}  // namespace features
