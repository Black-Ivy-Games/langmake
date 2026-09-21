# Langmake 0.2: text asset release evidence

September 18, 2026. This Windows/D3D11 alpha implements native text-authored sprites, procedural solid meshes, synthesized music and sound effects. The runtime, examples and portable package are ready to try. This record supersedes the current-status claims of the preserved [0.1 report](09-validation.md).

## Run it

- [2D Star Patrol](../run-rockets-2d.cmd): animated pixel rocket, aliens, star field and planet.
- [3D Star Patrol](../run-rockets-3d.cmd): native solid models, named colored parts and directional lighting.
- [Portable Windows ZIP](../dist/Langmake-0.2-windows-x64.zip): extract and run either rocket launcher. Keep its files together.
- [Authoring guide](10-text-assets.md) and [agent command contract](08-agent-api.md): schemas, examples, asset export and revision-checked edits.

A/D steers, W/S changes altitude, and Space fires. P pauses, R restarts, and Escape closes. Both games use the same original 6.67-second electronic theme and synthesized laser effect. Every visual asset and note is authored in JSON; Luau supplies gameplay. Conversation and inference remain external to the engine.

## Executed checks

These results record technical verification. They do not record human gameplay acceptance. Game creation follows the [human-directed playtesting workflow](08-agent-api.md#human-directed-development-and-playtesting), with the human developer evaluating the playable iterations and directing subsequent changes.

| Check | Observed result |
| --- | --- |
| Native Release build | C++20/GCC 15.1, CMake and Ninja; all targets linked successfully |
| Project native suite | 131 checks passed: source graph revisions, boundaries, validated edits, copy, checkpoint recovery and ordinary-failure rollback |
| World native suite | Passed real Box2D/Jolt physics, sandbox budgets, animation timing/restart, visual controls, velocity and bounded audio events |
| Asset native suite | 86 checks passed: pixel alpha/drawing/crop/animation, PNG export, geometry/CSG volumes/transforms, bounds, opaque colors, OBJ parts and all eight sample graphics recipes |
| Music native suite | Passed note timing, envelopes, pattern arrangement, slides, deterministic noise/looping across block sizes, allocation-free rendering, validation and WAV output checks |
| Existing integration suite | Six groups passed, including original starter creation, actual graphics captures and revision/rollback behavior |
| Original game playthroughs | Both input-only wins reproduced: 354 steps in 2D, 876 in 3D |
| Existing live suite | 16 checks passed: pause/step, input, exact capture ordering, concurrent captures, source edits, failures and recovery |
| New asset integration suite | 11 groups passed: both new starters copied with assets and won in 480 steps; actual 2D/3D rendering; exports; revisions; strict JSON proposals; source/sidecar overwrite protection; isolated mesh preview |
| Live asset changes | Palette changes alter actual rendered rocket pixels; invalid recipes keep the previous world; restoring valid sources clears errors; firing produces sound events and defeats an alien |
| Live audio | Device available on this workstation; tested firing reported zero dropped audio commands |
| Portable package | Nine check groups passed with empty PATH and a separate working directory: all four starters create, validate, simulate and render; native asset schemas/exports work |
| Archive integrity | ZIP CRC, entry set and every packaged file's SHA-256 verified |

The test fixtures and complete JSON command transcripts remain inside `.cache/`. Relevant runs:

- Final asset integration: `.cache/integration-tests/20260918-122631-de2e17a745/report.json`.
- Original integration: `.cache/integration-tests/20260918-122452-6657161889/report.json`.
- Original playthroughs: `.cache/playthrough-tests/20260918-122457-3dc0f22d14/report.json`.
- Original live controls: `.cache/live-tests/e92156ec0fdc4bddaabdfccc17e938fc/`.
- Package: `.cache/package-smoke/33a2e42a44ad472d900e53d371de6dfb/report.json` and adjacent `archive-verification.json`.

The directory-symlink native test was skipped because this host denied symlink creation. Canonical source-root checks remain implemented; that specific runtime test is not claimed as passed.

## Inspectable outputs

The [2D game capture](../evidence/star-patrol-2d.png), [3D game capture](../evidence/star-patrol-3d.png) and [isolated rocket model](../evidence/rocket-model.png) were rendered by Langmake and visually reviewed. Their adjacent `.png.json` files retain camera, simulation and asset-revision metadata.

The [original theme WAV](../evidence/star-patrol-theme.wav) is 48 kHz stereo PCM16, 320,000 sample frames. Its [measurement sidecar](../evidence/star-patrol-theme.wav.json) records a peak of approximately 0.4263 and zero clipped frames. Repeated synthesis produced identical bytes on this build. These are timing/signal checks, not a claim of professional music mastering or cross-platform floating-point identity.

## Package identity

`dist/Langmake-0.2/` contains 73 manifest-listed files plus `package-manifest.json`. The ZIP contains 74 entries and is 6,823,902 bytes. Its executable matches `build/langmake.exe`, is 19,177,441 bytes and imports only Windows-provided DLLs. Static runtime linkage avoids requiring a compiler installation to play.

The package documentation was refreshed to establish human-directed development and primary human gameplay playtesting. Only `AGENTS.md`, `AGENT-API.md`, `TEXT-ASSETS.md` and `START-HERE.txt` differ in payload from the previously qualified ZIP; the runtime and examples are byte-for-byte unchanged. Package links, manifest hashes, ZIP CRC and archived file hashes were checked, recorded in `.cache/workflow-docs-package-refresh.json`. Gameplay tests were not rerun for this documentation-only update. The package test report above retains the original archive identity; the identity below describes the refreshed download.

Executable SHA-256:

```text
8dcca3c8d7157e2a7fa77aac158e40964627e42ec89838fb53721ac8c108a1e7
```

ZIP SHA-256:

```text
612a60cc160b32eb333fff92eeb32149cd32f40863a9cc6773423ddab711d12c
```

The package includes the engine, four original examples with sources, launchers, primitive shader binaries, asset schemas, authoring guides, dependency lock and license notices. Manifold and the embedded bgfx asset shaders are pinned and attributed. No external character, texture, music or sample pack is bundled.

## Remaining limits

This milestone supports pixel/raster art, frame animation, colored solid models and electronic synthesis. Organic modeling, skeletal animation, textured/PBR meshes, imported animated characters, recorded guitars/vocals and a full music-production environment remain future work. Mesh materials are opaque and collision still uses primitive boxes/spheres. Reload restarts gameplay and music.

Generated exports are derived files, not a shipping compiler format; source recipes are compiled on load with an in-process revision cache. Audio playback queues are bounded and report dropped requests under overload. Checkpoint restore rolls back ordinary write failures but is not a crash-atomic multi-file transaction.

Windows/D3D11 is the qualified graphics target. Empty-PATH package tests ran on the development workstation, not a clean machine. Other desktops, consoles, mobile and VR require separate implementation and device qualification.
