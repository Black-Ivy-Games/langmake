# Android game creation: instructions for AI agents

For sampled music or recorded sound effects, also read [the music and sound guide](30-music-and-sound-guide.md). Bake on the authoring host, include only native recipes/recordings in the APK, inspect decoded memory, and use a player declaring audio feature revision 1. Historical player binaries may support synthesis only. Consult [audio qualification](31-audio-qualification.md) for actual new-feature coverage.

**Read this first whenever the human asks for an Android game or an Android version of an existing game.** This is the ordered workflow for Langmake 0.5.0. It applies to external directory/CLI agents and the optional Studio worker. It does not require a particular model or AI service.

Begin with `langmake context --project GAME` and the [per-game workflow](26-game-workflow.md). Configure this game's explicit target independently; do not copy device/Mac/SSH settings from another game. Portable application icons/versions, named presentation profiles, custom held-action RML buttons, scenario/receipt tools and generated handoffs are now supported. The direct Android commands below remain available for diagnosis. Native platform qualification and human feedback remain separate from host checks.

The human directs the game and is its primary gameplay playtester. Complete the authorized implementation and technical checks, then provide a playable phone build with controls and focused feedback prompts. Do not replace that handoff with an autonomous computer-use playthrough. Follow decisions and permissions already given; do not ask again for routine implementation choices.

## 1. Read the documentation in this order

Paths in the portable column are relative to the folder containing `langmake.exe`. For Studio, that folder is its bundled `runtime/`. In Studio's `documentation` tool, list available documents first, then read the matching names below.

| Order | Purpose | Source checkout | Portable runtime |
| --- | --- | --- | --- |
| 1 | This workflow, target setup, verification and handoff | [21-android-agent-guide.md](21-android-agent-guide.md) | `ANDROID-AGENT-GUIDE.md` |
| 2 | Human workflow, native commands and timeline grammar | [08-agent-api.md](08-agent-api.md) | `AGENT-API.md` |
| 3 | Project graph, gameplay, input/UI, declared state and saves | [13-runtime-guide.md](13-runtime-guide.md) | `RUNTIME-GUIDE.md` |
| 4 | Android touch configuration and platform behavior | [18-android.md](18-android.md) | `ANDROID.md` |
| 5 | Exact setup, build, signing and device commands | [Android tool reference](../scripts/android/README.md) | `scripts/android/README.md` |
| 6 | Tested phone/emulator coverage and supported limits | [20-android-qualification.md](20-android-qualification.md) | `ANDROID-QUALIFICATION.md` |
| As needed | Text-authored sprites, meshes, scores and effects | [10-text-assets.md](10-text-assets.md) | `TEXT-ASSETS.md` |
| As needed | A human brief to adapt, not an implemented game | [Android prompt example](../prompts/09-android-game.md) | `prompts/09-android-game.md` |
| Studio | Provider/workspace workflow and Targets panel | [17-studio.md](17-studio.md) | `STUDIO.md` |

Also read the workspace's `AGENTS.md`. Query the actual executable using `capabilities`, `schema`, `script-api`, and `asset schema --type sprite|mesh|model|music` for the asset types used. Roadmap documents are not evidence that a feature exists.

## 2. Establish paths and the target

Record these in the game's development notes: human brief and current iteration; engine/project paths; application ID; ABI; orientation; target device serial if supplied; testing status and pending human feedback. Keep credentials and private signing material out of the game.

| Decision | Rule |
| --- | --- |
| Engine root | Source checkout: contains `scripts/android/android.py` and `build/langmake.exe`. Portable runtime: contains the same helper and `langmake.exe`. Studio's application folder is not its runtime folder. |
| Project | Use the human's existing project, or a new authorized directory for `new`. Keep downloads/caches/evidence within the authorized workspace. Do not edit bundled examples to make a new game. |
| ABI | `arm64-v8a` for the supported ARM64 phone target; `x86_64` for the bundled emulator. Confirm that another device supports the chosen ABI. There is no 32-bit ARM export. |
| Orientation | `landscape` by default for a landscape game; `portrait` for a portrait game; `sensor` only when both layouts will be authored and checked. Record an assumption when the brief leaves this routine choice open. |
| Application ID | A unique reverse-domain ID such as `com.example.repairdrone`. Preserve it and its signing key for updates. Never reuse another installed game's ID. |
| Project identity | Preserve the manifest's `project_id`; legacy projects use their title. Changing it changes the Android save namespace even if the application ID stays the same. |
| Technical sessions | Use a distinct application ID such as `com.example.repairdrone.verify` for automated device scenarios when a human build must stay independent. Same-project Windows captures are isolated; relaunching the same Android app restarts it. |

