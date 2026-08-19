# AGENTS.md

This file provides guidance to the AI agent when working with code in this repository.

## Project

Native Win32 C++20 desktop app (MSBuild / VS2022 v143, x64 only, static CRT). Not Electron, no package.json. Deps (nlohmann/json, Velopack) are restored into `build/deps` by `tools/Restore-Dependencies.ps1` from `dependencies.lock.json` — never vendor them manually.

## Commands

- Build: `build.cmd Debug` / `build.cmd Release` (or `pwsh tools/Build.ps1 -Configuration Release`)
- Test: `pwsh tools/Test.ps1 -Configuration Debug` — single/filtered test: `tools/Test.ps1 -Filter <glob>` (custom test framework in `tests/test_main.cpp`, not GoogleTest)
- Version lives in `version.props`; change via `tools/Set-ProjectVersion.ps1`, never edit by hand
- New UI screen: `tools/New-UiScreen.ps1 <route-id>` then `tools/Merge-UiConfig.ps1` (see `/new-screen` skill)

## Code style

- Files `snake_case.{h,cpp}`; types PascalCase; member variables `snake_case_` with trailing underscore; `#pragma once`
- `.clang-format` (Google-based, 4-space, no include sorting) runs automatically on C++ edits via the PostToolUse hook; same hook strips CRLF on all text files
- **LF-only line endings** (`.gitattributes` eol=lf). `app.rc` embeds JSON bytes verbatim — CRLF flips corrupt it and break `git diff --check`, which is a completion criterion
- UI is JSON-driven from `Assets/ui/core.json` + `Assets/ui/screens/`. Screens must not be hardcoded
- UI components never call `src/logic` directly — business actions flow only through `src/application/adapters`
- `route-tabs` is permanent in every window; tabs are generated from screen `tabLabel`/`showInTabs`

## Active migration

`@Current.md` and `@New-Plan.md` define an in-progress structure-only move into `src/native/`, `src/updater/`, `distribution/`. Rules: no behavior/visual/namespace changes; move the window-shell group first; update `Terminal.vcxproj` includes and build after each group; diff must contain path changes only.

## Repo etiquette

- `Open-terminal`, `Open-terminal-native`, `Open-terminal-core` are old C# reference incarnations: read-only, never edit or depend on them
- `Playbook.md` and `Termial-plan.md` were intentionally deleted — do not restore
- Commit messages and docs: match the language of the surrounding file/history (mix of English and Indonesian)
- CI (`.github/workflows/ci.yml`) runs Debug+Release builds and tests on windows-2022; releases are manual-dispatch from `main` only with green CI

## Gotchas

- Only runtime env var: `TERMINAL_UPDATE_SOURCE` (updater)
- `docs/` and `Open-terminal*` are gitignored; don't be surprised they're absent from diffs
- .NET SDK 9.0.304 is pinned (`global.json`) solely for the `vpk` Velopack tool
