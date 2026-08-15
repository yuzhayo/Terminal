// Terminal canonical business-logic API gate.
// The only header a frontend needs to include.
//
// Usage:
//   #include "core_gate.h"
//   application::CoreApplication app;
//   app.Initialize();
//   auto status = app.LaunchTerminal(req);
#pragma once
#include "features/terminal_launch.h"
#include "features/claude_inject.h"
#include "features/claude_settings_file.h"
#include "features/chrome_profiles.h"
#include "features/chrome_visible_set.h"
#include "features/app_settings.h"
#include "core/status.h"
#include "application/core_application.h"

namespace logic {
using CoreApplication = ::application::CoreApplication;
namespace features = ::features;
namespace core = ::core;
}  // namespace logic
