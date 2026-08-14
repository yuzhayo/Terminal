#include "features/app_shell.h"

#include <algorithm>
#include <utility>

#include "platform/strings.h"

namespace features {
namespace {

const std::vector<AppCommand> kCommandTable = {
    {L"Terminal",        L"--terminal",    ScreenId::Terminal,       true},
    {L"JSON INJECT",     L"--json-inject", ScreenId::JsonInject,     true},
    {L"Chrome Launcher", L"--chrome",      ScreenId::ChromeLauncher, true},
    // --exit and --tray have no screen and are not in the Jump List.
    // JSON Editor is also not a Jump List entry (matches original behaviour).
};

// Parent/back table — flat navigation, no stack.
struct BackEntry { ScreenId from; ScreenId to; };
const BackEntry kBackTable[] = {
    {ScreenId::JsonEditor,      ScreenId::JsonInject},
    {ScreenId::ChromeProfiles,  ScreenId::ChromeLauncher},
    {ScreenId::UiEditor,        ScreenId::Settings},
};

}  // namespace

const std::vector<AppCommand>& CommandTable() { return kCommandTable; }

std::optional<ScreenId> RouteFromArg(const std::wstring& arg) {
    for (const AppCommand& cmd : kCommandTable) {
        if (cmd.screen && arg == cmd.arg) return cmd.screen;
    }
    return std::nullopt;
}

std::optional<ScreenId> ParentOf(ScreenId screen) {
    for (const BackEntry& e : kBackTable)
        if (e.from == screen) return e.to;
    return std::nullopt;
}

std::vector<std::wstring> TokenizeCommandLine(const std::wstring& command_line) {
    // Simplified tokeniser: splits on whitespace, respects double-quoted tokens.
    // The old HasFlag() did a raw substring search which allowed "--terminal" to
    // match inside "--json-inject--terminal" or inside a path. This tokeniser
    // requires exact token boundaries.
    std::vector<std::wstring> tokens;
    size_t i = 0;
    const size_t n = command_line.size();
    while (i < n) {
        // Skip whitespace.
        while (i < n && (command_line[i] == L' ' || command_line[i] == L'\t')) ++i;
        if (i >= n) break;
        std::wstring token;
        if (command_line[i] == L'"') {
            ++i;  // skip opening quote
            while (i < n && command_line[i] != L'"') {
                if (command_line[i] == L'\\' && i + 1 < n && command_line[i + 1] == L'"') {
                    token += L'"'; i += 2;
                } else {
                    token += command_line[i++];
                }
            }
            if (i < n) ++i;  // skip closing quote
        } else {
            while (i < n && command_line[i] != L' ' && command_line[i] != L'\t')
                token += command_line[i++];
        }
        if (!token.empty()) tokens.push_back(std::move(token));
    }
    return tokens;
}

CommandEffect ApplyCommand(ShellState* state, const std::wstring& raw_command_line) {
    CommandEffect effect;
    const std::vector<std::wstring> tokens = TokenizeCommandLine(raw_command_line);

    // --exit takes priority over everything else.
    for (const std::wstring& tok : tokens) {
        if (tok == L"--exit") { effect.should_exit = true; return effect; }
    }

    // Route to a screen if any navigation flag is present.
    std::optional<ScreenId> route;
    for (const std::wstring& tok : tokens) {
        route = RouteFromArg(tok);
        if (route) break;
    }

    // --tray: just stay in tray mode, don't show window.
    bool tray_only = false;
    for (const std::wstring& tok : tokens)
        if (tok == L"--tray") { tray_only = true; break; }

    if (route) {
        effect.navigate_to = route;
        Navigate(state, *route);
    }
    if (!tray_only) effect.should_show = true;
    return effect;
}

void Navigate(ShellState* state, ScreenId id) {
    if (state->current && *state->current == id) return;
    state->current = id;
    state->pending = id;
}

void GoBack(ShellState* state) {
    if (!state->current) return;
    const auto parent = ParentOf(*state->current);
    if (parent) Navigate(state, *parent);
}

CloseAction OnClose(CloseAction policy) { return policy; }

bool CanHideToTray(bool tray_icon_added_successfully) {
    return tray_icon_added_successfully;
}

InstanceRole ClassifyInstance(bool mutex_was_new, bool forwarded_successfully) {
    if (mutex_was_new) return InstanceRole::First;
    if (forwarded_successfully) return InstanceRole::Secondary;
    return InstanceRole::PeerNotFound;
}

}  // namespace features
