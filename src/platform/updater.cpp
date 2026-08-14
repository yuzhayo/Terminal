#include "platform/updater.h"

#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4100)
#include <Velopack.hpp>
#pragma warning(pop)

#include <exception>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

namespace updater {
namespace {

constexpr wchar_t kUpdateSourceEnvironment[] = L"TERMINAL_UPDATE_SOURCE";
constexpr char kGithubRepository[] = "https://github.com/yuzhayo/Terminal";

bool IsPreviewPackage() {
    std::array<wchar_t, 32768> executable_path{};
    const DWORD length = GetModuleFileNameW(nullptr, executable_path.data(),
                                            static_cast<DWORD>(executable_path.size()));
    if (length == 0 || length >= executable_path.size()) {
        return false;
    }

    const std::filesystem::path manifest_path =
        std::filesystem::path(std::wstring_view(executable_path.data(), length)).parent_path() /
        L"sq.version";
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(manifest_path, error);
    if (error || size == 0 || size > 64U * 1024U) {
        return false;
    }

    std::ifstream input(manifest_path, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::string manifest((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    return manifest.find("<channel>win-preview</channel>") != std::string::npos;
}

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
    if (!IsPreviewPackage()) {
        return std::nullopt;
    }

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

    const std::filesystem::path source(value);
    std::error_code error;
    if (!source.is_absolute() || !std::filesystem::is_directory(source, error) || error) {
        return std::nullopt;
    }
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

StartupHookTiming RunStartupHooks() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    StartupHookTiming timing{counter.QuadPart, 0};

    Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run();

    QueryPerformanceCounter(&counter);
    timing.hooks_complete_qpc = counter.QuadPart;
    return timing;
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
        OutputDebugStringA("Terminal updater failed: ");
        OutputDebugStringA(error.what());
        OutputDebugStringA("\n");
        return UpdateResult::Failed;
    }
}

}  // namespace updater
