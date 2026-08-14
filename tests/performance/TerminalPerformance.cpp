#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path app;
    std::filesystem::path output;
    int samples = 30;
};

struct WindowSearch {
    DWORD process_id = 0;
    HWND window = nullptr;
};

BOOL CALLBACK FindVisibleWindow(HWND window, LPARAM value) {
    auto* search = reinterpret_cast<WindowSearch*>(value);
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == search->process_id && IsWindowVisible(window)) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

std::wstring Quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

PROCESS_INFORMATION Start(const std::filesystem::path& app, std::wstring arguments) {
    std::wstring command = Quote(app);
    if (!arguments.empty()) command += L" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        app.parent_path().c_str(), &startup, &process)) {
        throw std::runtime_error("CreateProcessW failed: " + std::to_string(GetLastError()));
    }
    return process;
}

void CloseProcess(PROCESS_INFORMATION& process) {
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    process = {};
}

double MeasureVisibleStartup(const std::filesystem::path& app, LARGE_INTEGER frequency) {
    LARGE_INTEGER started{};
    QueryPerformanceCounter(&started);
    PROCESS_INFORMATION process = Start(app, L"--measurement-run");
    CloseHandle(process.hThread);
    process.hThread = nullptr;
    WindowSearch search{process.dwProcessId, nullptr};
    const ULONGLONG deadline = GetTickCount64() + 10000;
    while (!search.window && GetTickCount64() < deadline) {
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
        EnumWindows(FindVisibleWindow, reinterpret_cast<LPARAM>(&search));
        if (!search.window) Sleep(5);
    }
    LARGE_INTEGER visible{};
    QueryPerformanceCounter(&visible);
    if (!search.window) {
        TerminateProcess(process.hProcess, 30);
        WaitForSingleObject(process.hProcess, 2000);
        CloseProcess(process);
        throw std::runtime_error("Terminal did not create a visible main window.");
    }

    PROCESS_INFORMATION exit_process = Start(app, L"--exit");
    WaitForSingleObject(exit_process.hProcess, 3000);
    CloseProcess(exit_process);
    if (WaitForSingleObject(process.hProcess, 3000) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 31);
        WaitForSingleObject(process.hProcess, 1000);
    }
    CloseProcess(process);
    return static_cast<double>(visible.QuadPart - started.QuadPart) * 1000.0 /
           static_cast<double>(frequency.QuadPart);
}

Options Parse(int count, wchar_t** values) {
    Options options;
    for (int index = 1; index < count; ++index) {
        const std::wstring argument(values[index]);
        if (argument == L"--app" && index + 1 < count) options.app = values[++index];
        else if (argument == L"--output" && index + 1 < count) options.output = values[++index];
        else if (argument == L"--samples" && index + 1 < count) options.samples = std::stoi(values[++index]);
        else throw std::runtime_error("Unknown or incomplete argument.");
    }
    if (options.app.empty() || options.output.empty() || options.samples < 1 || options.samples > 200) {
        throw std::runtime_error("Usage: TerminalPerformance.exe --app <Terminal.exe> --output <report.json> [--samples 1..200]");
    }
    return options;
}

double Percentile(const std::vector<double>& sorted, double percentile) {
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(percentile * static_cast<double>(sorted.size()))) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

void WriteReport(const Options& options, const std::vector<double>& samples) {
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    std::filesystem::create_directories(options.output.parent_path());
    std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create report.json.");
    output << std::fixed << std::setprecision(3);
    output << "{\n  \"schemaVersion\": 1,\n  \"renderer\": \"gdi-dib-bitblt\",\n";
    output << "  \"scenario\": \"cold-process/warm-file-cache\",\n";
    output << "  \"sampleCount\": " << samples.size() << ",\n  \"samplesMs\": [";
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (index) output << ", ";
        output << samples[index];
    }
    output << "],\n  \"statisticsMs\": {\n";
    output << "    \"min\": " << sorted.front() << ",\n";
    output << "    \"median\": " << Percentile(sorted, 0.50) << ",\n";
    output << "    \"p95\": " << Percentile(sorted, 0.95) << ",\n";
    output << "    \"max\": " << sorted.back() << "\n  }\n}\n";
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
    try {
        const Options options = Parse(argument_count, arguments);
        if (!std::filesystem::is_regular_file(options.app)) {
            throw std::runtime_error("Terminal.exe not found.");
        }
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency)) throw std::runtime_error("QPC unavailable.");

        // Warm the executable bytes without launching the app.
        std::ifstream warm(options.app, std::ios::binary);
        std::vector<char> buffer(1024 * 1024);
        while (warm.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || warm.gcount() > 0) {}
        Sleep(2000);

        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(options.samples));
        for (int index = 0; index < options.samples; ++index) {
            samples.push_back(MeasureVisibleStartup(options.app, frequency));
            Sleep(2000);
        }
        WriteReport(options, samples);
        std::wcout << options.output.wstring() << L'\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TerminalPerformance error: " << error.what() << '\n';
        return 2;
    }
}
