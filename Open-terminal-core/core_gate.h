// Open-terminal-core public API gate.
// The only header a frontend needs to include.
//
// Usage:
//   #include "core_gate.h"
//   application::CoreApplication app;
//   app.Initialize();
//   auto status = app.LaunchTerminal(req);
#pragma once
#include "application/core_application.h"

// Re-export feature types needed for requests/results.
#include "features/terminal_launch.h"
#include "features/claude_inject.h"
#include "features/claude_settings_file.h"
#include "features/chrome_profiles.h"
#include "features/app_settings.h"
#include "core/status.h"
