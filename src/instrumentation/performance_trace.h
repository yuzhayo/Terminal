#pragma once

#include <cstdint>

namespace instrumentation {

class PerformanceTraceSession final {
public:
    PerformanceTraceSession(std::int64_t process_entry_qpc,
                            std::int64_t velopack_hooks_complete_qpc) noexcept;
    ~PerformanceTraceSession();

    PerformanceTraceSession(const PerformanceTraceSession&) = delete;
    PerformanceTraceSession& operator=(const PerformanceTraceSession&) = delete;

private:
    bool registered_ = false;
};

void TraceConfigResolved() noexcept;
void TraceRenderBufferReady() noexcept;
void TraceFirstLayoutComplete() noexcept;
void TraceFirstPresentComplete() noexcept;
void TraceFirstFrameVisible() noexcept;
void TraceInputReceived(std::uint64_t correlation_id) noexcept;
void TraceInputVisualPresented(std::uint64_t correlation_id) noexcept;
void TraceNavigationRequested(std::uint64_t correlation_id) noexcept;
void TraceNavigationPresented(std::uint64_t correlation_id) noexcept;
void TraceResizeFramePresented(std::uint64_t correlation_id) noexcept;
void TraceScenarioSettled(std::uint64_t correlation_id) noexcept;
void TraceResourceSnapshot() noexcept;

}  // namespace instrumentation
