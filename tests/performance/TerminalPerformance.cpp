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

#include "rendering/layered_popup_render_context.h"
#include "rendering/render_runtime.h"

namespace {

constexpr int kPopupContentWidthDip = 480;
constexpr int kPopupContentHeightDip = 480;
constexpr int kPopupDpi = 96;
constexpr int kPopupShadowMargin = 8;
constexpr int kPopupItemHeight = 32;
constexpr int kPopupItemCount = 200;
constexpr int kPopupVisibleRows = kPopupContentHeightDip / kPopupItemHeight;
constexpr int kPopupSurfaceWidth = kPopupContentWidthDip + kPopupShadowMargin * 2;
constexpr int kPopupSurfaceHeight = kPopupContentHeightDip + kPopupShadowMargin * 2;
constexpr double kInputToPaintTargetMs = 33.0;
constexpr wchar_t kPopupClassName[] = L"Yuzha.Terminal.Performance.LayeredPopup";

struct Options {
    std::filesystem::path app;
    std::filesystem::path output;
    int samples = 30;
};

struct WindowSearch {
    DWORD process_id = 0;
    HWND window = nullptr;
};

class PopupWindow final {
public:
    PopupWindow() {
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = DefWindowProcW;
        window_class.hInstance = instance_;
        window_class.lpszClassName = kPopupClassName;
        if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("RegisterClassExW failed: " +
                                     std::to_string(GetLastError()));
        }
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                  kPopupClassName, L"", WS_POPUP, 0, 0,
                                  kPopupSurfaceWidth, kPopupSurfaceHeight, nullptr, nullptr,
                                  instance_, nullptr);
        if (!window_) {
            const DWORD error = GetLastError();
            UnregisterClassW(kPopupClassName, instance_);
            throw std::runtime_error("CreateWindowExW failed: " + std::to_string(error));
        }
    }

    ~PopupWindow() {
        if (window_) DestroyWindow(window_);
        if (instance_) UnregisterClassW(kPopupClassName, instance_);
    }

    PopupWindow(const PopupWindow&) = delete;
    PopupWindow& operator=(const PopupWindow&) = delete;

    HWND get() const noexcept { return window_; }

private:
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
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

bool RenderDeterministicPopup(rendering::LayeredPopupRenderContext& context, HWND popup,
                              const std::vector<std::wstring>& items,
                              const ui::config::ResolvedFont& font) {
    context.Clear();
    context.SourceOverRounded(
        {kPopupShadowMargin + 2, kPopupShadowMargin + 3,
         kPopupSurfaceWidth - kPopupShadowMargin + 2,
         kPopupSurfaceHeight - kPopupShadowMargin + 3},
        8, 0, {0, 0, 0, 55}, {0, 0, 0, 0});
    const RECT body{kPopupShadowMargin, kPopupShadowMargin,
                    kPopupSurfaceWidth - kPopupShadowMargin,
                    kPopupSurfaceHeight - kPopupShadowMargin};
    context.SourceOverRounded(body, 8, 1, {30, 41, 59, 255}, {71, 85, 105, 255});
    for (int row_index = 0; row_index < kPopupVisibleRows; ++row_index) {
        RECT row{body.left + 1, body.top + row_index * kPopupItemHeight + 1,
                 body.right - 1, body.top + (row_index + 1) * kPopupItemHeight + 1};
        if (row_index == 7) {
            context.SourceOverRounded(row, 4, 0, {37, 99, 235, 255}, {0, 0, 0, 0});
        }
        row.left += 12;
        row.right -= 12;
        if (!context.DrawTextMask(items[static_cast<std::size_t>(row_index)], font, kPopupDpi,
                                  row,
                                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                                      DT_NOPREFIX,
                                  {226, 232, 240, 255})) {
            return false;
        }
    }
    return context.Present(popup, {0, 0});
}

std::vector<double> MeasureLayeredPopup(int sample_count, LARGE_INTEGER frequency) {
    PopupWindow popup;
    rendering::RenderRuntime runtime;
    rendering::LayeredPopupRenderContext context(runtime);
    if (!context.EnsureSize(kPopupSurfaceWidth, kPopupSurfaceHeight)) {
        throw std::runtime_error("Cannot create layered-popup surface.");
    }

    std::vector<std::wstring> items;
    items.reserve(kPopupItemCount);
    for (int index = 0; index < kPopupItemCount; ++index) {
        items.push_back(L"Terminal deterministic item " + std::to_wstring(index + 1));
    }
    const ui::config::ResolvedFont font{"Segoe UI", "Arial", 10, 400};

    // Resource allocation, font creation, and the first DWM submission are warm-up only.
    if (!RenderDeterministicPopup(context, popup.get(), items, font)) {
        throw std::runtime_error("Layered-popup warm-up present failed: " +
                                 std::to_string(GetLastError()));
    }

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(sample_count));
    for (int index = 0; index < sample_count; ++index) {
        LARGE_INTEGER started{};
        LARGE_INTEGER presented{};
        QueryPerformanceCounter(&started);
        if (!RenderDeterministicPopup(context, popup.get(), items, font)) {
            throw std::runtime_error("Layered-popup measured present failed: " +
                                     std::to_string(GetLastError()));
        }
        QueryPerformanceCounter(&presented);
        samples.push_back(static_cast<double>(presented.QuadPart - started.QuadPart) * 1000.0 /
                          static_cast<double>(frequency.QuadPart));
    }
    return samples;
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

void WriteReport(const Options& options, const std::vector<double>& samples,
                 const std::vector<double>& popup_samples) {
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    std::vector<double> popup_sorted = popup_samples;
    std::sort(popup_sorted.begin(), popup_sorted.end());
    const double popup_p95 = Percentile(popup_sorted, 0.95);
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
    output << "    \"max\": " << sorted.back() << "\n  },\n";
    output << "  \"layeredPopup\": {\n";
    output << "    \"scenario\": \"200-items/full-surface-render-present\",\n";
    output << "    \"contentSizeDip\": {\"width\": " << kPopupContentWidthDip
           << ", \"height\": " << kPopupContentHeightDip << "},\n";
    output << "    \"surfaceSizePx\": {\"width\": " << kPopupSurfaceWidth
           << ", \"height\": " << kPopupSurfaceHeight << "},\n";
    output << "    \"dpi\": " << kPopupDpi << ",\n";
    output << "    \"itemCount\": " << kPopupItemCount << ",\n";
    output << "    \"visibleRows\": " << kPopupVisibleRows << ",\n";
    output << "    \"sampleCount\": " << popup_samples.size() << ",\n";
    output << "    \"samplesMs\": [";
    for (std::size_t index = 0; index < popup_samples.size(); ++index) {
        if (index) output << ", ";
        output << popup_samples[index];
    }
    output << "],\n    \"statisticsMs\": {\n";
    output << "      \"min\": " << popup_sorted.front() << ",\n";
    output << "      \"median\": " << Percentile(popup_sorted, 0.50) << ",\n";
    output << "      \"p95\": " << popup_p95 << ",\n";
    output << "      \"max\": " << popup_sorted.back() << "\n    },\n";
    output << "    \"targetP95Ms\": " << kInputToPaintTargetMs << ",\n";
    output << "    \"passed\": " << (popup_p95 <= kInputToPaintTargetMs ? "true" : "false")
           << "\n  }\n}\n";
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
        const std::vector<double> popup_samples = MeasureLayeredPopup(options.samples, frequency);
        WriteReport(options, samples, popup_samples);
        std::wcout << options.output.wstring() << L'\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TerminalPerformance error: " << error.what() << '\n';
        return 2;
    }
}
