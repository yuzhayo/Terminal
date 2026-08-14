#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "app/app_identity.h"
#include "config/ui_config_gate.h"
#include "platform/app_paths.h"
#include "platform/windows_runtime.h"
#include "resource.h"

namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

#ifdef _DEBUG
constexpr char kConfiguration[] = "Debug";
#else
constexpr char kConfiguration[] = "Release";
#endif

class TestFailure final : public std::runtime_error {
public:
    TestFailure(std::string message, std::string file, int line)
        : std::runtime_error(std::move(message)), file_(std::move(file)), line_(line) {}

    const std::string& file() const noexcept { return file_; }
    int line() const noexcept { return line_; }

private:
    std::string file_;
    int line_ = 0;
};

void Require(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw TestFailure(std::string("Requirement failed: ") + expression, file, line);
    }
}

#define REQUIRE_TRUE(condition) Require((condition), #condition, __FILE__, __LINE__)

struct TestCase {
    std::string name;
    std::function<void()> run;
    std::string file;
    int line = 0;
};

struct TestResult {
    std::string name;
    std::string status;
    double duration_ms = 0.0;
    std::string message;
    std::string file;
    int line = 0;
};

struct Options {
    std::string filter = "*";
    std::filesystem::path json_report =
        std::filesystem::path("artifacts") / "test-results" / kConfiguration / "results.json";
    std::filesystem::path junit_report =
        std::filesystem::path("artifacts") / "test-results" / kConfiguration / "results.junit.xml";
};

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr,
                                             nullptr);
    if (required <= 0) {
        throw std::runtime_error("Argument is not valid Unicode.");
    }

    std::string converted(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), converted.data(),
                                            required, nullptr, nullptr);
    if (written != required) {
        throw std::runtime_error("Argument could not be converted to UTF-8.");
    }
    return converted;
}

bool GlobMatches(std::string_view pattern, std::string_view value) {
    std::size_t pattern_index = 0;
    std::size_t value_index = 0;
    std::size_t star_index = std::string_view::npos;
    std::size_t star_value_index = 0;

    while (value_index < value.size()) {
        if (pattern_index < pattern.size() &&
            (pattern[pattern_index] == '?' || pattern[pattern_index] == value[value_index])) {
            ++pattern_index;
            ++value_index;
        } else if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
            star_index = pattern_index++;
            star_value_index = value_index;
        } else if (star_index != std::string_view::npos) {
            pattern_index = star_index + 1;
            value_index = ++star_value_index;
        } else {
            return false;
        }
    }

    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

std::string UtcNow() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << time.wYear << '-' << std::setw(2) << time.wMonth
           << '-' << std::setw(2) << time.wDay << 'T' << std::setw(2) << time.wHour << ':'
           << std::setw(2) << time.wMinute << ':' << std::setw(2) << time.wSecond << '.'
           << std::setw(3) << time.wMilliseconds << 'Z';
    return output.str();
}

std::string EscapeXml(std::string_view value) {
    std::string escaped;
    for (const char character : value) {
        switch (character) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '\"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            default:
                escaped += character;
                break;
        }
    }
    return escaped;
}

void EnsureParentDirectory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void WriteJsonReport(const Options& options, const std::string& started_utc, double duration_ms,
                     const std::vector<TestResult>& results) {
    const std::size_t passed = static_cast<std::size_t>(std::count_if(
        results.begin(), results.end(), [](const TestResult& result) { return result.status == "passed"; }));

    Json report = {
        {"schemaVersion", 1},
        {"configuration", kConfiguration},
        {"startedUtc", started_utc},
        {"durationMs", duration_ms},
        {"totals", {{"selected", results.size()}, {"passed", passed},
                    {"failed", results.size() - passed}}},
        {"tests", Json::array()},
    };

    for (const TestResult& result : results) {
        report["tests"].push_back({
            {"name", result.name},       {"status", result.status},
            {"durationMs", result.duration_ms}, {"message", result.message},
            {"file", result.file},       {"line", result.line},
        });
    }

    EnsureParentDirectory(options.json_report);
    std::ofstream output(options.json_report, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create JSON report: " + options.json_report.string());
    }
    output << report.dump(2) << '\n';
}

