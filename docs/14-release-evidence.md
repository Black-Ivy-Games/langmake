# Langmake 0.3 execution evidence

September 18, 2026. This is evidence for a **Windows/D3D11 alpha**, not qualification for every intended platform or a declaration that the human has accepted the reference games' design. Technical verification and human gameplay feedback are separate.

## Test environment

- Microsoft Windows 11 Pro, version 10.0.26200.
- AMD Ryzen 7 5800X, 8 cores / 16 logical processors; approximately 64 GiB RAM.
- NVIDIA GeForce RTX 4080 SUPER, driver 32.0.16.1692.
- C++20 MinGW build, Direct3D 11 through bgfx. Pinned dependencies and license provenance are recorded in `dependencies.lock.json` and `THIRD_PARTY_NOTICES.md`.
- Integration builds initially used `.cache/build-0.3/`. The final regression run uses `build/langmake.exe`, version 0.3.0, SHA-256 `66927a662c59e9f5ded6bd8421a7bf0ebf52cf6dbffe34ef9d13376b7f9162c4`.

## Executed subsystem evidence

| Area | Executed evidence | What it establishes |
| --- | --- | --- |
| Native model assets | 37 checks in `model_tests` | Native joint hierarchy/rest transforms, attached geometry, ozz sampling/crossfades, looping/nonlooping clips and markers; generated glTF/GLB inverse binds and deformed vertex positions; unsupported/corrupt/path-escape rejection; project asset integration |
| Imported skin graphics | Generated glTF/GLB fixture captured at ticks 0 and 30 | Both equivalent imported skinned triangles render and translate with the sampled clip; PNGs and exact-tick sidecars were visually inspected |
| Live workflow | 9 scenario groups in `scripts/workflow_tests.py` | 652-entity concurrent inspection/control; forced Windows replacement denial makes observations stale while gameplay survives, then recovers; locked receipt cannot replay a mutation; aged receipt cleanup preserves evidence; 2D/3D timeline parity; player isolation; anchored HUD pixels/data at two resolutions; scene audio routing and restored timing |
| Native project/world systems | 141 project checks and 111 game-system checks, plus the original world suite | Complete source graph, revision-checked transactions, checkpoints, sandboxed modules, dynamic lifecycle/queries, controllers, hierarchical overrides/attachments, transactional scene preparation, fresh-process save reconstruction, migrations, camera collision, seeded start state and 128-agent navigation |
| Reference projects and editable modules | 38 checks with `create_gameplay_examples.py --verify --graphics`, plus validation and offline Luau checking of each reference | Repeated fixed-input completion, scene/save continuation, inventory/equipment/reward rules, cinematic actor tracks, selection/follow/recall/timer contracts, 128 followers and exact-tick capture/state agreement |
| Sustained simulation | 36,000 ticks with 128 moving/repathing agents | Bounded measured headless workload and steady memory on the stated host; results below |
| Audio scene integration | 600-tick adventure soundtrack and fresh-process resume | 480,000 stereo frames for the ten-second scenario, scene transition to relay, zero clipped frames/dropped commands; save at tick 350 followed by 60 resumed ticks produced 48,000 frames |
| Native UI/music/mixer | Native suites executed successfully during integration | Registered UI parsing/resources and bounded data; score/DSP behavior and deterministic scheduled mixer output across block sizes |

The final `scripts/test.ps1 -Graphics` run passed all nine CTest suites, six integration groups, both original platform-game completion/replay scenarios, eleven asset groups, 38 reference-project checks, sixteen live-control checks and nine workflow groups. The native project suite skipped its directory-symlink test because this host does not permit creating that symlink; ordinary traversal/invalid-source rejection checks ran. A passing same-host package test is not a clean-machine qualification.

Workspace evidence locations:

- `.cache/gameplay-reference-verification/report.json`, per-project simulation results, saves and PNG/JSON capture pairs.
- `.cache/final-release-tests.log` and `build/Testing/Temporary/LastTest.log` for the final aggregate/native runs.
- `.cache/integration-tests/20260918-151425-47161ca370/report.json` for original integration; `.cache/playthrough-tests/20260918-151428-d1b181e608/report.json` for original game scenarios.
- `.cache/integration-tests/20260918-151429-a8cbaa32f5/report.json` for assets, including actual audio-device initialization.
- `.cache/live-tests/80d77dd176ec4d28a950b9a645887fc9/report.json` for live controls and invalid-edit recovery.
- `.cache/workflow-tests/cfa230a4a0444af98ffa8c7aac09476e/report.json` for the nine final workflow groups. The earlier extended 120-iteration contention run is `.cache/workflow-tests/75ab1ca2e5bb4a9b99678e2126f3adba/report.json`.
- `.cache/performance-tests/a77356fd8de64e9ebae9bfe97b5c997b/report.json` for final-build hardware, timing, memory samples and graphical profile.
- `.cache/audio-scene-check/88f0de012b6f439cb0ca52560adfa95c/report.json` for adventure soundtrack/restore checks.
- Native CTest output in the active build directory. Re-run after changing the relevant source; generated cache evidence is not part of the authored game.