External agents can use the following PowerShell setup. Replace the engine path and game name with actual values before executing; do not create literal placeholder directories:

```powershell
Set-Location "PATH_TO_SELECTED_ENGINE_ROOT" # Replace with the actual engine/runtime directory.
$lmRoot = (Get-Location).Path
$lmEngine = if (Test-Path -LiteralPath ".\langmake.exe") {
    Join-Path $lmRoot "langmake.exe"
} else { Join-Path $lmRoot "build/langmake.exe" }
$lmAndroid = Join-Path $lmRoot "scripts/android/android.py"
$lmProject = Join-Path $lmRoot "games/repair-drone"
$lmAppId = "com.example.repairdrone"
$lmAbi = "arm64-v8a"
$lmOrientation = "landscape"
$lmRun = [DateTime]::UtcNow.Ticks.ToString()
$lmEvidence = Join-Path $lmRoot "evidence/repair-drone/$lmRun"
New-Item -ItemType Directory -Path $lmEvidence -Force | Out-Null
& $lmEngine capabilities
python $lmAndroid doctor
```

Check every command's exit code and JSON diagnostics. An old output file or `device_qualified:false` in a successful build report is not a test result: the latter deliberately means the build command itself did not test a device.

If `doctor` succeeds, reuse that setup. If tools are missing, follow the tool reference's `bootstrap` procedure. SDK terms require explicit human acceptance; reuse acceptance already established for this setup instead of asking again. Do not install global tools. Portable packages include debug/release players in `android-player/<abi>/<configuration>/` and need Python plus local SDK/JDK/Gradle, but no NDK or engine compiler. Source builds also need the pinned NDK and existing native build tools. A missing native player is a package problem, not something fixed by downloading the SDK.

Studio workers use the exposed `engine` and project-file tools for authoring and host checks. They do **not** have a general shell or an Android build/install tool. Prepare the game and exact target settings, then direct the human to **Targets**, or hand the commands to an authorized external CLI agent. Do not claim a phone deployment from Studio's host `capture` result.

## 3. Create or adapt the game

For a new game, run `new` into a nonexistent directory. Use `shared-2d` for a small v2 2D project with UI/modules/saves, `adventure` for the corresponding 3D foundations, or `strategy` when group navigation is needed. The original `2d`, `3d`, `rockets-2d` and `rockets-3d` starters are also supported; their simpler v1 scripts do not automatically gain complete progression saves.

```powershell
& $lmEngine new --template shared-2d --output $lmProject
& $lmEngine inspect --project $lmProject
& $lmEngine schema
& $lmEngine script-api
```

Skip `new` for an existing game. Preserve authored work, stable IDs and saves. Author the human's requested mechanics and original assets in JSON/Luau, using the runtime and asset guides. Register every scene, module, UI document, stylesheet, image and imported asset dependency: cooking includes the registered graph, not arbitrary files beside it. `new` does not copy unregistered verification timelines; write the new game's scenarios explicitly.

**Make phone controls intentional.** Bind keyboard and touch to the same actions. Explicitly list the needed touch buttons so inherited Jump/Fire or unused registered actions do not clutter the screen. This fragment is merged into `project.json`; preserve other actions that the game uses:

```json
{"input":{
  "actions":{
    "interact":{"keys":["E"],"gamepad":"north"},
    "save":{"keys":["F6"]},
    "load":{"keys":["F9"]}
  },
  "touch":{
    "enabled":true,
    "stick":{"enabled":true,"x_action":"move_x","y_action":"move_z"},
    "buttons":[
      {"action":"interact","label":"Use"},
      {"action":"save","label":"Save"},
      {"action":"load","label":"Load"}
    ]
  }
}}
```

