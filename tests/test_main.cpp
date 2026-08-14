#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "app/app_identity.h"
#include "platform/app_paths.h"
#include "platform/single_instance.h"
#include "platform/windows_runtime.h"
#include "rendering/render_runtime.h"
#include "rendering/layered_popup_render_context.h"
#include "rendering/native_peer_gdi_resource_cache.h"
#include "rendering/software_compositor.h"
#include "rendering/window_render_context.h"
#include "resource.h"
#include "ui/components/component_registry.h"
#include "ui/components/combo/combo_component.h"
#include "ui/components/dialog/dialog_component.h"
#include "ui/components/editable_draft_state.h"
#include "ui/components/input/native_peer_geometry.h"
#include "ui/components/scrollbar/scrollbar_component.h"
#include "ui/components/text/text_component.h"
#include "ui/application/stub_application_bridge.h"
#include "ui/config/ui_config_gate.h"
#include "ui/containers/logical_focus_coordinator.h"
#include "ui/containers/modal_overlay_stack.h"
#include "ui/containers/overlay_plane.h"
#include "ui/theme/theme_platform_adapter.h"

namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

#ifdef _DEBUG
constexpr char kConfiguration[] = "Debug";
#else
constexpr char kConfiguration[] = "Release";
#endif

class TestFailure final : public std::runtime_error {
public:
    TestFailure(std::string message, std::string file, int line)
        : std::runtime_error(std::move(message)), file_(std::move(file)), line_(line) {}

    const std::string& file() const noexcept { return file_; }
    int line() const noexcept { return line_; }

private:
    std::string file_;
    int line_ = 0;
};

void Require(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw TestFailure(std::string("Requirement failed: ") + expression, file, line);
    }
}

#define REQUIRE_TRUE(condition) Require((condition), #condition, __FILE__, __LINE__)

template <typename Function>
void RequireThrows(Function&& function, const char* expression, const char* file, int line) {
    bool threw = false;
    try {
        function();
    } catch (const std::exception&) {
        threw = true;
    }
    Require(threw, expression, file, line);
}

#define REQUIRE_THROWS(expression) \
    RequireThrows([&] { static_cast<void>(expression); }, #expression, __FILE__, __LINE__)

std::string ReadEmbeddedDefaultJson() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(IDR_UI_DEFAULT_JSON), RT_RCDATA);
    REQUIRE_TRUE(resource != nullptr);
    const DWORD size = SizeofResource(instance, resource);
    REQUIRE_TRUE(size > 0);
    const HGLOBAL loaded = LoadResource(instance, resource);
    const auto* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    REQUIRE_TRUE(bytes != nullptr);
    return std::string(bytes, bytes + size);
}

Json ReadEmbeddedDefaultDocument() {
    return Json::parse(ReadEmbeddedDefaultJson());
}

Json EmptyOverrideDocument() {
    return {
        {"schema", "yuzha.terminal.ui"},
        {"version", 1},
        {"documentKind", "override"},
        {"minimumReaderContract", 1},
        {"writtenBy", {{"appVersion", "0.1.0"}, {"configContract", 1}}},
        {"tokens", Json::object()},
        {"styles", Json::object()},
        {"windows", Json::object()},
        {"screens", Json::object()},
    };
}

class ScopedTestDirectory final {
public:
    explicit ScopedTestDirectory(std::string_view suffix) {
        root_ = std::filesystem::temp_directory_path() /
                ("Yuzha.Terminal.Tests-" + std::to_string(GetCurrentProcessId()) + "-" +
                 std::string(suffix));
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_);
    }

    ~ScopedTestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    const std::filesystem::path& path() const noexcept { return root_; }

private:
    std::filesystem::path root_;
};

class FocusProbeComponent final : public ui::components::Component {
public:
    FocusProbeComponent(const ui::config::ResolvedComponent& definition,
                        ui::components::ComponentHost& host, bool focusable)
        : Component(definition, host), focusable_(focusable) {}

    ui::components::MeasuredSize Measure(HDC, int available_width, int available_height) override {
        return {available_width, available_height};
    }
    void Paint(HDC) override {}
    bool CanFocus() const noexcept override { return focusable_; }
    bool FocusNativePeer() override {
        ++native_focus_calls;
        return true;
    }
    void SetLogicalFocus(bool focused, bool window_active) override {
        logical_focused = focused;
        active = window_active;
    }
    bool HandleKeyDown(UINT virtual_key) override {
        last_key = virtual_key;
        return true;
    }

    int native_focus_calls = 0;
    bool logical_focused = false;
    bool active = true;
    UINT last_key = 0;

private:
    bool focusable_ = false;
};

class ModalProbeComponent final : public ui::components::Component {
public:
    ModalProbeComponent(const ui::config::ResolvedComponent& definition,
                        ui::components::ComponentHost& host, bool focusable = false,
                        bool modal = false, bool suppressible = false)
        : Component(definition, host), focusable_(focusable), modal_(modal),
          suppressible_(suppressible) {}

    ui::components::MeasuredSize Measure(HDC, int available_width, int available_height) override {
        return {available_width, available_height};
    }
    void Paint(HDC) override {}
    bool CanFocus() const noexcept override { return focusable_ && (!modal_ || active_); }
    bool FocusNativePeer() override { return true; }
    void CollectFocusable(std::vector<ui::components::Component*>& focusable) override {
        if (!modal_ || active_) Component::CollectFocusable(focusable);
    }
    bool RequiresNativePeerSuppression() const noexcept override { return suppressible_; }
    bool SuspendNativePeers(std::wstring& diagnostic) override {
        ++suspend_calls;
        if (fail_suspend) {
            diagnostic = L"probe suspend failure";
            return false;
        }
        suspended = true;
        diagnostic.clear();
        return true;
    }
    void ResumeNativePeers() override {
        ++resume_calls;
        suspended = false;
    }
    bool IsModalOverlay() const noexcept override { return modal_; }
    bool IsModalActive() const noexcept override { return active_; }
    bool ActivateModal(std::wstring& diagnostic) override {
        if (!modal_ || active_) return false;
        active_ = true;
        diagnostic.clear();
        return true;
    }
    bool DeactivateModal(std::wstring& diagnostic) override {
        if (!modal_ || !active_) return false;
        active_ = false;
        diagnostic.clear();
        return true;
    }
    void CompleteModal(ui::components::ModalResult result) override { last_result = result; }

    int suspend_calls = 0;
    int resume_calls = 0;
    bool fail_suspend = false;
    bool suspended = false;
    std::optional<ui::components::ModalResult> last_result;

private:
    bool focusable_ = false;
    bool modal_ = false;
    bool suppressible_ = false;
    bool active_ = false;
};

platform::AppPaths MakeTestPaths(const std::filesystem::path& root) {
    platform::AppPaths paths;
    paths.data_root = root.wstring();
    paths.ui_override = (root / "ui" / "override.v1.json").wstring();
    paths.ui_config_log = (root / "logs" / "ui-config.log").wstring();
    paths.updater_state = (root / "updater" / "state.json").wstring();
    return paths;
}

void WriteUtf8(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE_TRUE(output.good());
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    REQUIRE_TRUE(output.good());
}

struct TestCase {
    std::string name;
    std::function<void()> run;
    std::string file;
    int line = 0;
};

struct TestResult {
    std::string name;
    std::string status;
    double duration_ms = 0.0;
    std::string message;
    std::string file;
    int line = 0;
};

struct Options {
    std::string filter = "*";
    std::filesystem::path json_report =
        std::filesystem::path("artifacts") / "test-results" / kConfiguration / "results.json";
    std::filesystem::path junit_report =
        std::filesystem::path("artifacts") / "test-results" / kConfiguration / "results.junit.xml";
};

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr,
                                             nullptr);
    if (required <= 0) {
        throw std::runtime_error("Argument is not valid Unicode.");
    }

    std::string converted(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), converted.data(),
                                            required, nullptr, nullptr);
    if (written != required) {
        throw std::runtime_error("Argument could not be converted to UTF-8.");
    }
    return converted;
}