## Reference scenarios

**Signal Orchard:** The 600-tick native-input scenario talks to Ada, accepts the task/reward, equips the tuner, collects a coil, uses animation-marker-driven attacks to disable the sentinel, enters the relay area, consumes the coil and completes the signal quest. Separate checks restore a mid-adventure save and finish, and restore during a cinematic before its actor position/rotation reaches the expected endpoint. Module probes take the alternate dialogue branch, repeat the reward without duplication, reject invalid equipment/over-capacity changes and test timer consumption.

**Mosslight Assembly:** The 3,300-tick scenario spawns 128 articulated workers, routes them around a central wall to collect light, sends them home, meets the 100-delivery objective and exercises another spawn/destroy pair. Group behavior uses explicit per-worker assignments, so a delayed arrival from an old command cannot count as delivery under a new command. Separate inputs select half the team and follow the commander using native navigation, then verify that selection and follow state survive a fresh-process load.

**Lantern Post:** The 300-tick 2D scenario accepts the keeper's task, collects a star, lights the post and enters the garden area. A fresh-process load returns through the garden door to the prior Box2D village; the collected star remains absent and the quest remains complete. The same inventory/dialogue/quest/UI module family is used in 2D and 3D.

All three validate and pass offline script checking. Observable state repeats on the tested host. Their captures at tick zero, midpoint and completion match separate headless simulations for declared game state, entities, UI, scene, camera, navigation and revisions. Timings/device measurements are excluded from equality checks. Screenshots were inspected for native articulated characters, readable screen-space UI and the strategy wall route. Human playtesting remains pending.

## Performance observations

The sustained 128-agent workload completed **36,000 fixed ticks** (ten simulated minutes) in approximately 57.94 wall-clock seconds:

| Measurement | Observed value |
| --- | ---: |
| Median simulation step | 1.5428 ms |
| 95th percentile simulation step | 1.8984 ms |
| Maximum simulation step | 4.3907 ms |
| Steady private-memory change after warmup | -98,304 bytes |
| Steady private-memory range | 98,304 bytes |

These are headless simulation figures for this fixed scene/build/host. A separate 600-tick graphical run at 1280×800 recorded 596 frame samples: median **6.9357 ms**, p95 **7.1400 ms**, maximum **7.9002 ms**. Its final observation reported 787 draws, 0.3144 ms GPU time, approximately 208 MiB private memory and 134 MiB working set. The native ozz test sampled 128 simple characters for 120 ticks in 0.00233 seconds. These references use simple low-poly models and do not establish a general production-art budget.

Optional collision/navigation/targeting/camera overlays were captured and visually inspected at `.cache/debug-captures/tick-180.png`; the sidecar lists the requested overlays. Collider bounds, worker paths and goals remain behind the screen-space HUD. This debug pass was verified during integration; the final performance run above has debug overlays disabled.

## Fresh-agent authoring exercise

A separate ephemeral agent received only a copied portable toolkit, a small original game brief and its authorized exercise directory. It used packaged docs, discovery commands, templates and native commands; it did not access engine sources, the internet, external art/music tools or computer-use gameplay.

It created **Signal Kite**, a two-area adventure with three distinct pickups, branching guidance/color, an original articulated kite model, original synthesized music/chime, a screen-space HUD and a runtime-spawned beacon light. Twenty assertions passed: both branches complete, revisits preserve progress, repeated repair is idempotent, mid-game and completed saves restore in fresh processes, live save/load input actions restore state after movement, captures match simulation, and offline audio is unclipped. Completion occurs at tick 282. Its handoff explicitly leaves movement feel, clarity, physical controls and listening approval to the human.

