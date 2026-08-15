#pragma once

#include <memory>

#include "logic/core_gate.h"

namespace ui::application { class StubApplicationBridge; }

namespace application::adapters {

bool RegisterInjectAdapter(ui::application::StubApplicationBridge& bridge,
                           const std::shared_ptr<logic::CoreApplication>& logic);

}  // namespace application::adapters
