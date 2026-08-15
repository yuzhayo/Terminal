#include "../core_gate.h"
#include <iostream>
#include <cassert>

using namespace application;
using namespace features;
using namespace core;

void TestInitialize() {
    CoreApplication app;
    app.Initialize();
    // Safe to call multiple times.
    app.Initialize();
    std::wcout << L"[PASS] Initialize\n";
}

void TestTerminalValidation() {
    CoreApplication app;
    app.Initialize();

    TerminalRequest req;
    req.folder = L"C:\\NonExistentFolder123456789";
    req.target = TerminalTarget::PowerShell;
    req.activate_venv = false;

    Status status = app.LaunchTerminal(req);
    assert(status.code == ErrorCode::FolderNotFound);
    std::wcout << L"[PASS] Terminal validation - folder not found\n";

    // Plan path reports the same folder error through the out-param.
    TerminalPlan plan;
    Status plan_status = app.PlanTerminalLaunch(req, &plan);
    assert(!plan.ok);
    assert(plan_status.code == ErrorCode::FolderNotFound);
    std::wcout << L"[PASS] Terminal plan - folder not found\n";
}

void TestClaudeInject() {
    CoreApplication app;
    app.Initialize();

    // Just verify the API compiles and doesn't crash.
    Status added = app.AddBaseUrl(L"https://api.anthropic.com");
    assert(added.ok());
    std::vector<std::wstring> urls = app.BaseUrls();
    assert(!urls.empty());

    std::wcout << L"[PASS] Claude inject API\n";
}

void TestChromeProfiles() {
    CoreApplication app;
    app.Initialize();

    // Switch runtime and verify no crash.
    app.SwitchChromeRuntime(ChromeRuntime::Windows);
    assert(app.ActiveChromeRuntime() == ChromeRuntime::Windows);

    std::wcout << L"[PASS] Chrome profiles API\n";
}

void TestSettings() {
    CoreApplication app;
    app.Initialize();

    // Theme and recent folders read through the facade.
    std::wstring theme = app.CurrentTheme();
    assert(theme == L"dark" || theme == L"light");

    std::wcout << L"[PASS] Settings API\n";
}

void TestEditorDraft() {
    CoreApplication app;
    app.Initialize();

    // Load the Windows settings file synchronously; verify the draft contract.
    EditorDraft draft;
    EditorLoadResult result = app.StartEditorLoad(EditorTarget::Windows, true, draft);
    Status status = app.ApplyEditorLoad(result, &draft);
    assert(status.ok());
    assert(draft.loaded);

    std::wcout << L"[PASS] Editor draft API\n";
}

int main() {
    try {
        std::wcout << L"Running CoreApplication contract tests...\n\n";

        TestInitialize();
        TestTerminalValidation();
        TestClaudeInject();
        TestChromeProfiles();
        TestSettings();
        TestEditorDraft();

        std::wcout << L"\nAll tests passed!\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