bool GlobMatches(std::string_view pattern, std::string_view value) {
    std::size_t pattern_index = 0;
    std::size_t value_index = 0;
    std::size_t star_index = std::string_view::npos;
    std::size_t star_value_index = 0;

    while (value_index < value.size()) {
        if (pattern_index < pattern.size() &&
            (pattern[pattern_index] == '?' || pattern[pattern_index] == value[value_index])) {
            ++pattern_index;
            ++value_index;
        } else if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
            star_index = pattern_index++;
            star_value_index = value_index;
        } else if (star_index != std::string_view::npos) {
            pattern_index = star_index + 1;
            value_index = ++star_value_index;
        } else {
            return false;
        }
    }

    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

std::string UtcNow() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << time.wYear << '-' << std::setw(2) << time.wMonth
           << '-' << std::setw(2) << time.wDay << 'T' << std::setw(2) << time.wHour << ':'
           << std::setw(2) << time.wMinute << ':' << std::setw(2) << time.wSecond << '.'
           << std::setw(3) << time.wMilliseconds << 'Z';
    return output.str();
}

std::string EscapeXml(std::string_view value) {
    std::string escaped;
    for (const char character : value) {
        switch (character) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '\"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            default:
                escaped += character;
                break;
        }
    }
    return escaped;
}

void EnsureParentDirectory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void WriteJsonReport(const Options& options, const std::string& started_utc, double duration_ms,
                     const std::vector<TestResult>& results) {
    const std::size_t passed = static_cast<std::size_t>(std::count_if(
        results.begin(), results.end(), [](const TestResult& result) { return result.status == "passed"; }));

    Json report = {
        {"schemaVersion", 1},
        {"configuration", kConfiguration},
        {"startedUtc", started_utc},
        {"durationMs", duration_ms},
        {"totals", {{"selected", results.size()}, {"passed", passed},
                    {"failed", results.size() - passed}}},
        {"tests", Json::array()},
    };

    for (const TestResult& result : results) {
        report["tests"].push_back({
            {"name", result.name},       {"status", result.status},
            {"durationMs", result.duration_ms}, {"message", result.message},
            {"file", result.file},       {"line", result.line},
        });
    }

    EnsureParentDirectory(options.json_report);
    std::ofstream output(options.json_report, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create JSON report: " + options.json_report.string());
    }
    output << report.dump(2) << '\n';
}

void WriteJunitReport(const Options& options, const std::string& started_utc, double duration_ms,
                      const std::vector<TestResult>& results) {
    const std::size_t failures = static_cast<std::size_t>(std::count_if(
        results.begin(), results.end(), [](const TestResult& result) { return result.status == "failed"; }));

    EnsureParentDirectory(options.junit_report);
    std::ofstream output(options.junit_report, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot create JUnit report: " + options.junit_report.string());
    }

    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<testsuite name=\"TerminalTests\" tests=\"" << results.size()
           << "\" failures=\"" << failures << "\" time=\"" << duration_ms / 1000.0
           << "\" timestamp=\"" << EscapeXml(started_utc) << "\">\n";
    for (const TestResult& result : results) {
        output << "  <testcase classname=\"TerminalTests\" name=\"" << EscapeXml(result.name)
               << "\" time=\"" << result.duration_ms / 1000.0 << "\">\n";
        if (result.status == "failed") {
            output << "    <failure message=\"" << EscapeXml(result.message) << "\" file=\""
                   << EscapeXml(result.file) << "\" line=\"" << result.line << "\"/>\n";
        }
        output << "  </testcase>\n";
    }
    output << "</testsuite>\n";
}

bool EndsWith(std::wstring_view value, std::wstring_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size(), suffix.size()) == suffix;
}

void TestAppIdentityContract() {
    REQUIRE_TRUE(std::wcscmp(app_identity::kProductName, L"Terminal") == 0);
    REQUIRE_TRUE(std::wcscmp(app_identity::kApplicationId, L"Yuzha.Terminal") == 0);
    REQUIRE_TRUE(std::wcscmp(app_identity::kExecutableName, L"Terminal.exe") == 0);
    REQUIRE_TRUE(std::string_view(app_identity::kUiSchema) == "yuzha.terminal.ui");
    REQUIRE_TRUE(app_identity::kUiSchemaVersion == 1);
}

void TestAppPathsContract() {
    platform::AppPaths paths;
    std::wstring diagnostic;
    REQUIRE_TRUE(platform::ResolveAppPaths(paths, diagnostic));
    REQUIRE_TRUE(diagnostic.empty());
    REQUIRE_TRUE(EndsWith(paths.data_root, L"\\Yuzha\\Terminal"));
    REQUIRE_TRUE(EndsWith(paths.ui_override, L"\\Yuzha\\Terminal\\ui\\override.v1.json"));
    REQUIRE_TRUE(EndsWith(paths.ui_config_log, L"\\Yuzha\\Terminal\\logs\\ui-config.log"));
    REQUIRE_TRUE(EndsWith(paths.updater_state, L"\\Yuzha\\Terminal\\updater\\state.json"));
}

void TestIconResourceEmbedded() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    REQUIRE_TRUE(FindResourceW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), RT_GROUP_ICON) != nullptr);
    REQUIRE_TRUE(LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON)) != nullptr);
}

void TestUiConfigGateEmbeddedDefault() {
    ScopedTestDirectory directory("embedded");
    platform::AppPaths paths = MakeTestPaths(directory.path());
    std::wstring diagnostic;
    ui::config::UiConfigGate gate(GetModuleHandleW(nullptr), paths);
    REQUIRE_TRUE(gate.ResolveBootstrap(diagnostic));
    REQUIRE_TRUE(diagnostic.empty());
    REQUIRE_TRUE(gate.document() != nullptr);
    REQUIRE_TRUE(gate.document()->generation == 1);
    REQUIRE_TRUE(gate.metadata().schema == app_identity::kUiSchema);
    REQUIRE_TRUE(gate.metadata().version == app_identity::kUiSchemaVersion);
    REQUIRE_TRUE(gate.metadata().minimum_reader_contract <= app_identity::kReaderContract);
    REQUIRE_TRUE(gate.metadata().written_by_config_contract == app_identity::kWriterContract);
    REQUIRE_TRUE(gate.document()->windows.size() == 1);
    REQUIRE_TRUE(gate.document()->screens.size() == 7);
    REQUIRE_TRUE(gate.document()->theme(ui::config::ThemeKind::Dark).styles.size() ==
                 gate.document()->theme(ui::config::ThemeKind::Light).styles.size());
    const auto& light_styles = gate.document()->theme(ui::config::ThemeKind::Light).styles;
    const auto title_style = std::find_if(light_styles.begin(), light_styles.end(),
                                          [](const ui::config::ResolvedStyle& style) {
                                              return style.id == "text-title";
                                          });
    const auto mono_style = std::find_if(light_styles.begin(), light_styles.end(),
                                         [](const ui::config::ResolvedStyle& style) {
                                             return style.id == "text-monospace";
                                         });
    REQUIRE_TRUE(title_style != light_styles.end());
    REQUIRE_TRUE(title_style->font.family == "Segoe UI");
    REQUIRE_TRUE(title_style->font.point_size == 12);
    REQUIRE_TRUE(mono_style != light_styles.end());
    REQUIRE_TRUE(mono_style->font.family == "Cascadia Mono");
    REQUIRE_TRUE(mono_style->font.fallback_family == "Consolas");
    REQUIRE_TRUE(std::holds_alternative<ui::config::SystemColorSlot>(
        gate.document()->theme(ui::config::ThemeKind::HighContrast).tokens.at("window")));
    const auto& main_window = gate.document()->windows.at("main");
    REQUIRE_TRUE(main_window.type == ui::config::ComponentType::Window);
    REQUIRE_TRUE(main_window.children.size() == 2);
    const auto& shell = main_window.children.front();
    REQUIRE_TRUE(shell.type == ui::config::ComponentType::Container);
    REQUIRE_TRUE(shell.children.size() == 7);
    REQUIRE_TRUE(shell.children[0].type == ui::config::ComponentType::Text);
    REQUIRE_TRUE(shell.children[2].type == ui::config::ComponentType::Input);
    REQUIRE_TRUE(shell.children[3].type == ui::config::ComponentType::Combo);
    REQUIRE_TRUE(shell.children[4].type == ui::config::ComponentType::Card);
    REQUIRE_TRUE(shell.children[4].children.size() == 2);
    REQUIRE_TRUE(shell.children[4].children[0].type == ui::config::ComponentType::Checkbox);
    REQUIRE_TRUE(shell.children[4].children[1].type == ui::config::ComponentType::Toggle);
    REQUIRE_TRUE(shell.children[5].type == ui::config::ComponentType::Button);
    REQUIRE_TRUE(shell.children[6].type == ui::config::ComponentType::Button);
    const auto& dialog = main_window.children[1];
    REQUIRE_TRUE(dialog.type == ui::config::ComponentType::Dialog);
    REQUIRE_TRUE(dialog.children.size() == 2);
}

