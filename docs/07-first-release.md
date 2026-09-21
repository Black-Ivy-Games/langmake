# First runnable release: Windows alpha

Implementation scope · September 2026 · Langmake 0.1

Historical milestone. For the current 0.2 asset features and commands, see [text assets](10-text-assets.md), [agent API](08-agent-api.md), and [current validation](11-assets-validation.md).

This milestone turns the selected custom-runtime architecture into a small runnable engine that a creator can inspect and change through project files and commands. The Windows alpha and two reference projects have build, simulation, playthrough and preview evidence described below. This milestone does not establish support for the entire platform and feature roadmap.

## Release scope

The alpha runs on Windows. Its native engine uses C++20, and gameplay is authored in Luau. Scenes and project settings are JSON. The two examples exercise separate 2D and 3D worlds with actual Box2D and Jolt physics. Their primitive geometry keeps the first release self-contained and makes changes easy to inspect.

The intended creator loop is:

1. Launch either reference project and play it.
2. Inspect the project and discover the engine schema from the command line.
3. Edit Luau or JSON directly, or apply a JSON patch through the engine.
4. Validate the changed project and simulate a repeatable input sequence.
5. Preview the result and capture an image as evidence.
6. Checkpoint and restore project files when exploring alternatives.

The external agent owns natural-language interaction. Neither the authoring command line nor the exported player requires an LLM account, a hosted inference endpoint, or a bundled model.

## Build and launch

Use the installed CMake, Ninja, Git and MinGW g++ tools. The scripts use these tools without installing software or changing the user's PATH. Dependency sources, build output, temporary files and caches are project-local.

From this repository:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
.\run-2d.cmd
.\run-3d.cmd
```

The build output is `build/langmake.exe`; Ninja uses one Release build directory. `scripts/build.ps1 -Jobs 4` can reduce build concurrency. The first build requires network access for dependencies; subsequent builds reuse downloaded sources.

Controls: **A/D or the arrow keys** move; **W/S** move in 3D; **Space** jumps; **P** pauses; **R** resets; **F5** reloads; **right mouse button** orbits the camera; **mouse wheel** zooms; **F12** captures a PNG; **Escape** closes the preview.

## Command contract

These commands are the supported entry points for automation. Command output and validation failures should be readable by a plain script as well as an LLM.

```powershell
.\build\langmake.exe capabilities
.\build\langmake.exe schema
.\build\langmake.exe validate --project examples/2d
.\build\langmake.exe inspect --project examples/3d
.\build\langmake.exe inspect --project examples/3d --live
.\build\langmake.exe new --dimension 2d --output my-game
.\build\langmake.exe simulate --project examples/2d --steps 120
.\build\langmake.exe simulate --project examples/2d --steps 120 --input path/to/input.json
.\build\langmake.exe patch --project examples/2d --file path/to/patch.json
.\build\langmake.exe checkpoint --project examples/2d --name before-jump-change
.\build\langmake.exe restore --project examples/2d --name before-jump-change
.\build\langmake.exe preview --project examples/3d
.\build\langmake.exe capture --project examples/3d --output .cache/captures/courtyard.png --frames 2
```

The `path/to/...` filenames are placeholders for creator-supplied files; they are not bundled test fixtures. Use `schema` and the example project files to discover the accepted data shape. Preview also accepts `--frames N`, `--capture PATH` and `--no-audio` for bounded observation runs.

See [Agent API](08-agent-api.md) for the project format, Luau bindings, JSON patches, input timelines and live-control details. Each example includes `tests/win.json`, an actual input-only completion recording.

`new` copies a starter into a new directory. To observe or control an existing preview, use a second terminal. `inspect --live` reads its runtime state and freshness information. `control` supports `pause`, `resume`, `reset`, `snapshot`, `step` and `capture`:

```powershell
.\build\langmake.exe control --project examples/3d --action pause
.\build\langmake.exe control --project examples/3d --action step --steps 60
.\build\langmake.exe control --project examples/3d --action resume
```

A step operation can also accept `--input path/to/input.json`. Live control requires the preview to be running; headless `simulate` runs independently. Capture files are PNG images.

Frame captures are rendered evidence. Headless simulation checks gameplay state and physics without demonstrating that the renderer is correct. Both forms of validation are necessary.

## Release acceptance

Run the verification entry point after building:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -ExecutionPolicy Bypass -File scripts/test.ps1 -Graphics
```