The fragment configures input, not the mechanics. Implement each displayed action. `engine.input_pressed("interact")` is an edge; `engine.input("interact")` is numeric and must be compared with zero. Lua treats numeric zero as true. Legacy movement axes are numeric; legacy `jump`/`fire` are booleans. Stick-up gives a negative vertical axis. The game chooses how this maps to 3D X/Z movement or 2D X/Y velocity; do not feed a 2D vertical control into world Z accidentally.

Touch supports up to 64 explicit unique buttons, six per page, and distinct stick axes. Buttons name registered actions or legacy `jump`/`fire`. To use only authored touch UI, set `input.touch.enabled:false`; the game's RML UI remains touch-enabled. Use `dp` for phone font/button sizes, percentage anchors, and `body { width:100%; height:100%; }`. RML/RCSS is not browser HTML/CSS/JavaScript. Register files and stable leaf IDs for text bindings; inspect `ui_renderer.safe_area`, insets and physical element bounds on the device. Verify the chosen portrait/landscape layouts instead of assuming a desktop screenshot proves readability.

**Make progress persistent.** Store save-critical values with `engine.state_get/state_set`, not only Luau locals or module tables. Use `engine.save_game("progress")` and `engine.load_game("progress")` for implemented UI/actions; an empty-slot load needs an understandable game response. Refresh the HUD in `on_load`. Award initial items in `start`, not again during restore. Preserve `project_id`; assess an explicit `migrate` hook for any source-revision/save-version change affecting existing saves. Read the runtime guide before editing that hook; a blind passthrough can restore incompatible entities.

## 4. Verify on the host before exporting

Create `verification-input.json` in the game. This is a grammar example for a 120-step smoke scenario, not a winning scenario or creative acceptance:

```json
{"events":[
  {"from":0,"to":30,"move_x":1},
  {"from":30,"to":31,"actions":{"interact":1}},
  {"from":60,"to":90,"move_x":-1}
]}
```

Ranges are inclusive/exclusive; the last overlapping entry replaces the whole input for that tick. Combine simultaneous inputs into the same entry. Tick 0 is initialized state; tick N follows N completed steps. Adapt movement, action timing and assertions to the actual game.

```powershell
& $lmEngine validate --project $lmProject
& $lmEngine check --project $lmProject
& $lmEngine simulate --project $lmProject --steps 120 --input "$lmProject/verification-input.json" --output "$lmEvidence/simulation.json" --save-output "$lmEvidence/progress.save.json"
& $lmEngine simulate --project $lmProject --steps 0 --load-save "$lmEvidence/progress.save.json" --output "$lmEvidence/restored.json"
& $lmEngine capture --project $lmProject --steps 120 --input "$lmProject/verification-input.json" --ticks 0,30,120 --output-dir "$lmEvidence/windows-captures" --no-overlay
```

Stop dependent steps on a failed command, fix the diagnostic and rerun the affected check. Assert concrete progression, input and restore behavior in the returned state; `ok:true` only establishes command success. Review captures and sidecars. Keep save bytes, state results and logs at separate filenames. Headless script save requests do not write files; `--save-output/--load-save` are the headless mechanism. Render music with `asset render` or `simulate --audio-output` and check levels/clipping; perceived volume still needs the human.

## 5. Build an installable APK and retain its report

Default debug APKs are signed with a workspace development key and launch in ordinary player mode. They are suitable for the first human iteration. Build only after the host checks pass:

