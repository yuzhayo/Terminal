#include "platform/wsl.h"

#include <atomic>
#include <mutex>

#include "platform/paths.h"
#include "platform/process.h"
#include "platform/strings.h"

namespace wsl {
namespace {

bool ResolveDistro(std::wstring* distro, std::wstring* error) {
    std::wstring output;
    unsigned long exit_code = 0;
    const std::wstring command = str::QuoteArg(Exe()) + L" --list --quiet";
    if (!process::RunCapture(command, {}, 8000, &output, &exit_code, error)) return false;
    if (exit_code != 0) {
        *error = L"WSL is not available on this machine.";
        return false;
    }
    for (const std::wstring& line : str::SplitLines(output)) {
        std::wstring name = str::Trim(line);
        // wsl.exe pads its list output with NULs; strip anything unprintable.
        while (!name.empty() && name.back() < L' ') name.pop_back();
        if (!name.empty() && str::IContains(name, L"ubuntu")) {
            *distro = name;
            return true;
        }
    }
    *error = L"No Ubuntu distribution is registered in WSL.";
    return false;
}

bool ResolveHome(const std::wstring& distro, std::wstring* home, std::wstring* error) {
    std::wstring output;
    unsigned long exit_code = 0;
    const std::wstring command =
        str::QuoteArg(Exe()) + L" -d " + str::QuoteArg(distro) + L" -- sh -c \"printf %s \\\"$HOME\\\"\"";
    if (!process::RunCapture(command, {}, 8000, &output, &exit_code, error)) return false;
    std::wstring value = str::Trim(output);
    while (!value.empty() && value.back() < L' ') value.pop_back();
    if (exit_code != 0 || value.empty() || value.front() != L'/') {
        *error = L"Cannot determine the home directory inside " + distro + L".";
        return false;
    }
    *home = value;
    return true;
}

}  // namespace

std::wstring Exe() {
    return paths::Join(paths::ExpandEnvironment(L"%SystemRoot%"), L"System32\\wsl.exe");
}

namespace {

// Process-wide probe result. Several workers can ask at once (a Chrome scan while
// the JSON Editor loads a path), so the cache is guarded.
//
// `settled` is separate and atomic on purpose: the lock is held for the whole
// 8-second probe, so IsResolved() must not take it — callers use it to decide
// whether they need a worker at all, and blocking there would defeat the point.
struct Cache {
    std::mutex lock;
    std::atomic<bool> settled{false};
    std::wstring distro;
    std::wstring home;
    std::wstring error;
};

Cache& Cached() {
    // Deliberately never destroyed: a worker can still be inside Resolve() while
    // the process tears down, and destroying the mutex under it would be worse
    // than one struct left behind at exit.
    static Cache* cache = new Cache();
    return *cache;
}

}  // namespace

bool IsResolved() { return Cached().settled.load(std::memory_order_acquire); }

bool Resolve(std::wstring* distro, std::wstring* home, std::wstring* error) {
    Cache& cache = Cached();
    std::lock_guard<std::mutex> guard(cache.lock);

    if (!cache.distro.empty()) {
        if (distro) *distro = cache.distro;
        if (home) *home = cache.home;
        return true;
    }
    if (!cache.error.empty()) {
        if (error) *error = cache.error;
        return false;
    }

    // wsl.exe --list waits up to 8 seconds when no distro is running, so the
    // first probe must happen off any thread that paints. Cache hits above are
    // exempt. Core cannot check the caller's thread, so this is a contract, not
    // an assert — see IsResolved().

    std::wstring found_distro;
    std::wstring found_home;
    if (!ResolveDistro(&found_distro, &cache.error) || !ResolveHome(found_distro, &found_home, &cache.error)) {
        if (cache.error.empty()) cache.error = L"WSL is not available.";
        cache.settled.store(true, std::memory_order_release);
        if (error) *error = cache.error;
        return false;
    }
    cache.distro = found_distro;
    cache.home = found_home;
    cache.settled.store(true, std::memory_order_release);
    if (distro) *distro = cache.distro;
    if (home) *home = cache.home;
    return true;
}

Probe ResolveProbe() {
    Probe probe;
    probe.ok = Resolve(nullptr, nullptr, &probe.error);
    return probe;
}

std::wstring UncPath(const std::wstring& distro, const std::wstring& posix_path) {
    std::wstring converted;
    converted.reserve(posix_path.size() + 1);
    if (posix_path.empty() || posix_path.front() != L'/') converted.push_back(L'\\');
    for (wchar_t c : posix_path) converted.push_back(c == L'/' ? L'\\' : c);
    return L"\\\\wsl.localhost\\" + distro + converted;
}

bool HomeFile(const std::wstring& relative_posix, std::wstring* unc_path, std::wstring* error) {
    std::wstring distro;
    std::wstring home;
    if (!Resolve(&distro, &home, error)) return false;
    std::wstring posix = home;
    if (!relative_posix.empty() && relative_posix.front() != L'/') posix.push_back(L'/');
    posix += relative_posix;
    *unc_path = UncPath(distro, posix);
    return true;
}

}  // namespace wsl
