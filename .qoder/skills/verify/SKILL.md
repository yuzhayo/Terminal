---
name: verify
description: Build and run the full test suite before marking work done. Use when the user asks to verify changes, confirm a fix, or before finishing any C++ task.
---

Run the build and test suite and report results honestly. Do not claim success if any step fails.

1. Build and test both configurations:

   ```powershell
   pwsh tools/Test.ps1 -Configuration All
   ```

   This builds Debug and Release, then runs `build/x64/<cfg>/TerminalTests.exe` for each.

2. To run a subset first (faster feedback), use a filter glob:

   ```powershell
   pwsh tools/Test.ps1 -Configuration Debug -Filter <glob>
   ```

3. Also verify `git diff --check` passes (LF-only line endings are a completion criterion).

4. If working on the structure migration (see Current.md), additionally confirm the diff contains structural/path changes only.

Report: which configurations built, test pass/fail counts per configuration, and any failures verbatim.
