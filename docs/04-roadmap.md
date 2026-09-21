# Roadmap and validation

The independent runtime foundation is selected and implementation is authorized. A runnable [Windows alpha](07-first-release.md) combines the native control loop with early 2D/3D reference mechanics and recorded validation. The broader phases below are goals, not a claim that the full roadmap is complete. Sequence work by demonstrated results. A general-purpose runtime with mature desktop, mobile, XR and console support is a sustained engineering program; the playable alpha is a smaller milestone.

Game-development milestones follow the [human-directed workflow](08-agent-api.md#human-directed-development-and-playtesting): the human sets direction and performs the primary gameplay playtesting; agents implement and run technical checks. Record human feedback separately from automated evidence. Engine regression tests remain useful and do not constitute human approval of game feel, difficulty or creative direction. When human feedback is pending, report that status explicitly.

## Phase 0 — agree on the product and baseline

The foundation is C++20 with Luau, model-independent authoring and Windows as the first host. The alpha scope is two primitive reference games with separate physics worlds, file editing, inspection, simulation, captures and packaging. Exact library revisions are in [dependencies.lock.json](../dependencies.lock.json). Record actual reference hardware and target profiles during qualification; tool availability does not establish GPU capability or runtime performance.

Exit: agreed foundation experiment, reference-game briefs, measured-success criteria, directory policy and dependency/license inventory. Decisions and pinned inputs exist for the current alpha; its release document records observed evidence and remaining qualification.

## Phase 1 — prove an agent can control the foundation

Current experiment: integrate C++20, SDL3, bgfx, Flecs, Luau, physics in each dimension and a native command interface. The alpha renders colored primitives, exposes project/runtime state and runs behavior scripts without native recompilation. Both reference games have completed input-only replays and inspected rendered captures. Textured sprites, imported meshes, lighting, complete animation, navigation and production game UI are later work.

The accepted 0.3 milestone qualifies Windows/D3D11 and moves the bgfx/OpenXR experiment to the platform milestone. At that milestone, qualify device/queue setup, externally owned swapchain textures, synchronization, stereo submission and session lifecycle on a selected Windows runtime and Android headset before claiming XR support. Keep renderer-specific integration behind adapters; a failed XR experiment may require a different backend for that platform.

Demonstrate creating a project, placing and inspecting primitives, changing behavior, launching a preview, collecting a capture and semantic state, exposing a parse error, repairing it and producing a runnable desktop package. Validate a round trip from text to scene to text with stable references. Separate headless checks from GPU-backed captures. Imported asset round trips belong to the subsequent asset-pipeline milestone.

Exercise the same operations through an ordinary scripted client and an external agent. Separate engine operation time, process/startup overhead, and model reasoning time. Where useful, compare against an existing engine's CLI workflow; mouse automation alone would set an artificially weak baseline.

Exit: a recorded end-to-end run with zero editor clicks, complete diagnostics, two repeatable scenario runs, and a packaged game launch. Resolve any required capability missing from a library by a bounded adapter, replacement library, or explicit scope decision before extensive content depends on it.

## Phase 2 — the authoring contract and first 2D game

Implement the CLI, versioned schemas, project summary, narrow queries, transactional source edits, stable IDs, checkpoints, validation, and evidence collection. Add optional MCP only after CLI semantics work. Maintain one shared operation implementation.

Reference game: a one-level 2D platformer with movement/jump, collision, collectibles, a hazard, restart, win state, a small HUD, sound, and keyboard/gamepad input. Use original primitives or clearly licensed art.

Exit: an agent can build and refine the game from the human's brief, recover from a deliberately broken reference/script, restore an earlier working revision, and export it. Essential scenarios pass in a clean project copy. The human plays a meaningful iteration, provides feedback and directs a revision; record that cycle separately from the technical checks. Selection context can support a request to change a specific object.

## Phase 3 — the 3D game and asset pipeline

Future reference-game expansion: a small third-person courtyard with an animated character, collision, a key and door, lighting, spatial audio, a pause/settings screen and one save slot. Import a glTF asset, inspect its clips/materials and exercise controller input. The current alpha's primitive 3D example does not establish these animation, import or save features.

Complete the 3D scene/prefab operations, camera inspection, navigation/collision overlays, import cache, and error-to-source mapping. Add only the renderer features needed by this game initially.

Exit: playable game and package, with a human playtest and feedback-driven revision recorded; agent can diagnose a misplaced collider or missing animation from evidence; no required editor-only authoring step. Repeated asset import and rebuild retain references. Measure frame time and memory on named hardware.

## Phase 4 — desktop reliability

Exercise Windows, Linux, and macOS builds on appropriate hosts and test devices. Add crash collection, controller reconnect, focus changes, resizing, audio-device changes, persistence failures, and release-build checks. Retest authored projects after engine upgrades using explicit migrations.

Exit: a documented supported configuration for each desktop OS; a fresh checkout can rebuild both reference games from pinned inputs. “Reproducible” initially means equivalent content and behavior with recorded inputs; signed packages may not be byte-identical.

## Phase 5 — mobile and first VR devices

Mobile: add touch UI, safe areas, screen-density handling, pause/resume, storage permissions where needed, and thermal/memory budgets. Build and test on at least one Android device and one iOS device using the required host/toolchains.

VR: start with one desktop OpenXR configuration and one standalone headset. Add tracked poses, action bindings, controller/hand capability checks, interaction UI, and comfort settings. Rendering correctness and headset frame timing must be tested physically.

Exit: both reference-game mechanics adapted sensibly to the selected device inputs, device-tested builds, and clear per-target capability results. A conventional flat-screen game does not become a well-designed VR game by changing its export flag.

## Phase 6 — console pilot and advanced capability tracks

Investigate developer access and available porting paths earlier, in parallel with desktop work, if consoles are a priority. The creator must handle required account/legal agreements. Once access exists, select one console and prove compiler, renderer, interpreter/AOT path, input, save data, suspend/resume, platform services, and packaging on its development hardware.

Exit: an actual device-tested pilot and a certification gap list. Add more consoles individually. Expand networking, streaming worlds, advanced rendering, content tooling, and accessibility against concrete games rather than claiming feature parity with established engines.

## What we measure

An external benchmark harness records model/provider/version, prompt, agent configuration, engine/tool versions, hardware, target profile, cache state, and repository revision for each authoring benchmark. This metadata does not require a model integration in the engine. Separate model latency and token cost from engine work. Run multiple attempts; record failures and manual intervention instead of reporting only a successful demonstration.

| Measure | Initial proposed goal, to validate on named hardware |
| --- | --- |
| Required editor clicks for supported tasks | Zero |
| Common warm data edit to visible preview | Median at or below 2 seconds; report p95 and scene size |
| Warm script edit to playable scene | Median at or below 5 seconds for reference games; restart allowed |
| Short semantic query | p95 below 250 ms for the reference scenes, excluding model latency |
| Agent task reliability | At least 9 of 10 bounded reference tasks completed with no manual file repair, each attempted three times |
| Fault recovery | Every defined invalid-reference/parse-error fixture yields a located error and preserves a recoverable project |
| Runtime budget | Desktop reference target 60 FPS; inspect CPU/GPU p95/p99 and memory, not only averages |
| XR budget | Meet selected headset refresh budget; for 90 Hz, about 11.1 ms total frame interval |
| Platform confidence | Report separately: package created, launched, tested, and store/certification status |

These are design targets, not measured claims. Include cold import, first shader compilation, startup, and packaging as separate timings so warm-cache results cannot hide expensive steps.

## Acceptance checks that matter

- Source identity: rename/reparent without breaking references; clone prefab instances without duplicate identity; save/load and instantiate scenes consistently.
- Concurrent work: reject stale service patches and detect external file edits instead of silently overwriting newer source.
- Recovery: stop halfway through a service-owned transaction and recover the last complete revision; diagnose malformed direct edits separately.
- Evidence freshness: a capture/test identifies the loaded revision and fails clearly when the requested source has not imported or reloaded.
- Gameplay: input replay checks collision, collection, death/restart, win state, and save/load.
- Rendering: known-camera captures plus human inspection on at least one real GPU per supported backend.
- Packaging: install or unpack on a clean target and run without the authoring service or model.
- Target limits: unsupported features return precise diagnostics, not silent substitutions.
- Workspace policy: downloads/generated files stay inside configured roots; report external SDK prerequisites before installing them.

## Work ownership and effort model

For a small team, share work across runtime/integration, agent tools, assets/rendering, and test/platform responsibilities. One person can cover several roles, but device validation and content quality still require real attention. Model-assisted code generation accelerates implementation; it does not establish correctness or eliminate driver and certification work.

After Phase 1, estimate the next phase from measured integration and iteration costs. Avoid committing to an all-platform completion date before a native package and both dimensional workflows exist. A custom-runtime route adds renderer, animation, UI/text, resource management, navigation, and tooling work before it reaches the same feature baseline.

## Main risks and the decision each triggers

| Risk | Response / decision point |
| --- | --- |
| New API exposes complexity without reducing work | Benchmark ordinary scripts and agents; focus on diagnostics, observation, precise changes, and recovery |
| Scope expands to every game genre | Ship two representative games and maintain a visible feature backlog |
| Library blocks crucial control | Prove adapters in Phase 1; replace the specific dependency before extensive content |
| Agent edits create inconsistent state | Stable IDs, schema checks, revisioned batches, fixtures, recoverable source |
| Custom renderer absorbs the project | Use bgfx for graphics access; constrain initial materials/lights; resolve the XR gate at the platform milestone |
| Platform claim exceeds evidence | Separate intended, buildable, tested, and certified status per device family |
| Native upgrades break games | Pin dependencies; test migrations on saved fixtures before upgrading |
| Tools accidentally depend on one agent or model | Exercise the same operations through a plain script and at least two external agent configurations |
| Assets are expensive or poorly licensed | Use primitives first and retain provenance for every imported asset |

The immediate deliverable is the runnable Windows alpha and its recorded evidence. The 0.3 runtime and workflow milestone is described in [the runtime guide](13-runtime-guide.md) and [executed evidence](14-release-evidence.md). XR remains an unresolved platform-milestone gate, not a blocker on Windows runtime development.
