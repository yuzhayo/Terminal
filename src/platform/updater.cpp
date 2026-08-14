#include "platform/updater.h"

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4100)
#include <Velopack.hpp>
#pragma warning(pop)

#include <exception>
#include <memory>
#include <optional>
#include <string>

namespace updater {
namespace {

constexpr wchar_t kUpdateSourceEnvironment[] = L"OPEN_TERMINAL_NATIVE_UPDATE_SOURCE";
constexpr char kGithubRepository[] = "https://github.com/yuzhayo/Terminal";

std::optional<std::string> ToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return std::string{};
    }

    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return std::nullopt;
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), result.data(), required, nullptr,
                                            nullptr);
    if (written != required) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::string> ReadUpdateSourceOverride() {
    const DWORD required = GetEnvironmentVariableW(kUpdateSourceEnvironment, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(kUpdateSourceEnvironment, value.data(), required);
    if (written == 0 || written >= required) {
        return std::nullopt;
    }
    value.resize(written);
    return ToUtf8(value);
}

UpdateResult ApplyFrom(Velopack::UpdateManager& manager) {
    const auto update = manager.CheckForUpdates();
    if (!update) {
        return UpdateResult::NoUpdate;
    }

    manager.DownloadUpdates(*update);
    manager.WaitExitThenApplyUpdates(*update, false, true);
    return UpdateResult::ApplyScheduled;
}

}  // namespace

void RunStartupHooks() {
    Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run();
}

UpdateResult CheckDownloadAndApply() {
    try {
        Velopack::UpdateOptions options{};
        options.AllowVersionDowngrade = false;
        options.ExplicitChannel = std::nullopt;
        options.MaximumDeltasBeforeFallback = 1;

        const auto override_source = ReadUpdateSourceOverride();
        if (override_source && !override_source->empty()) {
            Velopack::UpdateManager manager(*override_source, &options);
            return ApplyFrom(manager);
        }

        auto github = std::make_unique<Velopack::GithubSource>(kGithubRepository, "", false);
        Velopack::UpdateManager manager(std::move(github), &options);
        return ApplyFrom(manager);
    } catch (const std::exception& error) {
        OutputDebugStringA("OpenTerminalNative updater failed: ");
        OutputDebugStringA(error.what());
        OutputDebugStringA("\n");
        return UpdateResult::Failed;
    }
}

}  // namespace updater