The default runs CTest, `scripts/integration_tests.py` and `scripts/playthrough_tests.py` against the actual executable. The playthrough test drives ordinary movement/jump input, checks all five collectibles and the win state, repeats the replay, and verifies that authored scene/script/input files remain unchanged. `-Graphics` adds GPU-backed capture checks and `scripts/live_tests.py`, opening real preview windows. A passing release must demonstrate:

- Both projects validate, open in a real window and respond to player input.
- Luau controls gameplay and physics updates through documented engine bindings.
- Headless simulation exposes observable state for reproducible checks.
- Valid edits can be applied; malformed project data and invalid commands return actionable errors.
- Checkpoint/restore recovers authored files after an edit.
- Both dimensions can produce image captures that have been visually inspected.
- A distributable folder includes the player, required runtime DLLs, both projects, license notices and launch instructions.

Only checks actually run and their observed results belong in a release report. Successful compilation alone is not evidence that either game is playable. A Windows result does not qualify another platform or graphics backend.

### Observed validation — September 18, 2026

- The Windows native build and CTest/headless integration checks passed.
- Both ordinary-input recordings collected all five items and won: 2D at 354 simulation steps, 3D at 876 steps. Each replay reproduced its final state on this build.
- Both 1280×800 PNG captures were visually inspected: [Skyway](../evidence/skyway.png) and [Lantern Court](../evidence/lantern-court.png).
- Sixteen live-preview checks passed, including pause/step/resume, reload/error recovery, concurrent captures, capture/state ordering and script failures during bounded graphics runs.
- The folder package was generated and its `capabilities` smoke check passed. Import inspection found only Windows-provided DLL dependencies.
- The packaged executable created, validated, simulated and rendered both dimensions with an empty `PATH`, from a separate workspace directory. Its file hashes matched the manifest. See [release evidence](09-validation.md).

This evidence comes from the development workstation. It does not establish clean-machine portability, physical gamepad behavior, XR/device support or subjective game feel. Those require separate validation; the roadmap also retains its broader reliability and performance targets.

## Local distribution bundle

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
python scripts/package_smoke.py
```

The package is created in `dist/Langmake-0.1`. It includes `run-2d.cmd` and `run-3d.cmd` next to `langmake.exe`, its required `shaders/` directory, example projects, `licenses/`, third-party notices and a dependency revision lock. A creator can run examples without the compiler, a model, or a separate authoring service. MinGW runtime libraries are linked statically. Packaging invokes the existing `objdump` command to inspect imports and resolves any additional non-system DLLs only within `build/`; a missing import stops packaging with an actionable error. It never searches or copies an external compiler directory.

Packaging copies files into this dedicated output folder without deleting an existing directory tree. The generated package manifest lists the current packaged files; unrelated files already present in the output folder are not part of that manifest.

This is a local development bundle, not a signed installer or a store submission. A clean Windows-machine launch remains a separate portability check from launching on the build workstation.

## Explicit limits and next work

Linux, macOS, mobile, VR and consoles remain roadmap targets. This release does not ship those targets or certify their underlying integrations. In particular, desktop bgfx rendering does not clear the required OpenXR gate. Keep the early Windows/Android XR experiment from the selected-stack plan ahead of extensive investment in the renderer.

The alpha is not a complete replacement for a mature game engine. Rich imported assets, skeletal animation, navigation, advanced materials and lighting, full game UI/text, production networking, extensive save migration and large-world content pipelines need their own tested milestones. Check the executable's `capabilities` output for what this particular build actually implements.

After this release, prioritize measured edit/preview latency, reliable diagnostics and project recovery, then broaden the reference games and asset pipeline. Add platforms only after their native builds, device tests and packaging prerequisites have been demonstrated.