void WriteJunitReport(const Options& options, const std::string& started_utc, double duration_ms,
                      const std::vector<TestResult>& results) {
    const std::size_t failures = static_cast<std::size_t>(std::count_if(
        results.begin(), results.end(), [](const TestResult& result) { return result.status == "failed"; }));

    EnsureParentDirectory(options.junit_report);
    std::ofstream output(options.junit_report, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create JUnit report: " + options.junit_report.string());
    }

    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<testsuite name=\"TerminalTests\" tests=\"" << results.size()
           << "\" failures=\"" << failures << "\" time=\"" << duration_ms / 1000.0
           << "\" timestamp=\"" << EscapeXml(started_utc) << "\">\n";
    for (const TestResult& result : results) {
        output << "  <testcase classname=\"TerminalTests\" name=\"" << EscapeXml(result.name)
               << "\" time=\"" << result.duration_ms / 1000.0 << "\">\n";
        if (result.status == "failed") {
            output << "    <failure message=\"" << EscapeXml(result.message) << "\" file=\""
                   << EscapeXml(result.file) << "\" line=\"" << result.line << "\"/>\n";
        }
        output << "  </testcase>\n";
    }
    output << "</testsuite>\n";
}

bool EndsWith(std::wstring_view value, std::wstring_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size(), suffix.size()) == suffix;
}

void TestAppIdentityContract() {
    REQUIRE_TRUE(std::wcscmp(app_identity::kProductName, L"Terminal") == 0);
    REQUIRE_TRUE(std::wcscmp(app_identity::kApplicationId, L"Yuzha.Terminal") == 0);
    REQUIRE_TRUE(std::wcscmp(app_identity::kExecutableName, L"Terminal.exe") == 0);
    REQUIRE_TRUE(std::string_view(app_identity::kUiSchema) == "yuzha.terminal.ui");
    REQUIRE_TRUE(app_identity::kUiSchemaVersion == 1);
}

void TestAppPathsContract() {
    platform::AppPaths paths;
    std::wstring diagnostic;
    REQUIRE_TRUE(platform::ResolveAppPaths(paths, diagnostic));
    REQUIRE_TRUE(diagnostic.empty());
    REQUIRE_TRUE(EndsWith(paths.data_root, L"\\Yuzha\\Terminal"));
    REQUIRE_TRUE(EndsWith(paths.ui_override, L"\\Yuzha\\Terminal\\ui\\override.v1.json"));
    REQUIRE_TRUE(EndsWith(paths.ui_config_log, L"\\Yuzha\\Terminal\\logs\\ui-config.log"));
    REQUIRE_TRUE(EndsWith(paths.updater_state, L"\\Yuzha\\Terminal\\updater\\state.json"));
}

void TestIconResourceEmbedded() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    REQUIRE_TRUE(FindResourceW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), RT_GROUP_ICON) != nullptr);
    REQUIRE_TRUE(LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON)) != nullptr);
}

void TestUiConfigGateEmbeddedDefault() {
    platform::AppPaths paths;
    std::wstring diagnostic;
    REQUIRE_TRUE(platform::ResolveAppPaths(paths, diagnostic));

    config::UiConfigGate gate(GetModuleHandleW(nullptr), paths);
    REQUIRE_TRUE(gate.ResolveBootstrap(diagnostic));
    REQUIRE_TRUE(diagnostic.empty());
    REQUIRE_TRUE(gate.metadata().schema == app_identity::kUiSchema);
    REQUIRE_TRUE(gate.metadata().version == app_identity::kUiSchemaVersion);
    REQUIRE_TRUE(gate.metadata().minimum_reader_contract <= app_identity::kReaderContract);
    REQUIRE_TRUE(gate.metadata().written_by_config_contract == app_identity::kWriterContract);
}

void TestWindowsRuntimeMinimumBuild() {
    std::wstring diagnostic;
    REQUIRE_TRUE(platform::CheckWindowsRuntime(diagnostic) ==
                 platform::WindowsRuntimeStatus::Supported);
    REQUIRE_TRUE(diagnostic.empty());
}

