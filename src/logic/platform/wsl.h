// WSL helpers shared by the Claude Code injector and the Chrome scanner.
// Everything here runs on demand — never during startup.
//
// Core has no message pump, so the async wrapper the old app had (ResolveAsync,
// which posted a window message back to an HWND) is gone. Resolve() is a plain
// blocking call and the frontend decides which thread it runs on. Use IsResolved()
// to find out whether a call would block before making it.
#pragma once
#include <string>

namespace wsl {

std::wstring Exe();

// Registered Ubuntu distro plus its POSIX home, e.g. "Ubuntu" and "/home/me".
// The result is cached for the process lifetime, including the failure message.
//
// BLOCKS for up to ~16 seconds on a cold cache (two `wsl.exe` probes, 8 s each),
// so never call it from a thread that paints. Once IsResolved() is true it answers
// from the cache and is cheap.
bool Resolve(std::wstring* distro, std::wstring* home, std::wstring* error);

// True once Resolve() can answer from its cache, success or failure. Never
// probes, so it is safe on any thread — use it to decide whether an action has to
// be moved to a worker first.
bool IsResolved();

// Outcome of a probe, for handing across a thread boundary.
struct Probe {
    bool ok = false;
    std::wstring error;
};

// Blocking Resolve() packaged as a value. Run this on a worker, then hand the
// Probe back to whichever thread asked.
Probe ResolveProbe();

// \\wsl.localhost\<distro>\<posix path with backslashes>
std::wstring UncPath(const std::wstring& distro, const std::wstring& posix_path);

// Convenience: UNC path for a file under the resolved distro's home. Inherits
// Resolve()'s blocking cost on a cold cache.
bool HomeFile(const std::wstring& relative_posix, std::wstring* unc_path, std::wstring* error);

}  // namespace wsl