```powershell
$lmBuildText = python $lmAndroid build --project $lmProject --application-id $lmAppId --label "Repair Drone" --abi $lmAbi --orientation $lmOrientation
if ($LASTEXITCODE -ne 0) { throw ($lmBuildText -join "`n") }
$lmBuild = ($lmBuildText -join "`n") | ConvertFrom-Json
if (-not $lmBuild.ok -or -not $lmBuild.signed) { throw "An installable APK was not produced." }
[System.IO.File]::WriteAllText((Join-Path $lmEvidence "build-report.json"), ($lmBuildText -join "`n"), [System.Text.UTF8Encoding]::new($false))
```

Use returned `.apk`, `.application_id` and `.storage_id`; do not guess them. The report beside the APK also records orientation, ABI, source/packaged revisions, signing status and hashes. New builds create unique APK paths by default; an explicit output must not already exist. Compare Android's running `source_revision` with the build report's `packaged_revision`. Compare APK contents with `packaged_native_validation`, not the unstripped source-library hash.

For automated tests that must not affect a human app, export a second debug APK using the same project and a distinct verification application ID. Use that APK's report throughout its test; do not mix application/storage IDs across reports.

For production artifacts, follow the tool reference's `--configuration release --keystore ... --key-alias ... --store-password-env ... --bundle` recipe with the developer's key. Passwords belong in the named environment variables, not command arguments. Unsigned release APKs cannot be installed. An AAB is a distribution bundle, not an APK passed to `install`. Store enrollment/publishing is separate authorization and is not performed by these tools.

## 6. Install and perform bounded phone verification

```powershell
python $lmAndroid devices
$lmSerial = "DEVICE_SERIAL_FROM_DEVICES" # Use the authorized device in state "device".
python $lmAndroid install --serial $lmSerial --application-id $lmBuild.application_id --apk $lmBuild.apk
python $lmAndroid launch --serial $lmSerial --application-id $lmBuild.application_id
python $lmAndroid screenshot --serial $lmSerial --output "$lmEvidence/phone.png"
python $lmAndroid logs --serial $lmSerial --application-id $lmBuild.application_id --output "$lmEvidence/phone.log"
```

Choose an explicit serial. If multiple devices are present and the brief does not identify one, ask which to use; meanwhile complete host work. Empty/offline/unauthorized results are device prerequisites: ask the human to unlock the phone, enable USB debugging, use a data cable and accept its authorization prompt as applicable. Do not silently choose a different device, install drivers or clear app data. Use the documented emulator only when appropriate and label its results separately. A missing phone does not prevent creating an APK, but its phone checks remain pending.

`install` checks the app ID then updates with `adb install -r`. Preserve the original signing key for later updates. `launch` restarts the selected app; `stop` force-stops it. Do not use any of these during the human's play session. A force-stop is not the same as backgrounding: let a save complete before deliberately killing a process.

Technical acceptance for the authored iteration:

1. Confirm the intended content is visible and no native error/OS overlay blocks it. A compositor screenshot includes OS overlays; an engine capture does not.
2. Check required buttons, stick press/release and authored UI routing. Prefer native scenarios for mechanics; use focused physical/UI input checks only where necessary. Do not claim touch was tested by an input timeline alone.
3. Save meaningful declared progress and await its receipt. A manual slot `progress` writes `saves/progress.json`; a normal relaunch restores the separate `_android_resume.json` automatic slot. To verify the manual save, pull its bytes, stop/relaunch the verification app, reacquire its new live session, then send `load` for `progress` with `include_state:true` and assert the restored values. Test background/automatic recovery separately after the background save completes. Confirm an APK update preserves that save namespace when applicable; do not uninstall or clear data as a shortcut. Check restore hooks do not duplicate rewards.
4. Check orientation(s), safe-area layout and readable controls. Record performance/memory for the actual game if load warrants it. Check audio playback; have the human assess perceived loudness and comfort.
5. Retain APK/report, logs, images, state assertions and device model/API/ABI. Keep emulator, physical-device, host-only and human feedback results separate. Broaden testing only when changes or failures justify it.

## 7. Native Android inspection and captures

These operations use a **debug APK** and Android's app-private `run-as` access. Release apps reject that access. The CLI is a transport: upload success is not command execution success. Full transport examples and request/receipt rules are in the [tool reference](../scripts/android/README.md#native-command-and-capture-recipes).

- Interactive inspection: `inspect --serial ... --application-id ... --storage-id ...` returns `{ok,serial,live:{...}}`. Verify `.live.running`, `.live.session`, `.live.source_revision`, `.live.diagnostic` and `.live.published_at_ms`. Compute observation age; Android does not add the desktop CLI's freshness checks or entity filters. Filter the returned data locally.
- Technical pause/step/reset/resume requires `launch ... --debug-args --development`. Ordinary player mode permits snapshot/capture/save/load. Every control file includes a fresh numeric-string ID and the current live session. Poll its receipt with a timeout; check the receipt's `ok`. If a mutation's receipt is missing, inspect the outcome and report uncertainty instead of issuing the mutation again automatically.
- Exact-tick captures use `launch ... --debug-args --steps N --input PATH --ticks TICKS --output-dir DIR`. Supply **all four** timing options. Paths are app-private and storage-relative; push the timeline first. N is 0..36000, with at most **128** unique increasing capture ticks inside the range. Do not pass desktop `--no-overlay`, window dimensions or `--frames` in this mode.
- A timeline run disables automatic recovery restore/write and uses an isolated native session. It still restarts the selected Android application. Use a separate verification application ID to preserve a human app/session.
- Poll `launch-result.json` and fetch `tick-N.png` **and** `tick-N.png.json`. `running:true` is an in-progress marker. Successful completion has `ok:true`, `state` and `captures`; `running` may be absent. That file is reused across launches: correlate a new session and a unique capture directory before accepting a result. Ordinary `inspect` reads the interactive live file, not the isolated timeline session.

## 8. Human handoff and next iteration

Install/launch the human APK when authorized, or provide its exact artifact and install command. If you used a debug session, restart in ordinary player mode **before** inviting the human to play. Provide:

- Game name, goal, changes in this iteration, exact phone application ID/APK path and Windows launch command if applicable.
- Complete touch/keyboard controls, how to save/load/restart, and supported orientation.
- Passed technical checks with evidence paths; separate pending device checks and known issues.
- Two or three focused prompts for human feedback on controls, readability and difficulty/audio.

Example Windows handoff: `& $lmEngine play --project $lmProject`. Example Android handoff: `python $lmAndroid launch --serial $lmSerial --application-id $lmBuild.application_id`, with all variables replaced by actual values in the human-facing handoff. During human play, stop automated commands/source edits that would disrupt it. Human feedback directs creative changes; passing these technical checks does not accept the game's design.

## Troubleshooting and maintenance

| Symptom | Next step |
| --- | --- |
| Missing `libmain.so` or player metadata | Use a complete runtime package or finish the authorized source build. Match ABI and debug/release variant; SDK installation alone cannot supply a player. |
| Build fails or outputs unsigned APK | Read JSON/stderr. Resolve validation/toolchain/signing errors; use debug for an ordinary test or provide the developer's release key. Never install an arbitrary older APK as if it were the new result. |
| APK signature conflicts with installed app | Use the original signing key or a distinct authorized test ID. Do not uninstall the human's game to discard its saves. |
| No touch action, or it fires every update | Check explicit button/action names and numeric-zero handling; use `input_pressed`. Check focused UI consumption and physical bounds. |
| Tiny/cropped HUD | Use dp, full-size body and anchors; inspect safe-area/density and actual device pixels. |
| Save vanished after an iteration | Check unchanged application ID, signing key, `project_id`/legacy title, and migration requirements. Inspect quarantined recovery diagnostics; preserve rejected bytes. |
| Stale inspection or missing receipt | Check live age/session, current APK revision, app PID/logs and the exact storage ID. Acknowledge uncertain mutation outcomes; do not replay them blindly. |
| Old/absent timeline result | Check unique output directory, new session, pushed input and final sidecars. Allow startup time; use a bounded wait and logs. Ordinary interactive live state is not a timeline completion signal. |
| Music too quiet or clipped | Inspect score/instrument/bus gains and offline PCM metrics, then ask for human listening feedback. Do not change device-wide volume without direction. |

Reviewed September 19, 2026 against 0.4.1 commands, source and qualification. Engine changes affecting Android authoring must update this guide, the tool reference, discovery links and affected prompt examples together. Regenerate runtime-only and Studio documentation from canonical sources and verify packaged links/manifests. A documentation review does not rerun or expand historical device qualification.

September 21, 2026 source update: mobile pointer coordinates now survive finger release until the next consumed fixed tick, matching queued semantic UI clicks. Authored tap-to-place games should verify rapid down/up taps as well as held gestures on the target device. This does not qualify untested devices or historical packaged players.
