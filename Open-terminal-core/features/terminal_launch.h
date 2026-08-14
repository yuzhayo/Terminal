// Terminal launcher — business logic only, no UI types.
//
// Usage pattern (Plan/Run split):
//   1. Build a Request from UI inputs.
//   2. Call Plan(request) — validates folder + venv immediately so a bad path is
//      reported before any WSL probe is started.
//   3. If Plan returns needs_wsl_probe == true, run wsl::ResolveProbe() on a
//      worker thread. On completion, set request.wsl_distro from the probe result.
//   4. Call Run(request) — launches and persists the folder.
#pragma once
#include <string>
#include <vector>

#include "core/status.h"

namespace storage { struct Settings; }

namespace features {

enum class TerminalTarget {
    PowerShellAdmin,
    PowerShell,
    UbuntuWsl,
};

struct TerminalRequest {
    TerminalTarget target = TerminalTarget::PowerShell;
    std::wstring folder;
    bool activate_venv = false;
    // WSL only: filled by the caller after wsl::ResolveProbe() completes.
    // Empty lets wsl.exe fall back to its default distro.
    std::wstring wsl_distro;
};

struct TerminalPlan {
    bool ok = false;
    std::wstring error;       // non-empty only when ok == false
    bool needs_wsl_probe = false;
};

// --- stateless helpers ---

// Validates folder (and venv file when activate_venv is set) without launching
// anything and without touching WSL. normalized receives the resolved path on
// success.
TerminalPlan Plan(const TerminalRequest& request);

// Launches the terminal, then persists the folder in settings.
// Caller must have resolved wsl_distro first if target == UbuntuWsl.
core::Status Run(const TerminalRequest& request);

// Venv activation script paths (exposed so the UI can show a hint).
std::wstring VenvActivateWindows(const std::wstring& folder);
std::wstring VenvActivateWsl(const std::wstring& folder);

// Returns true if the venv flag for this target is set in settings.
bool VenvEnabled(TerminalTarget target);

// Toggles and persists the venv flag for this target.
void SetVenvEnabled(TerminalTarget target, bool enabled);

// Persists folder as the current folder without launching.
void RememberFolder(const std::wstring& folder);

// The ordered recent-folder list from settings.
const std::vector<std::wstring>& RecentFolders();

}  // namespace features
