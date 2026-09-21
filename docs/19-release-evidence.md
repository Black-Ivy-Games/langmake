# Langmake 0.4 verification record

This is the preserved 0.4.0 verification record. Subsequent Android completion work, SDK acceptance and target build/device results are recorded in [0.4.1 Android qualification](20-android-qualification.md). Pending statements below describe the original 0.4.0 handoff.

September 18, 2026. Windows runtime and optional Studio passed distribution checks and are ready for human testing. Android implementation is awaiting SDK installation and actual target qualification. This record distinguishes code, executed checks and prerequisites. It does not replace the preserved [0.3 evidence](14-release-evidence.md).

## Current evidence

| Area | Executed evidence | Status |
| --- | --- | --- |
| Windows host build | Fresh MinGW/CMake build, `.cache/build-0.4/langmake.exe`, version 0.4.0 | Passed |
| Native runtime | Eleven CTest targets, including touch routing and physical audio suspension/resumption; final run 7.62 seconds | Passed: `.cache/build-0.4/final-native-tests.log` |
| Core integration | Six checks including physics, replay, source transactions, bounded scripts and captures | Passed: `.cache/integration-tests/20260918-183046-8a97e1f09a/report.json` |
| Original scenarios | 2D 354-step and 3D 876-step input-only completions reproduced | Passed: `.cache/playthrough-tests/20260918-183049-65fb3744ad/report.json` |
| Player content | All seven templates: complete registered graph, private-file omission, unchanged simulation observations, existing-output preservation, invalid-input rejection | Passed: `.cache/cook-tests/00732c8796b3487a9d73f08118f556f5/report.json` |
| Text assets and graphics | Eleven asset checks, including rendered sprites/models and live reload | Passed: `.cache/integration-tests/20260918-183500-2ef9223a31/report.json` |
| Adventure/strategy/2D references | Thirty-eight checks including scenes/saves, 128 followers and capture/state parity | Passed: `.cache/release-0.4-evidence/reference-report.json` |
| Live preview | Sixteen checks including pause, exact steps, queued captures, reload/error recovery and project locks | Passed: `.cache/release-0.4-evidence/live-report.json` |
| Workflow stress | Nine checks: 652 entities, forced Windows sharing failures/recovery, no duplicate commands, bounded receipts, replay parity, player isolation, HUD and audio | Passed: `.cache/build-0.4/workflow-tests.log`; `.cache/workflow-tests/8f451edbb19d455ba421c4072ed3f206/` |
| GLES shader conversion | Native model vertex and fragment shaders compiled with pinned host shaderc, Android/300_es | Passed: `.cache/android-shader-check/` |
| Android host tools | Nineteen tests, pinned JDK/Gradle run locally, real AGP configuration, source-cook/APK staging, release-reuse guards, missing-player diagnostics and early current-engine discovery | Host checks passed; SDK/native/device tests pending |
| Studio core | Twenty-one checks covering containment, revision conflicts, cancellation, checkpoints, persistence, writer/play locks, project/chat management, provider handoff context, Android application IDs and split UTF-8 process output | Passed: `studio/.cache/core-final-tests.txt` |
| Project switching | Eleven real-Electron checks: identical file paths/revisions in different projects, stale asynchronous read/list/diff results, sidebar removal, no-chat clearing, guarded saves and chat-scoped activity | Passed in source and final package: `studio/.cache/ui-selection-1789779420197/evidence.json` |
| Studio graphical integration | Real Electron window, explicitly fake loopback tool-calling model, real Langmake validation, edits/diff, Play/Stop lock and human feedback | Passed: `studio/.cache/ui-1789778044290/evidence.json` and screenshot; this is not a live model quality test |
| Codex transport | Actual installed app-server initialize/account-read/thread-start with dynamic tools, no signed-in account | Protocol passed; authenticated generation untested |
| Runtime distribution | All seven starters create/validate/check/simulate/render with an empty PATH; ZIP physically extracts, every manifest hash verifies, extracted adventure initializes its font/UI and tick-zero capture | Passed: `.cache/release-0.4-evidence/package-report.json`; 191 packaged files |
| Studio distribution | Independent copied distribution, 286 verified file hashes, real Electron window and tool loop using its own bundled runtime, then both regressions rerun after final fixes | Passed: `studio/.cache/packaged-final-1789779417605-evidence.json`; final workflow screenshot/report `studio/.cache/ui-1789779423082/`; no source/build directory dependency |
| External-agent authoring | Separate assigned agent used only packaged docs/native CLI to create Rocket Relay: three collectibles, positive/idle/wrong-direction scenarios, four capture/state comparisons and unchanged starter hashes | Passed: `.cache/studio-android-authoring/evidence/report.json`; this was not a context-free model, live-provider or human playtest |

The initial asset graphics run passed ten checks, then its Python harness hit a transient `PermissionError` reading a live snapshot through an ordinary Windows file handle. The preview remained running. The harness now uses native `inspect --live --limit 0`, which provides replacement-compatible reads and explicit freshness. The complete affected suite subsequently passed all eleven checks; the failed initial report remains retained.

## Required remaining qualification

Follow the [implementation checklist](16-studio-android-plan.md). The host touch tests cover stick/buttons, short taps, UI priority, disabled controls, pagination and safe-area density. Actual phone/emulator touch behavior is a separate outstanding check. Runtime and Studio package checks passed.

No physical Android device was attached during the initial read-only enumeration. Google SDK/NDK/emulator installation is awaiting the human's response to the explicit SDK license question. Emulator results, when available, must not be described as physical-phone qualification. Provider protocol fixtures must not be described as successful authenticated cloud-model use.

Remaining Android checks include both native ABIs, Java compilation, APK/AAB packaging/signing/alignment, real 2D/3D pixels and UI, input, surface/rotation/background recovery, audio, saves across process death/update and memory/performance. **No Android APK is currently verified or supplied.** The exported player source and tooling must not be advertised as completed phone support.

## Content format

`cook` currently produces a validated source graph, not precompiled model or Luau bytecode. It compiles/checks the project before packaging; the player compiles native asset recipes on load. The manifest identifies that encoding explicitly. Android's installation manifest adds SHA-256 integrity hashes, distinct from the engine's FNV revision tokens. Startup time and memory therefore need target measurements.

## Preserved boundaries

The runtime remains independent of Studio and AI providers. Existing games and the previous portable release are preserved. Human gameplay acceptance remains pending actual human playtesting even when technical tests pass. No store submission, signing enrollment, iOS, XR, console or unrestricted device compatibility is claimed.

## Desktop handoff

`dist/Langmake-Studio-0.4/Langmake Studio.exe` opens the optional UI. Its adjacent `runtime/` is a normal standalone engine distribution. `dist/Langmake-0.4/langmake.exe` is the runtime-only edition. Configure a provider profile in Studio before sending an implementation request; no account or model is bundled. External agents can continue using either runtime directory and an authorized game folder directly.

Studio verifies source edits and native engine commands, then hands Play to the human. Provider adapters were tested through fixtures and the actual Codex protocol, but authenticated cloud-model generation remains to be exercised with the human's configured account. This is an unsigned Windows alpha, not a signed installer or broad clean-machine/device certification.