void TestScrollbarMetrics() {
    const ui::components::ScrollbarMetrics idle =
        ui::components::CalculateScrollbarMetrics(100, 24, 0, 0, 100, 0);
    REQUIRE_TRUE(idle.track_length == 100);
    REQUIRE_TRUE(idle.thumb_start == 0);
    REQUIRE_TRUE(idle.thumb_length == 100);

    const ui::components::ScrollbarMetrics start =
        ui::components::CalculateScrollbarMetrics(100, 24, 0, 300, 100, 0);
    const ui::components::ScrollbarMetrics end =
        ui::components::CalculateScrollbarMetrics(100, 24, 0, 300, 100, 300);
    REQUIRE_TRUE(start.thumb_length == 25);
    REQUIRE_TRUE(start.thumb_start == 0);
    REQUIRE_TRUE(end.thumb_length == 25);
    REQUIRE_TRUE(end.thumb_start == 75);
}

void TestComponentRegistryVerticalSlice() {
    ui::components::ComponentRegistry registry;
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Window));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Container));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Text));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Button));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Input));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Screen));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Checkbox));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Toggle));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Card));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Scrollbar));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Combo));
    REQUIRE_TRUE(registry.Supports(ui::config::ComponentType::Dialog));
}

void TestComboPopupPlacement() {
    const RECT work{0, 0, 800, 600};
    const auto below = ui::components::CalculateComboPopupPlacement(
        RECT{100, 100, 300, 132}, work, SIZE{220, 160}, 2);
    REQUIRE_TRUE(!below.opens_above);
    REQUIRE_TRUE(below.origin.x == 100 && below.origin.y == 134);

    const auto above = ui::components::CalculateComboPopupPlacement(
        RECT{700, 550, 780, 582}, work, SIZE{220, 160}, 2);
    REQUIRE_TRUE(above.opens_above);
    REQUIRE_TRUE(above.origin.x == 580 && above.origin.y == 388);
}

void TestModalOverlayStackNestedSuppressionAndFocus() {
    rendering::RenderRuntime runtime;
    ui::config::ResolvedTheme theme;
    theme.styles.push_back(ui::config::ResolvedStyle{});
    ui::components::ComponentHost host;
    host.render_runtime = &runtime;
    host.theme = &theme;
    const auto definition = [](std::string id) {
        ui::config::ResolvedComponent value;
        value.id = std::move(id);
        value.style_index = 0;
        return value;
    };

    ModalProbeComponent root(definition("root"), host);
    auto background_focus = std::make_unique<ModalProbeComponent>(
        definition("background-focus"), host, true);
    auto background_peer = std::make_unique<ModalProbeComponent>(
        definition("background-peer"), host, false, false, true);
    auto outer = std::make_unique<ModalProbeComponent>(definition("outer"), host, false, true);
    auto outer_focus = std::make_unique<ModalProbeComponent>(definition("outer-focus"), host, true);
    auto outer_peer = std::make_unique<ModalProbeComponent>(
        definition("outer-peer"), host, false, false, true);
    auto inner = std::make_unique<ModalProbeComponent>(definition("inner"), host, false, true);
    auto inner_focus = std::make_unique<ModalProbeComponent>(definition("inner-focus"), host, true);

    auto* background_focus_pointer = background_focus.get();
    auto* background_peer_pointer = background_peer.get();
    auto* outer_pointer = outer.get();
    auto* outer_focus_pointer = outer_focus.get();
    auto* outer_peer_pointer = outer_peer.get();
    auto* inner_pointer = inner.get();
    auto* inner_focus_pointer = inner_focus.get();
    inner->AddChild(std::move(inner_focus));
    outer->AddChild(std::move(outer_focus));
    outer->AddChild(std::move(outer_peer));
    outer->AddChild(std::move(inner));
    root.AddChild(std::move(background_focus));
    root.AddChild(std::move(background_peer));
    root.AddChild(std::move(outer));

    ui::containers::LogicalFocusCoordinator focus;
    focus.Rebuild(root);
    REQUIRE_TRUE(focus.RequestFocus(background_focus_pointer));
    ui::containers::ModalOverlayStack stack;
    std::wstring diagnostic;

    background_peer_pointer->fail_suspend = true;
    REQUIRE_TRUE(!stack.Push(*outer_pointer, root, focus, diagnostic));
    REQUIRE_TRUE(!stack.active());
    REQUIRE_TRUE(stack.suppression_depth(background_peer_pointer) == 0);
    background_peer_pointer->fail_suspend = false;

    REQUIRE_TRUE(stack.Push(*outer_pointer, root, focus, diagnostic));
    REQUIRE_TRUE(stack.size() == 1);
    REQUIRE_TRUE(stack.suppression_depth(background_peer_pointer) == 1);
    REQUIRE_TRUE(background_peer_pointer->suspended);
    REQUIRE_TRUE(focus.scope() == outer_pointer);
    REQUIRE_TRUE(focus.focused() == outer_focus_pointer);
    REQUIRE_TRUE(!focus.RequestFocus(background_focus_pointer));

    REQUIRE_TRUE(stack.Push(*inner_pointer, root, focus, diagnostic));
    REQUIRE_TRUE(stack.size() == 2);
    REQUIRE_TRUE(stack.suppression_depth(background_peer_pointer) == 2);
    REQUIRE_TRUE(stack.suppression_depth(outer_peer_pointer) == 1);
    REQUIRE_TRUE(outer_peer_pointer->suspended);
    REQUIRE_TRUE(focus.focused() == inner_focus_pointer);

    REQUIRE_TRUE(stack.Pop(ui::components::ModalResult::Cancel, root, focus, diagnostic));
    REQUIRE_TRUE(inner_pointer->last_result == ui::components::ModalResult::Cancel);
    REQUIRE_TRUE(stack.suppression_depth(background_peer_pointer) == 1);
    REQUIRE_TRUE(stack.suppression_depth(outer_peer_pointer) == 0);
    REQUIRE_TRUE(!outer_peer_pointer->suspended);
    REQUIRE_TRUE(focus.focused() == outer_focus_pointer);

    REQUIRE_TRUE(stack.Pop(ui::components::ModalResult::Discard, root, focus, diagnostic));
    REQUIRE_TRUE(outer_pointer->last_result == ui::components::ModalResult::Discard);
    REQUIRE_TRUE(!stack.active());
    REQUIRE_TRUE(!background_peer_pointer->suspended);
    REQUIRE_TRUE(focus.focused() == background_focus_pointer);
}

void TestDialogExplicitDismissPolicy() {
    rendering::RenderRuntime runtime;
    ui::config::ResolvedTheme theme;
    theme.styles.push_back(ui::config::ResolvedStyle{});
    ui::components::ComponentHost host;
    host.render_runtime = &runtime;
    host.theme = &theme;
    ui::config::ResolvedComponent definition;
    definition.id = "policy-dialog";
    definition.type = ui::config::ComponentType::Dialog;
    definition.style_index = 0;
    ui::config::DialogProperties properties;
    properties.title = std::string("Policy");
    properties.dismiss_explicit_action = false;
    definition.properties = properties;
    ui::components::DialogComponent dialog(definition, host);
    REQUIRE_TRUE(!dialog.CanCompleteModal(ui::components::ModalResult::Accept));
    REQUIRE_TRUE(!dialog.CanCompleteModal(ui::components::ModalResult::Discard));
    REQUIRE_TRUE(!dialog.CanCompleteModal(ui::components::ModalResult::Cancel));
    REQUIRE_TRUE(dialog.CanCompleteModal(ui::components::ModalResult::Dismiss));
}

void TestLayeredPopupPremultipliedSurface() {
    rendering::RenderRuntime runtime;
    rendering::LayeredPopupRenderContext context(runtime);
    REQUIRE_TRUE(context.EnsureSize(32, 24));
    context.Clear();
    context.SourceOver({0, 0, 32, 24}, {200, 100, 50, 128});
    const std::uint32_t pixel = context.PixelAt(4, 4);
    REQUIRE_TRUE((pixel >> 24) == 128);
    REQUIRE_TRUE((pixel & 0xFFu) <= 128u);
    REQUIRE_TRUE(((pixel >> 8) & 0xFFu) <= 128u);
    REQUIRE_TRUE(((pixel >> 16) & 0xFFu) <= 128u);
    REQUIRE_TRUE(runtime.diagnostics().active_layered_popup_contexts == 1);
}

