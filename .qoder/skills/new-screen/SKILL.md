---
name: new-screen
description: Scaffold a new JSON-driven UI screen (route) with tabs integration. Use when the user asks to add a new screen, route, or tab to the app UI.
disable-model-invocation: true
---

Create a new UI screen. `$ARGUMENTS` is the route id (kebab-case, e.g. `settings` or `update-feed`); an optional second argument is the tab title.

1. Validate the route id matches `^[a-z0-9]+(-[a-z0-9]+)*$`.
2. Create the screen JSON:

   ```powershell
   pwsh tools/New-UiScreen.ps1 <route-id> [-Title "<Title>"]
   ```

   This writes `Assets/ui/screens/<route-id>.json` with `routeId`, `tabLabel`, `showInTabs`. Fails if the screen already exists — don't overwrite.
3. Regenerate the merged UI document (embedded via `app.rc`):

   ```powershell
   pwsh tools/Merge-UiConfig.ps1
   ```
4. Build to confirm: `build.cmd Debug`.
5. If the screen needs a handler/adapter action, wire it through `src/application/adapters` — components must never call `src/logic` directly.
