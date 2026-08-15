// COM apartment, initialized on first use.
//
// CoInitializeEx costs ~3 ms and nothing on the first frame needs it: the known
// folder lookups in paths.cpp, CoCreateGuid in storage::NewId, and
// SetCurrentProcessExplicitAppUserModelID all work without an apartment. Only
// the folder picker, ShellExecuteExW verbs, and the Jump List do, and all three
// happen after the window is up.
#pragma once

namespace platform {

// Initializes the apartment once per process. Call before any CoCreateInstance
// or shell API that needs COM. Cheap after the first call.
bool EnsureCom();

// Uninitializes if EnsureCom() ever succeeded. Called once from wWinMain.
void ReleaseCom();

}  // namespace platform