void TestWindowRenderContextPersistentAllocation() {
    rendering::WindowRenderContext context;
    HDC screen = GetDC(nullptr);
    REQUIRE_TRUE(screen != nullptr);
    REQUIRE_TRUE(context.EnsureSize(screen, 64, 48));
    const std::uint64_t first_generation = context.allocation_generation();
    REQUIRE_TRUE(first_generation == 1);
    REQUIRE_TRUE(context.EnsureSize(screen, 64, 48));
    REQUIRE_TRUE(context.allocation_generation() == first_generation);
    REQUIRE_TRUE(context.EnsureSize(screen, 80, 48));
    REQUIRE_TRUE(context.allocation_generation() == first_generation + 1);
    REQUIRE_TRUE(context.width() == 80);
    REQUIRE_TRUE(context.height() == 48);
    ReleaseDC(nullptr, screen);
}

void TestWindowRenderContextInvalidationUnion() {
    rendering::RenderRuntime runtime;
    rendering::WindowRenderContext context(&runtime);
    HDC screen = GetDC(nullptr);
    REQUIRE_TRUE(screen != nullptr);
    REQUIRE_TRUE(context.EnsureSize(screen, 80, 60));
    RECT invalid{};
    REQUIRE_TRUE(context.TakeInvalidation(invalid));
    REQUIRE_TRUE(invalid.left == 0 && invalid.top == 0 && invalid.right == 80 &&
                 invalid.bottom == 60);

    context.Invalidate(RECT{10, 12, 20, 22});
    context.Invalidate(RECT{2, 18, 14, 40});
    REQUIRE_TRUE(context.TakeInvalidation(invalid));
    REQUIRE_TRUE(invalid.left == 2 && invalid.top == 12 && invalid.right == 20 &&
                 invalid.bottom == 40);
    REQUIRE_TRUE(!context.TakeInvalidation(invalid));

    const std::uint64_t generation = context.allocation_generation();
    REQUIRE_TRUE(!context.EnsureSize(screen, 0, 60));
    REQUIRE_TRUE(context.valid());
    REQUIRE_TRUE(context.width() == 80 && context.height() == 60);
    REQUIRE_TRUE(context.allocation_generation() == generation);
    ReleaseDC(nullptr, screen);
}

void TestSoftwareSourceOver() {
    const rendering::RgbaColor red{255, 0, 0, 128};
    const std::uint32_t red_over_black =
        rendering::SourceOverPremultiplied(0xFF000000u, rendering::Premultiply(red));
    REQUIRE_TRUE(red_over_black == 0xFF800000u);
    REQUIRE_TRUE(rendering::SourceOverPremultiplied(0xFF123456u, 0x00000000u) ==
                 0xFF123456u);

    std::vector<std::uint32_t> pixels(4, 0xFFFFFFFFu);
    rendering::SourceOverSolid(pixels.data(), 2, 2, 2, RECT{-2, 0, 1, 2},
                               rendering::RgbaColor{0, 0, 255, 255});
    REQUIRE_TRUE(pixels[0] == 0xFF0000FFu);
    REQUIRE_TRUE(pixels[1] == 0xFFFFFFFFu);
    REQUIRE_TRUE(pixels[2] == 0xFF0000FFu);
    REQUIRE_TRUE(pixels[3] == 0xFFFFFFFFu);
}

void TestRenderRuntimeCornerCacheAndEpoch() {
    rendering::RenderRuntime runtime;
    rendering::WindowRenderContext context(&runtime);
    int redraw_requests = 0;
    context.SetRedrawRequest([&redraw_requests] { ++redraw_requests; });
    HDC screen = GetDC(nullptr);
    REQUIRE_TRUE(screen != nullptr);
    REQUIRE_TRUE(context.EnsureSize(screen, 16, 16));
    RECT ignored{};
    REQUIRE_TRUE(context.TakeInvalidation(ignored));
    REQUIRE_TRUE(runtime.diagnostics().active_window_contexts == 1);

    const RECT bounds{0, 0, 12, 12};
    for (unsigned int index = 0; index < 96; ++index) {
        REQUIRE_TRUE(runtime.PaintRoundedStyleBox(
            context.dc(), bounds, 4, 1,
            rendering::RgbaColor{static_cast<BYTE>(index), 80, 120, 255},
            rendering::RgbaColor{20, 30, 40, 255}, RGB(1, 2, 3), 96, index));
    }
    REQUIRE_TRUE(runtime.diagnostics().cached_corner_tiles == 96);
    REQUIRE_TRUE(runtime.PaintRoundedStyleBox(
        context.dc(), bounds, 4, 1, rendering::RgbaColor{200, 80, 120, 255},
        rendering::RgbaColor{20, 30, 40, 255}, RGB(1, 2, 3), 96, 96));
    REQUIRE_TRUE(runtime.diagnostics().cached_corner_tiles == 1);

    const std::uint64_t previous_epoch = runtime.resource_epoch();
    runtime.AdvanceResourceEpoch();
    REQUIRE_TRUE(runtime.resource_epoch() == previous_epoch + 1);
    REQUIRE_TRUE(runtime.diagnostics().cached_corner_tiles == 0);
    REQUIRE_TRUE(context.resource_epoch() == runtime.resource_epoch());
    REQUIRE_TRUE(context.has_invalidation());
    REQUIRE_TRUE(redraw_requests == 1);
    ReleaseDC(nullptr, screen);
}

void TestRenderRuntimePaintHotPathAndHeadlessMeasure() {
    rendering::RenderRuntime runtime;
    ui::config::ResolvedTheme theme;
    ui::config::ResolvedStyle style;
    style.font = {"Segoe UI", "", 9, 400};
    style.radius = 4;
    style.border_width = 1;
    for (auto& state : style.states) {
        state.background = ui::config::LiteralRgba{20, 30, 40, 255};
        state.foreground = ui::config::LiteralRgba{240, 240, 240, 255};
        state.border = ui::config::LiteralRgba{80, 90, 100, 255};
    }
    theme.styles.push_back(style);
    REQUIRE_TRUE(runtime.PrepareStyleResources(theme.styles.front(), 96, RGB(0, 0, 0)));
    const auto prepared = runtime.diagnostics();

    rendering::WindowRenderContext context(&runtime);
    HDC screen = GetDC(nullptr);
    REQUIRE_TRUE(screen != nullptr && context.EnsureSize(screen, 32, 32));
    runtime.BeginPaintScope();
    REQUIRE_TRUE(runtime.PaintRoundedStyleBox(
        context.dc(), RECT{0, 0, 24, 24}, 4, 1, rendering::RgbaColor{20, 30, 40, 255},
        rendering::RgbaColor{80, 90, 100, 255}, RGB(0, 0, 0), 96, 0));
    runtime.EndPaintScope();
    const auto painted = runtime.diagnostics();
    REQUIRE_TRUE(painted.cached_fonts == prepared.cached_fonts);
    REQUIRE_TRUE(painted.cached_brushes == prepared.cached_brushes);
    REQUIRE_TRUE(painted.cached_pens == prepared.cached_pens);
    REQUIRE_TRUE(painted.cached_corner_tiles == prepared.cached_corner_tiles);

    ui::components::ComponentHost host;
    host.dpi = 96;
    host.render_runtime = &runtime;
    host.render_context = &context;
    host.theme = &theme;
    ui::config::ResolvedComponent definition;
    definition.id = "headless-text";
    definition.type = ui::config::ComponentType::Text;
    definition.style_index = 0;
    definition.properties = ui::config::TextProperties{std::string("مرحبا"),
        ui::config::TextVariant::Body, false, false, ui::config::TextAlign::Start};
    ui::components::TextComponent text(definition, host);
    const auto measured = text.Measure(nullptr, 300, 100);
    REQUIRE_TRUE(measured.width > 0 && measured.height > 0);
    ReleaseDC(nullptr, screen);
}