No confirmed engine failure or engine-defect workaround was needed. Its command logger initially overwrote a test save; separating log/artifact paths fixed that harness error and the replacement saves loaded. All five documentation findings were addressed: current audio-effects wording, a portable alias for the schema's runtime-guide path, explicit copying of the starter timeline and portable example commands, command-relative timeline offsets after loading, and scene-local `won`/`message` semantics. The guide also distinguishes queued save requests from confirmed writes. A final-build rerun passed five additional groups covering validation/checking, original and alternate completion, fresh-process saves and exact-tick captures. Camera comparison retained every original field and accounted for the final build's additional false `target_locked`/`occluded` flags.

Exercise evidence is under `.cache/fresh-agent-exercise/`, including `HANDOFF.md`, `usability-report.json`, the command ledger and `evidence/assertions.json`. Final-build requalification is `.cache/final-fresh-agent/320772660b874703939eccd828be2c4f/report.json`. The next-agent brief ships as `GAME-CREATION-BRIEF.md` in the portable package.

## Portable distribution

The final executable passed **14 portable verification groups**. All seven templates create, validate, pass offline script checking, simulate and render from the package with an empty tool PATH and a separate working directory. Native sprite/model/audio exports, bundled font HUD, exact-tick captures and game-audio rendering also pass. The ZIP was physically extracted, every manifest hash checked, and its extracted executable created, checked and rendered a new adventure without source/build access.

The original runtime qualification covered 161 manifest-listed files plus `package-manifest.json`: executable, shaders, font, seven examples, launchers, API schemas/declarations, guides, dependency inventory and licenses. Subsequent documentation additions, including the prompt library, are recorded separately with the current file count and archive hash. The package is at `dist/Langmake-0.3/`; the archive is `dist/Langmake-0.3-windows-x64.zip`. Runtime evidence is `.cache/package-smoke/e3ff19f3aa2e415aa6fbf8066e6f5802/report.json` and `.cache/final-package-smoke.log`. The release record beside the ZIP carries its current SHA-256 and documentation-refresh verification. This is same-host extracted-package qualification, not a separate clean-machine test.

Existing authored games, the four original starters and `dist/Langmake-0.2*` were preserved. Use the `run-*.cmd` launchers for human playtesting; the reference README files provide controls. The package's `GAME-CREATION-BRIEF.md` is the handoff for a new external agent.

## Reproduce the checks from a source checkout

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
powershell -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -ExecutionPolicy Bypass -File scripts/test.ps1 -Graphics
python scripts/create_gameplay_examples.py --verify --graphics --engine build/langmake.exe
python scripts/performance_tests.py --engine build/langmake.exe --graphics
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
python scripts/package_smoke.py
```

Graphics checks open temporary windows and require a working desktop/GPU backend. The test scripts and compiler sources are in the source checkout, not the player package. Use the supported scripts' `--help`/PowerShell parameters when targeting the integration build. The reference generator's `--verify` mode does not rewrite examples; calling it without that flag regenerates the three engine-owned projects.

The model test can materialize its original glTF/GLB skin fixture with `model_tests.exe --fixture-root .cache/model-fixture`. Both imported triangles were captured and visually inspected at ticks 0 and 30; their animation moves upward between samples. Evidence is in `.cache/model-fixture-captures/tick-0.png` and `tick-30.png`, each with an exact-state JSON sidecar. This graphical import evidence supplements the native inverse-bind/deformed-vertex checks.

## Remaining qualification and honest limits

- Human acceptance of movement, combat, pacing, story, visual style and sound is pending. Automated scenario success is not a substitute for the human playing.
- Windows/D3D11 is the implemented target. No Linux, macOS, mobile, console or VR device/export qualification is claimed.
- A packaged same-host/extracted launch is distinct from a clean machine. The latter remains a portability gate unless explicitly recorded.
- Models use CPU skinning and simple base-color/vertex-color materials. Imported glTF is deliberately bounded; no FBX, compressed/morph animation, retargeting, IK, root motion or full PBR is claimed.
- Navigation is bounded ground/top-down steering around primitive geometry, with explicit obstacle rebuild. No off-mesh jumps, streaming world or multiplayer is implemented.
- Saves reconstruct declared systems and versioned game state, not an arbitrary Luau VM, physics solver or sample-exact audio-device continuation.
- Synthesized scores support filters/delay/reverb and bus mixing. Samples, SoundFonts, recorded instruments and vocals remain future work.
- Performance and deterministic evidence apply to the measured workload/host. Cross-platform floating-point lockstep and arbitrary production-scene performance are not established.

The current contracts and limitations are in [the agent API](08-agent-api.md), [runtime guide](13-runtime-guide.md) and executable discovery commands. The broader architecture/roadmap documents include future work.
