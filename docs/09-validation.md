# Langmake 0.1 validation record

This is the preserved 0.1 record. See [0.2 validation](11-assets-validation.md) for the text-asset release.

September 18, 2026. The Windows alpha is built, tested and packaged. This records the implemented milestone; the broader plan remains in documents 01–06.

## Ready to run

Launch `run-2d.cmd` or `run-3d.cmd` from the workspace. The distributable directory is `dist/Langmake-0.1`; its launchers run the packaged executable directly. The ZIP is `dist/Langmake-0.1-windows-x64.zip`. Keep the executable and `shaders/` together.

The package contains 43 manifest-listed files plus `package-manifest.json`, including both games, their winning input recordings, the runtime, shaders, an API guide, dependency identities and licenses. It imports only Windows-provided DLLs. MinGW runtime libraries are statically linked.

Executable SHA-256:

```text
2e9a6d4fe9c5f077c84fed475b9d56e62d05757c74fa7e99310ee0cf91e93884
```

## Executed checks

| Check | Observed result |
| --- | --- |
| `scripts/build.ps1` | C++20 Release build passed using GCC 15.1, CMake and Ninja on Windows x64 |
| Native CTest suites | Project and World tests passed; includes validation, revision conflicts, recovery, actual Box2D/Jolt physics, Lua bindings and execution limits |
| CLI integration | All six groups passed, including structured failures, schema discovery, physics/input, source mutations, restore, starter creation and PNG decoding |
| 2D game completion | Ordinary movement/jump input collected five sparks and won in 354 fixed steps; repeated state matched |
| 3D game completion | Ordinary movement/jump input collected five lanterns and won in 876 fixed steps; repeated state matched |
| Authored-file integrity | Playthrough runner verified manifest, script and input recording hashes stayed unchanged |
| GPU rendering | Both 1280×800 captures were visually inspected; no null renderer accepted |
| Live authoring | Sixteen checks passed: visibility/freshness, pause/step/resume, player movement, correlated capture, concurrent capture requests, capture ordering, exclusive preview ownership, live patches, malformed JSON recovery, malformed Luau recovery and observable runtime failures |
| Bounded script failure | A capture whose update script failed returned a structured error and nonzero exit, even though an image could be written |
| Distribution | Package manifest hashes matched; both packaged starters created, validated, simulated and captured with an empty `PATH` from a separate directory |

Commands to reproduce:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
powershell -ExecutionPolicy Bypass -File scripts/test.ps1 -Graphics
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
python scripts/package_smoke.py
```

`-Graphics` opens temporary preview windows. The tests use their own workspace-contained project copies. Package smoke also creates a ZIP from exactly the package manifest, excluding generated live state and unrelated output-folder files.

Reports from this run:

- Build: `.cache/release-build.log`
- Combined tests: `.cache/release-tests.log`
- CLI integration: `.cache/integration-tests/20260918-001501-a201d09fc7/report.json`
- Input replays: `.cache/playthrough-tests/20260918-001504-9eb8660f84/report.json`
- Live checks: `.cache/live-tests/68d28918562346b9b9d632842f3a4e99/report.json`
- Package smoke: `.cache/package-smoke/ab49166dabe1462f97adbe5ee7ff7da4/report.json`
- Visuals: `evidence/skyway.png` and `evidence/lantern-court.png`, with matching JSON sidecars

## What the evidence does not establish

This is workstation validation, not a clean-machine install test, hardware certification, performance benchmark or human usability study. Empty-`PATH` execution checks toolchain independence on this machine; it does not replace a second Windows device test. Physical gamepad input and audio perception have not been tested. A project-path symlink-escape test was skipped because this Windows environment denied symlink creation; traversal and other path/transaction tests passed.

The renderer currently draws colored primitive boxes and spheres. Physics is translation-only in this alpha. Imported assets, textures/PBR, skeletal animation, full game UI, navigation, production networking and other-platform exports are future milestones. Linux, macOS, mobile, VR and consoles are not supported releases. The early bgfx/OpenXR interoperability gate still needs actual headset/device evidence.

Source edits reload a fresh world and reset progress. Checkpoints restore authored manifest/script files, not gameplay saves. Multi-file restore is not crash-atomic. The Luau limits improve development error recovery; they are not a security boundary for hostile projects. These limits are part of the current contract, not hidden completion claims.

## Next implementation milestone

Before extensive renderer expansion, test the selected graphics/XR boundary when hardware is available. The next content milestone is a measured asset pipeline: glTF mesh/material import, textured 2D sprites, stable asset IDs, import diagnostics and repeatable capture tests. Follow with ozz animation, camera/collider visualization and a richer third-person reference game. Preserve the same file/CLI authoring contract while adding systems; no editor-only operation should become required.