void TestWindowRenderContextRoundedSourceOver() {
    rendering::WindowRenderContext context;
    HDC screen = GetDC(nullptr);
    REQUIRE_TRUE(screen != nullptr && context.EnsureSize(screen, 4, 4));
    context.SourceOver(RECT{0, 0, 4, 4}, rendering::RgbaColor{0, 0, 255, 255});
    context.SourceOverRounded(RECT{0, 0, 4, 4}, 0, 0,
                              rendering::RgbaColor{255, 0, 0, 128},
                              rendering::RgbaColor{0, 0, 0, 0});
    const std::uint32_t expected = rendering::SourceOverPremultiplied(
        0xFF0000FFu, rendering::Premultiply(rendering::RgbaColor{255, 0, 0, 128}));
    REQUIRE_TRUE(context.PixelAt(1, 1) == expected);
    ReleaseDC(nullptr, screen);
}

void TestOverlayPlanePaintOrder() {
    rendering::RenderRuntime runtime;
    rendering::WindowRenderContext context(&runtime);
    HDC screen = GetDC(nullptr);
    REQUIRE_TRUE(screen != nullptr);
    REQUIRE_TRUE(context.EnsureSize(screen, 2, 2));
    const RECT pixel{0, 0, 1, 1};
    context.SourceOver(pixel, rendering::RgbaColor{0, 0, 0, 255});

    ui::containers::OverlayPlane plane;
    const auto red_layer = plane.Push([](rendering::WindowRenderContext& surface,
                                         const RECT& invalid) {
        surface.SourceOver(invalid, rendering::RgbaColor{255, 0, 0, 128});
    });
    const auto blue_layer = plane.Push([](rendering::WindowRenderContext& surface,
                                          const RECT& invalid) {
        surface.SourceOver(invalid, rendering::RgbaColor{0, 0, 255, 128});
    });
    REQUIRE_TRUE(red_layer != 0 && blue_layer != 0 && plane.size() == 2);
    plane.Paint(context, pixel);
    std::uint32_t expected = rendering::SourceOverPremultiplied(
        0xFF000000u, rendering::Premultiply(rendering::RgbaColor{255, 0, 0, 128}));
    expected = rendering::SourceOverPremultiplied(
        expected, rendering::Premultiply(rendering::RgbaColor{0, 0, 255, 128}));
    REQUIRE_TRUE(context.PixelAt(0, 0) == expected);
    REQUIRE_TRUE(plane.Remove(red_layer));
    REQUIRE_TRUE(!plane.Remove(red_layer));
    REQUIRE_TRUE(plane.size() == 1);
    plane.Clear();
    REQUIRE_TRUE(plane.size() == 0);
    ReleaseDC(nullptr, screen);
}

void TestNativePeerGdiResourceLeaseSharing() {
    rendering::NativePeerGdiResourceCache cache;
    ui::config::ResolvedFont descriptor{"Segoe UI", "", 9, 400};
    {
        auto first = cache.AcquireFont(descriptor, 96);
        auto second = cache.AcquireFont(descriptor, 96);
        REQUIRE_TRUE(static_cast<bool>(first));
        REQUIRE_TRUE(static_cast<bool>(second));
        REQUIRE_TRUE(first.get() == second.get());
        REQUIRE_TRUE(cache.physical_font_count() == 1);
        REQUIRE_TRUE(cache.active_font_lease_count() == 2);
        first.Reset();
        REQUIRE_TRUE(cache.physical_font_count() == 1);
        REQUIRE_TRUE(cache.active_font_lease_count() == 1);
    }
    REQUIRE_TRUE(cache.physical_font_count() == 0);
    REQUIRE_TRUE(cache.active_font_lease_count() == 0);

    {
        auto first = cache.AcquireBrush(RGB(10, 20, 30));
        auto second = cache.AcquireBrush(RGB(10, 20, 30));
        REQUIRE_TRUE(static_cast<bool>(first));
        REQUIRE_TRUE(static_cast<bool>(second));
        REQUIRE_TRUE(first.get() == second.get());
        REQUIRE_TRUE(cache.physical_brush_count() == 1);
        REQUIRE_TRUE(cache.active_brush_lease_count() == 2);
    }
    REQUIRE_TRUE(cache.physical_brush_count() == 0);
    REQUIRE_TRUE(cache.active_brush_lease_count() == 0);
}

void TestLogicalFocusCoordinatorTraversal() {
    rendering::RenderRuntime runtime;
    ui::config::ResolvedTheme theme;
    theme.styles.push_back(ui::config::ResolvedStyle{});
    ui::components::ComponentHost host;
    host.render_runtime = &runtime;
    host.theme = &theme;
    ui::config::ResolvedComponent root_definition;
    root_definition.style_index = 0;
    ui::config::ResolvedComponent first_definition;
    first_definition.style_index = 0;
    ui::config::ResolvedComponent second_definition;
    second_definition.style_index = 0;

    FocusProbeComponent root(root_definition, host, false);
    auto first = std::make_unique<FocusProbeComponent>(first_definition, host, true);
    auto second = std::make_unique<FocusProbeComponent>(second_definition, host, true);
    FocusProbeComponent* first_pointer = first.get();
    FocusProbeComponent* second_pointer = second.get();
    root.AddChild(std::move(first));
    root.AddChild(std::move(second));

    ui::containers::LogicalFocusCoordinator coordinator;
    coordinator.Rebuild(root);
    REQUIRE_TRUE(coordinator.focusable_count() == 2);
    REQUIRE_TRUE(coordinator.Move(false));
    REQUIRE_TRUE(coordinator.focused() == first_pointer);
    REQUIRE_TRUE(first_pointer->logical_focused);
    REQUIRE_TRUE(first_pointer->native_focus_calls == 1);
    REQUIRE_TRUE(coordinator.Move(false));
    REQUIRE_TRUE(coordinator.focused() == second_pointer);
    REQUIRE_TRUE(!first_pointer->logical_focused);
    REQUIRE_TRUE(second_pointer->logical_focused);
    REQUIRE_TRUE(coordinator.Move(true));
    REQUIRE_TRUE(coordinator.focused() == first_pointer);
    coordinator.SetWindowActive(false);
    REQUIRE_TRUE(first_pointer->logical_focused);
    REQUIRE_TRUE(!first_pointer->active);
    coordinator.SetWindowActive(true);
    REQUIRE_TRUE(first_pointer->active);
    REQUIRE_TRUE(coordinator.HandleKeyDown(VK_RETURN));
    REQUIRE_TRUE(first_pointer->last_key == VK_RETURN);
}

void TestEditableDraftStateTransactions() {
    ui::components::EditableDraftState state(L"baseline");
    REQUIRE_TRUE(!state.is_dirty());
    state.Update(L"draft");
    REQUIRE_TRUE(state.is_dirty());
    state.ApplySaveResult(false);
    REQUIRE_TRUE(state.is_dirty());
    state.ApplySaveResult(true);
    REQUIRE_TRUE(!state.is_dirty());
    REQUIRE_TRUE(state.baseline() == L"draft");
    state.Update(L"discard-me");
    REQUIRE_TRUE(state.StageDiscard());
    REQUIRE_TRUE(state.discard_staged());
    REQUIRE_TRUE(state.value() == L"draft");
    state.RollbackDiscard();
    REQUIRE_TRUE(state.value() == L"discard-me");
    REQUIRE_TRUE(state.is_dirty());
    REQUIRE_TRUE(state.StageDiscard());
    state.CommitDiscard();
    REQUIRE_TRUE(!state.discard_staged());
    REQUIRE_TRUE(state.value() == L"draft");
    REQUIRE_TRUE(!state.is_dirty());
}

void TestNativePeerGeometryContainment() {
    const RECT component{0, 0, 100, 40};
    const RECT peer{8, 6, 92, 34};
    REQUIRE_TRUE(ui::components::ValidateNativePeerGeometry(component, peer, {}));
    const RECT outside{-1, 6, 92, 34};
    REQUIRE_TRUE(!ui::components::ValidateNativePeerGeometry(component, outside, {}));
    const RECT reserved_overlap{80, 0, 100, 40};
    REQUIRE_TRUE(!ui::components::ValidateNativePeerGeometry(
        component, peer, std::span<const RECT>(&reserved_overlap, 1)));
    const RECT reserved_clear{0, 0, 6, 40};
    REQUIRE_TRUE(ui::components::ValidateNativePeerGeometry(
        component, peer, std::span<const RECT>(&reserved_clear, 1)));
}

