#pragma once

#include <cstdint>

namespace updater {

struct StartupHookTiming {
    std::int64_t process_entry_qpc = 0;
    std::int64_t hooks_complete_qpc = 0;
};

enum class UpdateResult {
    NoUpdate,
    ApplyScheduled,
    Failed,
};

StartupHookTiming RunStartupHooks();
UpdateResult CheckDownloadAndApply();

}  // namespace updater
