# Studio usability: studio2, September 22, 2026

The human authorized a plan and implementation after local LM Studio setup exposed confusing connection failures, inherited starter names, raw tool transcripts with no final answer, and an older Studio installation without the editor button.

## Implemented plan

1. Keep HTTP restricted to loopback; add actionable connection errors, a local default, and explicit connection/model-list testing. Fetching models sends no project content or inference request. Keys remain in Studio's existing encrypted store; testing an edited endpoint never forwards the old destination's stored key.
2. Collapse tool activity, separate intermediate commentary from the final answer, flush stream data at EOF, validate a complete tool batch before executing it, and reject interrupted/limited responses. An empty terminal response gets one tool-free final-answer retry; it never repeats tools automatically. All providers must produce a visible final answer before Studio calls the run complete.
3. Use the entered project name in Studio without rewriting the template's authored game title. Preserve existing imported names, with the existing project rename action available.
4. Expose the existing text-compatible **UI editor** beside Play with availability hints, and identify the Studio build in settings. Package it with the existing UI observation revision 1 runtime. Preserve older packages and private data.

The editor adjusts registered RML interfaces with isolated native captures, property edits, drag/resize, undo/redo, revision-checked Apply, and screenshot/notes export. It is not a scene editor and needs no model. See [the guide](34-ui-editor-guide.md).

## Open the update

Close the old Studio window. From the repository run:

```powershell
powershell -ExecutionPolicy Bypass -File .\studio\open-packaged.ps1
```

The launcher selects `dist/studio2/Langmake-Studio-0.5.0`, uses its runtime, and reuses `dist/Langmake-Studio-0.5.0/.data` when present. Nothing is copied from private data into the package. Supply `-DataDirectory` to select another workspace data directory. Existing conversations, profiles and checkpoints remain in that selected directory. Do not run older releases concurrently against it. Opening the new executable directly gives it separate data.

In **Providers & runtime**, choose a local OpenAI-compatible profile, enter `http://127.0.0.1:1234/v1` with the actual configured port, and click **Test connection & fetch models**. Select the exact model ID, then save. For the user's loaded model the screenshot showed `qwen/qwen3.8-27b`; discovery is the authoritative source for the currently running server. Old errors remain in conversation history.

For the existing test folder whose sidebar says Glow Garden, use the project's **…** button to rename the Studio entry. New projects now use the name entered in New. This does not silently change in-game text or application identity.

## Qualification boundaries

Provider and broker fixtures cover model discovery, credential destination isolation, tool/final separation, Unicode/EOF streams, empty completions, interruption, malformed batches, output limits, HTTP failures and Anthropic compatibility. Real Electron tests exercise a deliberately fake loopback provider and the existing native runtime in isolated projects. Such tests establish integration behavior, not Qwen model quality or human acceptance.

No runtime source or device player is changed for this update. The runtime binary is reused from the UI revision 1 build; its historical platform/audio qualification remains in the existing records. No new provider account, remote machine, mobile device or real-model inference is required by this iteration. Human feedback on the updated editor and local Qwen behavior is still needed.

## Verification record

Source verification on September 22, 2026 passed 93 Node tests, the real Electron chat/model-list workflow, project-selection isolation, the usability regression (new project naming, empty-answer recovery/error and persistence), and the native UI editor workflow. Tests use isolated fixture data under `studio/.cache/`; they do not change the user's game or active Studio data.

The distributable runtime is `dist/Langmake-0.5.0-studio2/`; Studio is `dist/studio2/Langmake-Studio-0.5.0/`. The package manifest identifies `studio2-2026-09-22`. Package acceptance results and exact fixture evidence paths belong in the generated `dist/studio2-qualification.json`; consult that receipt for the packaged reruns rather than assuming source tests prove a packaged installation.

### Launcher correction, September 22, 2026

The initial companion launcher incorrectly used `Start-Process -WindowStyle Hidden` for the interactive application. A human launch produced a running Electron process with a hidden main window. The previous mocked launcher check did not establish visibility; the Electron tests launched through Playwright instead of this script.

`studio/open-packaged.ps1` now uses `Normal`, observes the started process and waits up to 15 seconds for a visible main window before reporting success. Early exit and missing windows report errors. `studio/test/launcher.ps1` passed a real packaged launch with isolated data, verified its visible main window and environment restoration, then closed only its fixture process. The already-running human window was revealed without restarting it or editing private data. The application/runtime package bytes are unchanged; this corrects the repository companion launcher. The UI/model prompt examples remain applicable.