void TestUiConfigDuplicateKeyRejected() {
    const std::string embedded = ReadEmbeddedDefaultJson();
    const std::string duplicate = "{\"schema\":\"yuzha.terminal.ui\"," + embedded.substr(1);
    REQUIRE_THROWS(ui::config::detail::ResolveDocuments(duplicate, std::nullopt, 1));
}

void TestUiConfigUnknownFieldRejected() {
    Json embedded = ReadEmbeddedDefaultDocument();
    embedded["unexpected"] = true;
    const std::string bytes = embedded.dump();
    REQUIRE_THROWS(ui::config::detail::ResolveDocuments(bytes, std::nullopt, 1));
}

void TestUiConfigMissingAndCyclicReferencesRejected() {
    Json missing = ReadEmbeddedDefaultDocument();
    missing["styles"]["window"]["states"]["normal"]["background"]["$ref"] =
        "tokens.doesNotExist";
    const std::string missing_bytes = missing.dump();
    REQUIRE_THROWS(ui::config::detail::ResolveDocuments(missing_bytes, std::nullopt, 1));

    Json cyclic = ReadEmbeddedDefaultDocument();
    cyclic["tokens"]["dark"]["accent"] = {{"$ref", "tokens.accentHover"}};
    cyclic["tokens"]["dark"]["accentHover"] = {{"$ref", "tokens.accent"}};
    const std::string cyclic_bytes = cyclic.dump();
    REQUIRE_THROWS(ui::config::detail::ResolveDocuments(cyclic_bytes, std::nullopt, 1));
}

void TestUiConfigOverrideMergeAndArrayReplacement() {
    const std::string embedded = ReadEmbeddedDefaultJson();
    Json override_document = EmptyOverrideDocument();
    override_document["tokens"]["dark"]["accent"] = "#010203";
    override_document["screens"]["terminal"]["children"] = Json::array();
    const std::string override_bytes = override_document.dump();

    const auto result = ui::config::detail::ResolveDocuments(embedded, override_bytes, 7);
    REQUIRE_TRUE(result.document != nullptr);
    REQUIRE_TRUE(!result.override_diagnostic.has_value());
    REQUIRE_TRUE(result.document->generation == 7);
    const auto color = std::get<ui::config::LiteralRgba>(
        result.document->theme(ui::config::ThemeKind::Dark).tokens.at("accent"));
    REQUIRE_TRUE((color == ui::config::LiteralRgba{1, 2, 3, 255}));
    REQUIRE_TRUE(result.document->screens.at("terminal").children.empty());
}

void TestUiConfigOverrideRejectedAsWhole() {
    const std::string embedded = ReadEmbeddedDefaultJson();
    Json override_document = EmptyOverrideDocument();
    override_document["unknown"] = true;
    const std::string override_bytes = override_document.dump();

    const auto result = ui::config::detail::ResolveDocuments(embedded, override_bytes, 5);
    REQUIRE_TRUE(result.document != nullptr);
    REQUIRE_TRUE(result.override_diagnostic.has_value());
    REQUIRE_TRUE(result.override_diagnostic->code == "unknown-field");
    REQUIRE_TRUE(result.document->generation == 5);
    REQUIRE_TRUE(result.document->screens.at("terminal").children.size() == 1);
}

void TestUiConfigRollbackIncompatibleOverridePreserved() {
    const std::string embedded = ReadEmbeddedDefaultJson();
    Json override_document = EmptyOverrideDocument();
    override_document["minimumReaderContract"] = app_identity::kReaderContract + 1;
    const std::string override_bytes = override_document.dump();

    const auto result = ui::config::detail::ResolveDocuments(embedded, override_bytes, 1);
    REQUIRE_TRUE(result.override_diagnostic.has_value());
    REQUIRE_TRUE(result.override_diagnostic->rollback_incompatible);
    REQUIRE_TRUE(result.override_diagnostic->code == "reader-contract");
}

void TestUiConfigAllComponentSchemasResolve() {
    Json embedded = ReadEmbeddedDefaultDocument();
    Json& children = embedded["screens"]["terminal"]["children"];
    children.push_back({{"id", "layout"}, {"type", "Container"},
                        {"style", {{"$ref", "styles.surface"}}}, {"children", Json::array()}});
    children.push_back({{"id", "action"}, {"type", "Button"},
                        {"style", {{"$ref", "styles.button-primary"}}}, {"label", "Run"},
                        {"events", {{"click", {{"action", "run-stub"}, {"payload", Json::object()}}}}}});
    children.push_back({{"id", "input"}, {"type", "Input"},
                        {"style", {{"$ref", "styles.input"}}},
                        {"valueBinding", {{"$bind", "viewState.inputValue"}}},
                        {"placeholder", "Value"}});
    children.push_back({{"id", "combo"}, {"type", "Combo"},
                        {"style", {{"$ref", "styles.combo"}}},
                        {"itemsBinding", {{"$bind", "viewState.items"}}},
                        {"selectedValueBinding", {{"$bind", "viewState.selectedValue"}}},
                        {"placeholder", "Choose"}});
    children.push_back({{"id", "checkbox"}, {"type", "Checkbox"},
                        {"style", {{"$ref", "styles.checkbox"}}}, {"label", "Checked"},
                        {"checkedBinding", {{"$bind", "viewState.checked"}}}});
    children.push_back({{"id", "toggle"}, {"type", "Toggle"},
                        {"style", {{"$ref", "styles.toggle"}}}, {"label", "Enabled"},
                        {"checkedBinding", {{"$bind", "viewState.enabled"}}}});
    children.push_back({{"id", "card"}, {"type", "Card"},
                        {"style", {{"$ref", "styles.card"}}}, {"children", Json::array()}});
    children.push_back({{"id", "list"}, {"type", "List"},
                        {"style", {{"$ref", "styles.list"}}},
                        {"automation", {{"name", "Items"}}},
                        {"itemsBinding", {{"$bind", "viewState.items"}}},
                        {"itemTemplate", {{"id", "list-item"}, {"type", "Card"},
                                          {"style", {{"$ref", "styles.card"}}},
                                          {"children", Json::array()}}}});
    children.push_back({{"id", "scrollbar"}, {"type", "Scrollbar"},
                        {"style", {{"$ref", "styles.scrollbar"}}},
                        {"automation", {{"name", "Scroll"}}}});
    children.push_back({{"id", "dialog"}, {"type", "Dialog"},
                        {"style", {{"$ref", "styles.dialog"}}}, {"title", "Confirm"},
                        {"children", Json::array()}});
    const std::string bytes = embedded.dump();

    const auto result = ui::config::detail::ResolveDocuments(bytes, std::nullopt, 1);
    REQUIRE_TRUE(result.document != nullptr);
    REQUIRE_TRUE(result.document->screens.at("terminal").children.size() == 11);
}

void TestUiConfigNativeSurfaceAlphaRejected() {
    Json embedded = ReadEmbeddedDefaultDocument();
    embedded["tokens"]["dark"]["input"] = "#181B2180";
    embedded["screens"]["terminal"]["children"].push_back(
        {{"id", "input"}, {"type", "Input"}, {"style", {{"$ref", "styles.input"}}},
         {"valueBinding", {{"$bind", "viewState.inputValue"}}}, {"placeholder", "Value"}});
    const std::string bytes = embedded.dump();
    REQUIRE_THROWS(ui::config::detail::ResolveDocuments(bytes, std::nullopt, 1));
}

void TestUiConfigHighContrastMappingRejected() {
    Json embedded = ReadEmbeddedDefaultDocument();
    embedded["tokens"]["highContrast"]["accent"] = {{"$systemColor", "windowText"}};
    const std::string bytes = embedded.dump();
    REQUIRE_THROWS(ui::config::detail::ResolveDocuments(bytes, std::nullopt, 1));
}

void TestUiConfigComponentRangeRejected() {
    Json embedded = ReadEmbeddedDefaultDocument();
    embedded["screens"]["terminal"]["children"].push_back(
        {{"id", "combo"}, {"type", "Combo"}, {"style", {{"$ref", "styles.combo"}}},
         {"automation", {{"name", "Choose"}}},
         {"itemsBinding", {{"$bind", "viewState.items"}}},
         {"selectedValueBinding", {{"$bind", "viewState.selectedValue"}}},
         {"maxVisibleItems", 51}});
    const std::string bytes = embedded.dump();
    REQUIRE_THROWS(ui::config::detail::ResolveDocuments(bytes, std::nullopt, 1));
}

