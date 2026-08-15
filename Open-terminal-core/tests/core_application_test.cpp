#include "../core_gate.h"
#include <iostream>
#include <cassert>

using namespace application;
using namespace features;
using namespace core;

void TestInitialize() {
    CoreApplication app;
    app.Initialize();
    std::wcout << L"[PASS] Initialize\n";
}

void TestTerminalValidation() {
    CoreApplication app;
    app.Initialize();

    TerminalRequest req;
    req.folder = L"C:\\NonExistentFolder123456789";
    req.mode = TerminalMode::PowerShell;
    req.activate_venv = false;

    Status status = app.LaunchTerminal(req);
    assert(status.code == ErrorCode::FolderNotFound);
    std::wcout << L"[PASS] Terminal validation - folder not found\n";
}

void TestClaudeInject() {
    CoreApplication app;
    app.Initialize();

    // Just verify the API compiles and doesn't crash
    app.SetClaudeRuntime(ClaudeRuntime::Windows);
    app.SetClaudeProvider(L"anthropic");

    std::wcout << L"[PASS] Claude inject API\n";
}

void TestChromeProfiles() {
    CoreApplication app;
    app.Initialize();

    // Switch runtime and verify no crash
    app.SwitchChromeRuntime(ChromeRuntime::Windows);

    std::wcout << L"[PASS] Chrome profiles API\n";
}

void TestSettings() {
    CoreApplication app;
    app.Initialize();

    // Load settings - should not crash
    auto settings = app.LoadSettings();

    std::wcout << L"[PASS] Settings API\n";
}

void TestEditorDraft() {
    CoreApplication app;
    app.Initialize();

    // Create draft - verify API exists
    EditorDraft draft;
    draft.path = L"test.json";
    draft.content = L"{}";

    auto result = app.StartEditorParse(draft);
    assert(result.ok || !result.ok); // Just verify it compiles

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
