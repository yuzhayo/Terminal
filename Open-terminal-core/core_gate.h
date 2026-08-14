// Core gate — stub. Owns all child modules; the frontend talks only to this.
// The request/dispatch shape is left undefined until the UI exists.
#pragma once

#include "features/app_settings.h"
#include "features/app_shell.h"
#include "features/chrome_profiles.h"
#include "features/chrome_visible_set.h"
#include "features/claude_inject.h"
#include "features/claude_settings_file.h"
#include "features/terminal_launch.h"
#include "features/ui_config_draft.h"
#include "storage/settings.h"

// Child modules are stateless (all state lives in storage:: singletons or
// in caller-owned structs). Core owns no instances today. The gate struct
// below is a placeholder for when the UI is ready to wire up a typed or
// serialised boundary.

namespace core {

// Call once at startup before any feature is used.
inline void Load() { storage::Load(); }

// The gate struct is intentionally empty — the children expose free functions.
// When the UI exists, replace this with a typed dispatch surface or a JSON
// command bus, whichever the frontend needs.
struct Gate {};

}  // namespace core