void TestUiConfigReloadKeepsLastKnownGood() {
    ScopedTestDirectory directory("reload");
    const platform::AppPaths paths = MakeTestPaths(directory.path());
    ui::config::UiConfigGate gate(GetModuleHandleW(nullptr), paths);
    std::wstring diagnostic;
    REQUIRE_TRUE(gate.ResolveBootstrap(diagnostic));
    const auto original = gate.document();
    REQUIRE_TRUE(original != nullptr);

    WriteUtf8(paths.ui_override, "{}");
    REQUIRE_TRUE(!gate.Reload(diagnostic));
    REQUIRE_TRUE(!diagnostic.empty());
    REQUIRE_TRUE(gate.document() == original);
    REQUIRE_TRUE(gate.document()->generation == 1);
    REQUIRE_TRUE(gate.active_diagnostic().has_value());
    REQUIRE_TRUE(gate.active_diagnostic()->source == WideToUtf8(paths.ui_override));
    REQUIRE_TRUE(!gate.active_diagnostic_text().empty());

    Json override_document = EmptyOverrideDocument();
    override_document["tokens"]["light"]["accent"] = "#102030";
    WriteUtf8(paths.ui_override, override_document.dump());
    REQUIRE_TRUE(gate.Reload(diagnostic));
    REQUIRE_TRUE(diagnostic.empty());
    REQUIRE_TRUE(gate.document()->generation == 2);
    REQUIRE_TRUE(!gate.active_diagnostic().has_value());
}

void TestUiConfigBootstrapRejectsOverrideAndUsesDefault() {
    ScopedTestDirectory directory("bootstrap-invalid-override");
    const platform::AppPaths paths = MakeTestPaths(directory.path());
    WriteUtf8(paths.ui_override, "{}");
    ui::config::UiConfigGate gate(GetModuleHandleW(nullptr), paths);
    std::wstring diagnostic;
    REQUIRE_TRUE(gate.ResolveBootstrap(diagnostic));
    REQUIRE_TRUE(diagnostic.empty());
    REQUIRE_TRUE(gate.document() != nullptr);
    REQUIRE_TRUE(gate.document()->screens.size() == 7);
    REQUIRE_TRUE(gate.active_diagnostic().has_value());
    REQUIRE_TRUE(std::filesystem::is_regular_file(paths.ui_config_log));
}

void TestUiConfigVersionRejected() {
    Json embedded = ReadEmbeddedDefaultDocument();
    embedded["version"] = 2;
    const std::string embedded_bytes = embedded.dump();
    REQUIRE_THROWS(ui::config::detail::ResolveDocuments(embedded_bytes, std::nullopt, 1));

    const std::string valid_embedded = ReadEmbeddedDefaultJson();
    Json override_document = EmptyOverrideDocument();
    override_document["version"] = 2;
    const std::string override_bytes = override_document.dump();
    const auto result =
        ui::config::detail::ResolveDocuments(valid_embedded, override_bytes, 1);
    REQUIRE_TRUE(result.override_diagnostic.has_value());
    REQUIRE_TRUE(result.override_diagnostic->code == "unsupported-version");
}

void TestUiConfigNeverReadsLegacyUiJson() {
    ScopedTestDirectory directory("legacy");
    const platform::AppPaths paths = MakeTestPaths(directory.path());
    WriteUtf8(directory.path() / "ui.json", "not-json");
    ui::config::UiConfigGate gate(GetModuleHandleW(nullptr), paths);
    std::wstring diagnostic;
    REQUIRE_TRUE(gate.ResolveBootstrap(diagnostic));
    REQUIRE_TRUE(diagnostic.empty());
    REQUIRE_TRUE(!gate.active_diagnostic().has_value());
}

void TestThemePlatformAdapterContract() {
    ui::theme::ThemePlatformAdapter adapter(
        {false, ui::theme::PlatformAppTheme::Dark, false});
    REQUIRE_TRUE(adapter.Select(ui::config::ThemePreference::System) ==
                 ui::config::ThemeKind::Dark);
    REQUIRE_TRUE(adapter.Select(ui::config::ThemePreference::Light) ==
                 ui::config::ThemeKind::Light);
    REQUIRE_TRUE(adapter.resource_epoch() == 1);
    REQUIRE_TRUE(adapter.QueueBackgroundSnapshot(
        {false, ui::theme::PlatformAppTheme::Light, false}));
    REQUIRE_TRUE(!adapter.QueueBackgroundSnapshot(
        {true, ui::theme::PlatformAppTheme::Light, false}));
    REQUIRE_TRUE(adapter.ApplyQueuedSnapshot());
    REQUIRE_TRUE(adapter.resource_epoch() == 2);
    REQUIRE_TRUE(adapter.Select(ui::config::ThemePreference::Dark) ==
                 ui::config::ThemeKind::HighContrast);
    REQUIRE_TRUE(!adapter.ApplyQueuedSnapshot());
    std::wstring diagnostic;
    REQUIRE_TRUE(!adapter.StartPostFirstFrameMonitoring(nullptr, WM_APP + 1, diagnostic));
    REQUIRE_TRUE(!diagnostic.empty());
}

void TestSingleInstanceIpcContract() {
    const std::wstring mutex_name = platform::BuildCurrentUserMutexName();
    REQUIRE_TRUE(mutex_name.starts_with(L"Local\\Yuzha.Terminal.Instance.v1."));
    REQUIRE_TRUE(mutex_name.size() == std::wstring(L"Local\\Yuzha.Terminal.Instance.v1.").size() + 32);

    const platform::IpcRequest request{
        "01234567-89ab-cdef-0123-456789abcdef", platform::IpcCommand::OpenRoute, "settings"};
    const std::string payload = platform::SerializeIpcRequest(request);
    REQUIRE_TRUE(!payload.empty());
    const auto parsed = platform::ParseIpcPayload(payload.data(), payload.size());
    REQUIRE_TRUE(parsed.request.has_value());
    REQUIRE_TRUE(*parsed.request == request);

    const std::string unknown =
        R"({"protocol":"yuzha.terminal.ipc","version":1,"requestId":"01234567-89ab-cdef-0123-456789abcdef","command":"nope","arguments":{}})";
    REQUIRE_TRUE(platform::ParseIpcPayload(unknown.data(), unknown.size()).error ==
                 platform::IpcError::UnsupportedCommand);
    const std::string duplicate =
        R"({"protocol":"yuzha.terminal.ipc","protocol":"yuzha.terminal.ipc","version":1,"requestId":"01234567-89ab-cdef-0123-456789abcdef","command":"activate-default","arguments":{}})";
    REQUIRE_TRUE(!platform::ParseIpcPayload(duplicate.data(), duplicate.size()).request.has_value());
}

void TestStubApplicationBridgePatch() {
    ui::application::StubApplicationBridge bridge;
    const auto profiles = bridge.ResolveStringItems("terminalProfiles");
    REQUIRE_TRUE(profiles.size() == 3);
    REQUIRE_TRUE(profiles.front() == L"PowerShell");
    const auto patch = bridge.Dispatch({"run-terminal-stub", {}});
    REQUIRE_TRUE(patch.has_value());
    REQUIRE_TRUE(patch->generation == 1);
    REQUIRE_TRUE(patch->view_state.at("stubStatus") == "completed");
    REQUIRE_TRUE(patch->window_title.has_value());
    REQUIRE_TRUE(patch->request_repaint);
    const auto dialog = bridge.Dispatch({"open-save-discard-dialog", {}});
    REQUIRE_TRUE(dialog.has_value() && dialog->dialog_request.has_value());
    REQUIRE_TRUE(dialog->dialog_request->action == ui::application::DialogRequestAction::Open);
    REQUIRE_TRUE(dialog->dialog_request->dialog_id == "save-discard-dialog");
    REQUIRE_TRUE(!bridge.Dispatch({"unknown-action", {}}).has_value());
}

void TestWindowsRuntimeMinimumBuild() {
    std::wstring diagnostic;
    REQUIRE_TRUE(platform::CheckWindowsRuntime(diagnostic) ==
                 platform::WindowsRuntimeStatus::Supported);
    REQUIRE_TRUE(diagnostic.empty());
}