std::vector<TestCase> DiscoverTests() {
    std::vector<TestCase> tests = {
        {"AppIdentity.Contract", TestAppIdentityContract, __FILE__, __LINE__},
        {"AppPaths.Contract", TestAppPathsContract, __FILE__, __LINE__},
        {"IconResource.Embedded", TestIconResourceEmbedded, __FILE__, __LINE__},
        {"UiConfigGate.EmbeddedDefault", TestUiConfigGateEmbeddedDefault, __FILE__, __LINE__},
        {"WindowsRuntime.MinimumBuild", TestWindowsRuntimeMinimumBuild, __FILE__, __LINE__},
    };
    std::sort(tests.begin(), tests.end(),
              [](const TestCase& left, const TestCase& right) { return left.name < right.name; });
    return tests;
}

Options ParseOptions(int argument_count, wchar_t** arguments) {
    Options options;
    bool filter_seen = false;
    bool json_seen = false;
    bool junit_seen = false;

    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (index + 1 >= argument_count) {
            throw std::runtime_error("Missing value for argument: " + WideToUtf8(argument));
        }

        const std::wstring_view value(arguments[++index]);
        if (argument == L"--filter" && !filter_seen) {
            filter_seen = true;
            options.filter = WideToUtf8(value);
        } else if (argument == L"--report-json" && !json_seen) {
            json_seen = true;
            options.json_report = std::filesystem::path(value);
        } else if (argument == L"--report-junit" && !junit_seen) {
            junit_seen = true;
            options.junit_report = std::filesystem::path(value);
        } else {
            throw std::runtime_error("Unknown or repeated argument: " + WideToUtf8(argument));
        }
    }
    return options;
}

void PrintUsage() {
    std::wcerr << L"Usage: TerminalTests.exe [--filter <glob>] [--report-json <path>] "
                  L"[--report-junit <path>]\n";
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
    try {
        const Options options = ParseOptions(argument_count, arguments);
        const std::vector<TestCase> discovered = DiscoverTests();
        if (discovered.empty()) {
            std::cerr << "No tests were discovered.\n";
            return 2;
        }

        std::vector<TestCase> selected;
        std::copy_if(discovered.begin(), discovered.end(), std::back_inserter(selected),
                     [&](const TestCase& test) { return GlobMatches(options.filter, test.name); });
        if (selected.empty()) {
            std::cerr << "The filter selected no tests: " << options.filter << '\n';
            return 2;
        }

        const std::string started_utc = UtcNow();
        const Clock::time_point suite_start = Clock::now();
        std::vector<TestResult> results;
        results.reserve(selected.size());

        for (const TestCase& test : selected) {
            TestResult result{test.name, "passed", 0.0, "", test.file, test.line};
            const Clock::time_point test_start = Clock::now();
            try {
                test.run();
            } catch (const TestFailure& failure) {
                result.status = "failed";
                result.message = failure.what();
                result.file = failure.file();
                result.line = failure.line();
            } catch (const std::exception& error) {
                result.status = "failed";
                result.message = error.what();
            } catch (...) {
                result.status = "failed";
                result.message = "Unknown C++ exception.";
            }
            result.duration_ms =
                std::chrono::duration<double, std::milli>(Clock::now() - test_start).count();
            std::cout << (result.status == "passed" ? "PASS " : "FAIL ") << result.name;
            if (!result.message.empty()) {
                std::cout << " - " << result.message;
            }
            std::cout << '\n';
            results.push_back(std::move(result));
        }

        const double duration_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - suite_start).count();
        WriteJsonReport(options, started_utc, duration_ms, results);
        WriteJunitReport(options, started_utc, duration_ms, results);

        const bool any_failed = std::any_of(results.begin(), results.end(),
                                            [](const TestResult& result) {
                                                return result.status == "failed";
                                            });
        return any_failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "TerminalTests runner error: " << error.what() << '\n';
        PrintUsage();
        return 2;
    }
}
