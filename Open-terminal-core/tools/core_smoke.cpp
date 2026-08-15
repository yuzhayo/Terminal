// Smoke harness — verifies core business logic without any UI.
// Build: cl /std:c++20 /W4 /EHsc /I<root> tools/core_smoke.cpp platform/*.cpp storage/*.cpp features/*.cpp /link /out:core_smoke.exe
// Usage: core_smoke.exe <subcommand> [args]
//   terminal   <folder>           -- plan + run a PowerShell launch
//   inject     <target>           -- apply claude config to windows|wsl
//   editor     <target>           -- save/restore cycle on a temp file
//   chrome                        -- load cache, scan Windows, apply
//   settings                      -- theme set/get, start-with-windows read
#include <cstdio>
#include <string>
#include <vector>

#include "core_gate.h"
#include "platform/strings.h"
#include "platform/wsl.h"
#include "storage/settings.h"

namespace {

void println(const std::wstring& s) {
    wprintf(L"%ls\n", s.c_str());
}

void print_status(const core::Status& st) {
    const wchar_t* kind = L"None";
    switch (st.kind) {
        case core::StatusKind::Info:    kind = L"INFO";    break;
        case core::StatusKind::Success: kind = L"SUCCESS"; break;
        case core::StatusKind::Error:   kind = L"ERROR";   break;
        default: break;
    }
    wprintf(L"  [%ls] %ls\n", kind, st.text.c_str());
}

// ---- subcommands ----

int smoke_terminal(const std::vector<std::wstring>& args) {
    const std::wstring folder = args.empty() ? L"C:\\Users" : args[0];
    wprintf(L"=== terminal: folder=%ls\n", folder.c_str());

    features::TerminalRequest req;
    req.target = features::TerminalTarget::PowerShell;
    req.folder = folder;
    req.activate_venv = false;

    const features::TerminalPlan plan = features::Plan(req);
    wprintf(L"  Plan: ok=%d needs_wsl=%d error=%ls\n",
            plan.ok, plan.needs_wsl_probe, plan.error.c_str());
    if (!plan.ok) return 0;
    if (plan.needs_wsl_probe) {
        println(L"  (WSL probe needed — skipping Run in smoke)");
        return 0;
    }
    // Don't actually open a terminal in a smoke test; just show the plan result.
    println(L"  Plan passed. Run() would launch PowerShell.");
    return 0;
}

int smoke_inject(const std::vector<std::wstring>& args) {
    const std::wstring target_name = args.empty() ? L"windows" : args[0];
    wprintf(L"=== inject: target=%ls\n", target_name.c_str());

    features::InjectTarget target = features::InjectTargetFromName(target_name);
    features::EnsureDefaultTarget();

    // Add a base URL.
    auto s = features::AddBaseUrl(L"https://api.anthropic.com", L"Anthropic", L"");
    print_status(s);

    // Bulk-add a fake key.
    auto keys = features::ParseBulkKeys(L"smoke | sk-smoke-test-key-000000000000000");
    auto bulk = features::BulkAddApiKeys(keys);
    wprintf(L"  Bulk add: added=%d skipped=%d\n", bulk.added, bulk.skipped);

    if (features::InjectNeedsWslProbe(target)) {
        println(L"  (WSL probe needed — skipping Inject in smoke)");
        return 0;
    }
    auto result = features::Inject(target);
    print_status(result);
    return 0;
}

int smoke_editor(const std::vector<std::wstring>& args) {
    const std::wstring target_name = args.empty() ? L"windows" : args[0];
    wprintf(L"=== editor: target=%ls\n", target_name.c_str());

    features::EditorTarget target = features::EditorTargetFromName(target_name);
    features::EditorDraft draft;

    if (target == features::EditorTarget::UbuntuWsl && !wsl::IsResolved()) {
        println(L"  (WSL not resolved — skipping)");
        return 0;
    }

    auto result = features::StartLoad(target, true, draft);
    wprintf(L"  Load: resolved=%d read_ok=%d path=%ls\n",
            result.resolved, result.read_ok, result.path.c_str());
    auto st = features::ApplyLoad(result, &draft);
    print_status(st);
    wprintf(L"  Draft: loaded=%d dirty=%d text_len=%zu\n",
            draft.loaded, draft.dirty, draft.text.size());

    // Verify the empty-file seed.
    if (!result.read_ok || draft.text.empty()) {
        println(L"  Draft text empty or load failed.");
    }
    return 0;
}

int smoke_chrome(const std::vector<std::wstring>&) {
    println(L"=== chrome");
    features::LoadProfileCache();
    println(L"  Cache loaded.");

    auto scan = features::ScanProfiles(features::ChromeRuntime::Windows);
    wprintf(L"  Scan: ok=%d profiles=%zu\n", scan.ok, scan.profiles.size());

    auto st = features::ApplyScan(scan);
    print_status(st);

    wprintf(L"  EmptyState: %d\n", static_cast<int>(features::CardEmptyState()));
    return 0;
}

int smoke_settings(const std::vector<std::wstring>&) {
    println(L"=== settings");
    wprintf(L"  Theme: %ls\n", features::CurrentThemeToken().c_str());
    auto st = features::SetTheme(L"light");
    print_status(st);
    wprintf(L"  Theme after set: %ls\n", features::CurrentThemeToken().c_str());
    features::SetTheme(L"dark");  // reset

    const bool sww = features::IsStartWithWindowsEnabled();
    wprintf(L"  StartWithWindows (OS): %d\n", sww);

    const bool changed = features::SyncStartWithWindows();
    wprintf(L"  SyncStartWithWindows changed: %d\n", changed);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    storage::Load();

    if (argc < 2) {
        wprintf(L"Usage: core_smoke <terminal|inject|editor|chrome|settings> [args]\n");
        return 1;
    }

    const std::wstring cmd = argv[1];
    std::vector<std::wstring> args;
    for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);

    if (cmd == L"terminal")  return smoke_terminal(args);
    if (cmd == L"inject")    return smoke_inject(args);
    if (cmd == L"editor")    return smoke_editor(args);
    if (cmd == L"chrome")    return smoke_chrome(args);
    if (cmd == L"settings")  return smoke_settings(args);

    wprintf(L"Unknown subcommand: %ls\n", cmd.c_str());
    return 1;
}
