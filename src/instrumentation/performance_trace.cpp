#include "instrumentation/performance_trace.h"

#include <windows.h>
#include <psapi.h>
#include <TraceLoggingProvider.h>

#include "rendering/render_runtime.h"

TRACELOGGING_DEFINE_PROVIDER(
    g_terminal_performance_provider, "Yuzha.Terminal.Performance",
    (0x926b237e, 0xf049, 0x4ec4, 0x80, 0x26, 0x5d, 0xb2, 0xe2, 0x7a, 0x82, 0x39));

namespace instrumentation {
namespace {

bool g_registered = false;

std::int64_t CurrentQpc() noexcept {
    LARGE_INTEGER counter{};
    return QueryPerformanceCounter(&counter) ? counter.QuadPart : 0;
}

struct WindowCounter {
    DWORD process_id = 0;
    std::uint32_t count = 0;
};

BOOL CALLBACK CountChildWindow(HWND, LPARAM value) noexcept {
    auto* counter = reinterpret_cast<WindowCounter*>(value);
    ++counter->count;
    return TRUE;
}

BOOL CALLBACK CountTopLevelWindow(HWND window, LPARAM value) noexcept {
    auto* counter = reinterpret_cast<WindowCounter*>(value);
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != counter->process_id) return TRUE;
    ++counter->count;
    EnumChildWindows(window, CountChildWindow, value);
    return TRUE;
}

}  // namespace

PerformanceTraceSession::PerformanceTraceSession(
    std::int64_t process_entry_qpc, std::int64_t velopack_hooks_complete_qpc) noexcept {
    registered_ = TraceLoggingRegister(g_terminal_performance_provider) == ERROR_SUCCESS;
    g_registered = registered_;
    if (!registered_) {
        return;
    }

    TraceLoggingWrite(g_terminal_performance_provider, "ProcessEntry",
                      TraceLoggingInt64(process_entry_qpc, "Qpc"));
    TraceLoggingWrite(g_terminal_performance_provider, "VelopackHooksComplete",
                      TraceLoggingInt64(velopack_hooks_complete_qpc, "Qpc"));
}

PerformanceTraceSession::~PerformanceTraceSession() {
    if (registered_) {
        g_registered = false;
        TraceLoggingUnregister(g_terminal_performance_provider);
    }
}

void TraceConfigResolved() noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "ConfigResolved",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"));
    }
}

void TraceRenderBufferReady() noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "RenderBufferReady",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"));
    }
}

void TraceFirstLayoutComplete() noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "FirstLayoutComplete",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"));
    }
}

void TraceFirstPresentComplete() noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "FirstPresentComplete",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"));
    }
}

void TraceFirstFrameVisible() noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "FirstFrameVisible",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"));
    }
}

void TraceInputReceived(std::uint64_t correlation_id) noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "InputReceived",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"),
                          TraceLoggingUInt64(correlation_id, "CorrelationId"));
    }
}

void TraceInputVisualPresented(std::uint64_t correlation_id) noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "InputVisualPresented",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"),
                          TraceLoggingUInt64(correlation_id, "CorrelationId"));
    }
}

void TraceNavigationRequested(std::uint64_t correlation_id) noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "NavigationRequested",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"),
                          TraceLoggingUInt64(correlation_id, "CorrelationId"));
    }
}

void TraceNavigationPresented(std::uint64_t correlation_id) noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "NavigationPresented",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"),
                          TraceLoggingUInt64(correlation_id, "CorrelationId"));
    }
}

void TraceResizeFramePresented(std::uint64_t correlation_id) noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "ResizeFramePresented",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"),
                          TraceLoggingUInt64(correlation_id, "CorrelationId"));
    }
}

void TraceScenarioSettled(std::uint64_t correlation_id) noexcept {
    if (g_registered) {
        TraceLoggingWrite(g_terminal_performance_provider, "ScenarioSettled",
                          TraceLoggingInt64(CurrentQpc(), "Qpc"),
                          TraceLoggingUInt64(correlation_id, "CorrelationId"));
    }
}

void TraceResourceSnapshot(const rendering::RenderRuntimeDiagnostics& renderer) noexcept {
    if (!g_registered) {
        return;
    }

    const HANDLE process = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    const bool memory_available =
        GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory)) != FALSE;
    WindowCounter windows{GetCurrentProcessId(), 0};
    EnumWindows(CountTopLevelWindow, reinterpret_cast<LPARAM>(&windows));
    TraceLoggingWrite(g_terminal_performance_provider, "ResourceSnapshot",
                      TraceLoggingInt64(CurrentQpc(), "Qpc"),
                      TraceLoggingUInt64(memory_available ? memory.PrivateUsage : 0,
                                         "PrivateUsageBytes"),
                      TraceLoggingUInt64(memory_available ? memory.WorkingSetSize : 0,
                                         "WorkingSetBytes"),
                      TraceLoggingUInt32(GetGuiResources(process, GR_GDIOBJECTS), "GdiObjects"),
                      TraceLoggingUInt32(GetGuiResources(process, GR_USEROBJECTS), "UserObjects"),
                      TraceLoggingUInt32(windows.count, "HwndCount"),
                      TraceLoggingUInt64(renderer.resource_epoch, "RendererResourceEpoch"),
                      TraceLoggingUInt64(renderer.active_window_contexts, "ActiveWindowContexts"),
                      TraceLoggingUInt64(renderer.cached_fonts, "CachedFonts"),
                      TraceLoggingUInt64(renderer.cached_brushes, "CachedBrushes"),
                      TraceLoggingUInt64(renderer.cached_pens, "CachedPens"),
                      TraceLoggingUInt64(renderer.cached_corner_tiles, "CachedCornerTiles"),
                      TraceLoggingUInt64(renderer.native_peer_fonts, "NativePeerFonts"),
                      TraceLoggingUInt64(renderer.native_peer_brushes, "NativePeerBrushes"),
                      TraceLoggingUInt64(renderer.native_peer_font_leases, "NativePeerFontLeases"),
                      TraceLoggingUInt64(renderer.native_peer_brush_leases,
                                         "NativePeerBrushLeases"));
}

}  // namespace instrumentation
