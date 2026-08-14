// Process launching and command capture.
#pragma once
#include <string>
#include <string_view>

namespace process {

// How a launched process shows up. Terminals need their own console; helper
// commands must not flash one.
enum class Window { NewConsole, Hidden };

std::wstring ErrorMessage(unsigned long code);

// Launches `command_line` detached. `exe` may be empty when the command line
// starts with the program itself.
bool Launch(std::wstring_view exe, std::wstring_view command_line, std::wstring_view working_dir, Window window,
            std::wstring* error);

// Launches via ShellExecuteExW so verbs like "runas" (UAC elevation) work.
bool ShellLaunch(std::wstring_view verb, std::wstring_view file, std::wstring_view parameters,
                 std::wstring_view working_dir, std::wstring* error);

// Runs a command, waits up to `timeout_ms`, and captures stdout+stderr.
// Returns false only when the process could not be started or timed out.
bool RunCapture(std::wstring_view command_line, std::wstring_view working_dir, unsigned long timeout_ms,
                std::wstring* output, unsigned long* exit_code, std::wstring* error);

}  // namespace process
