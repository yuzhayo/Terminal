// WSL helpers shared by the Claude Code injector and the Chrome scanner.
// Everything here runs on demand — never during startup.
#pragma once
#include <windows.h>

#include <string>

namespace wsl {

std::wstring Exe();

// Registered Ubuntu distro plus its POSIX home, e.g. "Ubuntu" and "/home/me".
// The result is cached for the process lifetime, including the failure message.
//
// The first call runs `wsl.exe --list`, which blocks for up to 8 seconds when no
// distro is running, so it must be reached from a worker (see ResolveAsync). A
// debug assert catches a call made on the UI thread before the cache is warm.
bool Resolve(std::wstring* distro, std::wstring* home, std::wstring* error);

// True once Resolve() can answer from its cache, success or failure. Never
// probes, so it is safe on any thread — use it to decide whether an action has to
// wait for a worker first.
bool IsResolved();

// Outcome of a background probe, posted back by ResolveAsync.
struct Probe {
    bool ok = false;
    std::wstring error;
};

// Runs Resolve() on a worker thread and posts `message` to `target` with
// wparam = `generation` and lparam = a heap Probe* the handler owns. Returns
// false when the worker could not start, in which case nothing is posted.
bool ResolveAsync(HWND target, UINT message, unsigned generation);

// \\wsl.localhost\<distro>\<posix path with backslashes>
std::wstring UncPath(const std::wstring& distro, const std::wstring& posix_path);

// Convenience: UNC path for a file under the resolved distro's home.
bool HomeFile(const std::wstring& relative_posix, std::wstring* unc_path, std::wstring* error);

}  // namespace wsl