std::vector<TestCase> DiscoverTests() {
    std::vector<TestCase> tests = {
        {"AppIdentity.Contract", TestAppIdentityContract, __FILE__, __LINE__},
        {"AppPaths.Contract", TestAppPathsContract, __FILE__, __LINE__},
        {"ComponentRegistry.VerticalSlice", TestComponentRegistryVerticalSlice, __FILE__, __LINE__},
        {"Combo.PopupPlacement", TestComboPopupPlacement, __FILE__, __LINE__},
        {"Dialog.ExplicitDismissPolicy", TestDialogExplicitDismissPolicy, __FILE__, __LINE__},
        {"EditableDraftState.Transactions", TestEditableDraftStateTransactions, __FILE__, __LINE__},
        {"IconResource.Embedded", TestIconResourceEmbedded, __FILE__, __LINE__},
        {"LogicalFocusCoordinator.Traversal", TestLogicalFocusCoordinatorTraversal, __FILE__, __LINE__},
        {"ModalOverlayStack.NestedSuppressionAndFocus", TestModalOverlayStackNestedSuppressionAndFocus, __FILE__, __LINE__},
        {"LayeredPopupRenderContext.PremultipliedSurface", TestLayeredPopupPremultipliedSurface, __FILE__, __LINE__},
        {"NativePeerGdiResourceCache.LeaseSharing", TestNativePeerGdiResourceLeaseSharing, __FILE__, __LINE__},
        {"NativePeerGeometry.Containment", TestNativePeerGeometryContainment, __FILE__, __LINE__},
        {"OverlayPlane.PaintOrder", TestOverlayPlanePaintOrder, __FILE__, __LINE__},
        {"RenderRuntime.CornerCacheAndEpoch", TestRenderRuntimeCornerCacheAndEpoch, __FILE__, __LINE__},
        {"RenderRuntime.PaintHotPathAndHeadlessMeasure", TestRenderRuntimePaintHotPathAndHeadlessMeasure, __FILE__, __LINE__},
        {"SoftwareCompositor.SourceOver", TestSoftwareSourceOver, __FILE__, __LINE__},
        {"SingleInstance.IpcContract", TestSingleInstanceIpcContract, __FILE__, __LINE__},
        {"Scrollbar.Metrics", TestScrollbarMetrics, __FILE__, __LINE__},
        {"StubApplicationBridge.Patch", TestStubApplicationBridgePatch, __FILE__, __LINE__},
        {"ThemePlatformAdapter.Contract", TestThemePlatformAdapterContract, __FILE__, __LINE__},
        {"UiConfigGate.AllComponentSchemas", TestUiConfigAllComponentSchemasResolve, __FILE__, __LINE__},
        {"UiConfigGate.BootstrapInvalidOverrideFallback", TestUiConfigBootstrapRejectsOverrideAndUsesDefault, __FILE__, __LINE__},
        {"UiConfigGate.ComponentRangeRejected", TestUiConfigComponentRangeRejected, __FILE__, __LINE__},
        {"UiConfigGate.DuplicateKeyRejected", TestUiConfigDuplicateKeyRejected, __FILE__, __LINE__},
        {"UiConfigGate.EmbeddedDefault", TestUiConfigGateEmbeddedDefault, __FILE__, __LINE__},
        {"UiConfigGate.HighContrastMappingRejected", TestUiConfigHighContrastMappingRejected, __FILE__, __LINE__},
        {"UiConfigGate.LegacyUiJsonIgnored", TestUiConfigNeverReadsLegacyUiJson, __FILE__, __LINE__},
        {"UiConfigGate.MissingAndCyclicReferences", TestUiConfigMissingAndCyclicReferencesRejected, __FILE__, __LINE__},
        {"UiConfigGate.NativeSurfaceAlphaRejected", TestUiConfigNativeSurfaceAlphaRejected, __FILE__, __LINE__},
        {"UiConfigGate.OverrideMergeArrayReplacement", TestUiConfigOverrideMergeAndArrayReplacement, __FILE__, __LINE__},
        {"UiConfigGate.OverrideRejectedAsWhole", TestUiConfigOverrideRejectedAsWhole, __FILE__, __LINE__},
        {"UiConfigGate.ReloadLastKnownGood", TestUiConfigReloadKeepsLastKnownGood, __FILE__, __LINE__},
        {"UiConfigGate.RollbackIncompatible", TestUiConfigRollbackIncompatibleOverridePreserved, __FILE__, __LINE__},
        {"UiConfigGate.UnknownFieldRejected", TestUiConfigUnknownFieldRejected, __FILE__, __LINE__},
        {"UiConfigGate.VersionRejected", TestUiConfigVersionRejected, __FILE__, __LINE__},
        {"WindowRenderContext.PersistentAllocation", TestWindowRenderContextPersistentAllocation, __FILE__, __LINE__},
        {"WindowRenderContext.InvalidationUnion", TestWindowRenderContextInvalidationUnion, __FILE__, __LINE__},
        {"WindowRenderContext.RoundedSourceOver", TestWindowRenderContextRoundedSourceOver, __FILE__, __LINE__},
        {"WindowsRuntime.MinimumBuild", TestWindowsRuntimeMinimumBuild, __FILE__, __LINE__},
    };
    std::sort(tests.begin(), tests.end(),
              [](const TestCase& left, const TestCase& right) { return left.name < right.name; });
    return tests;
}

Options ParseOptions(int argument_count, wchar_t** arguments) {
    Options options;
    bool filter_seen = false;
    bool json_seen = false;
    bool junit_seen = false;

    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (index + 1 >= argument_count) {
            throw std::runtime_error("Missing value for argument: " + WideToUtf8(argument));
        }

        const std::wstring_view value(arguments[++index]);
        if (argument == L"--filter" && !filter_seen) {
            filter_seen = true;
            options.filter = WideToUtf8(value);
        } else if (argument == L"--report-json" && !json_seen) {
            json_seen = true;
            options.json_report = std::filesystem::path(value);
        } else if (argument == L"--report-junit" && !junit_seen) {
            junit_seen = true;
            options.junit_report = std::filesystem::path(value);
        } else {
            throw std::runtime_error("Unknown or repeated argument: " + WideToUtf8(argument));
        }
    }
    return options;
}

void PrintUsage() {
    std::wcerr << L"Usage: TerminalTests.exe [--filter <glob>] [--report-json <path>] "
                  L"[--report-junit <path>]\n";
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
    try {
        const Options options = ParseOptions(argument_count, arguments);
        const std::vector<TestCase> discovered = DiscoverTests();
        if (discovered.empty()) {
            std::cerr << "No tests were discovered.\n";
            return 2;
        }

        std::vector<TestCase> selected;
        std::copy_if(discovered.begin(), discovered.end(), std::back_inserter(selected),
                     [&](const TestCase& test) { return GlobMatches(options.filter, test.name); });
        if (selected.empty()) {
            std::cerr << "The filter selected no tests: " << options.filter << '\n';
            return 2;
        }

        const std::string started_utc = UtcNow();
        const Clock::time_point suite_start = Clock::now();
        std::vector<TestResult> results;
        results.reserve(selected.size());

        for (const TestCase& test : selected) {
            TestResult result{test.name, "passed", 0.0, "", test.file, test.line};
            const Clock::time_point test_start = Clock::now();
            try {
                test.run();
            } catch (const TestFailure& failure) {
                result.status = "failed";
                result.message = failure.what();
                result.file = failure.file();
                result.line = failure.line();
            } catch (const std::exception& error) {
                result.status = "failed";
                result.message = error.what();
            } catch (...) {
                result.status = "failed";
                result.message = "Unknown C++ exception.";
            }
            result.duration_ms =
                std::chrono::duration<double, std::milli>(Clock::now() - test_start).count();
            std::cout << (result.status == "passed" ? "PASS " : "FAIL ") << result.name;
            if (!result.message.empty()) {
                std::cout << " - " << result.message;
            }
            std::cout << '\n';
            results.push_back(std::move(result));
        }

        const double duration_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - suite_start).count();
        WriteJsonReport(options, started_utc, duration_ms, results);
        WriteJunitReport(options, started_utc, duration_ms, results);

        const bool any_failed = std::any_of(results.begin(), results.end(),
                                            [](const TestResult& result) {
                                                return result.status == "failed";
                                            });
        return any_failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "TerminalTests runner error: " << error.what() << '\n';
        PrintUsage();
        return 2;
    }
}
