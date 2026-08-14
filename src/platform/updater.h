#pragma once

namespace updater {

enum class UpdateResult {
    NoUpdate,
    ApplyScheduled,
    Failed,
};

void RunStartupHooks();
UpdateResult CheckDownloadAndApply();

}  // namespace updater
