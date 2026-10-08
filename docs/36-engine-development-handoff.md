# Langmake engine, Studio and UI editor: development handoff

**Checkpoint: September 22, 2026.** Prepared for a new agent/context window in `.`. This is an engineering continuation document, not a new feature authorization or a replacement for [AGENTS.md](../AGENTS.md). Paths below are relative to the repository unless explicitly identified as machine-local. Read current source and capabilities before relying on this dated snapshot.

The previous authorized implementation is complete: the optional light UI editor was implemented, tested and packaged. The current request was to write this handoff. **There is no unfinished authorized implementation to resume automatically.** The human will direct the next development iteration. Human editor feedback is still awaited.

## 1. Start here in the new context

1. Read [AGENTS.md](../AGENTS.md), [README](../README.md), this handoff, [agent API](08-agent-api.md) and [runtime guide](13-runtime-guide.md).
2. Inspect `git status --short`, the current branch and recent history. Preserve the substantial existing uncommitted implementation described below. Do not reset, clean, replace generated history, or assume untracked files are disposable.
3. Query the actual executable with `build/langmake.exe capabilities`, `schema`, and `script-api`. Runtime version **0.5.0** alone is insufficient: audio feature revision **1** and UI observation revision **1** distinguish newer builds.
4. Read [Studio module instructions](../studio/README.md), [Studio architecture/workflow](17-studio.md), [UI editor guide](34-ui-editor-guide.md), and the relevant qualification records before extending those areas.
5. Confirm the human's requested next scope. A question about a possibility is not permission to implement it. Once a change is authorized, carry it through implementation, appropriate checks, documentation and an honest handoff without repeatedly requesting routine permission.

Suggested opening prompt for the next agent:

> Read AGENTS.md and docs/36-engine-development-handoff.md in this workspace. Inspect the current working tree and actual capabilities before making assumptions. Preserve all existing uncommitted work and the engine/Studio separation. My next authorized change is: [describe the change]. Implement that scope, verify the affected systems, update canonical docs and relevant prompts, and report technical evidence separately from human acceptance.

## 2. Product intent and decisions to preserve

Langmake exists to dramatically lower the effort needed for a human to develop a game through an AI agent. The human owns creative direction, priorities and acceptance. Agents implement ideas and perform technical verification. The human remains the primary gameplay playtester.

Three complementary products share ordinary project files:

| Product | Responsibility | Must remain independent of |
| --- | --- | --- |
| Native runtime and CLI | Execute games; validate, simulate, inspect, capture and package text-authored content | Model providers, chat accounts, Electron and Studio |
| Optional Studio | Manage projects/chats, connect the human's chosen provider, mediate worker tools, checkpoints and play sessions | Any requirement that games be created exclusively through Studio |
| Light UI layout editor | Help humans adjust an existing interface and communicate precise visual intent to agents | A proprietary UI source format, mandatory provider calls or a full visual game editor |

The runtime is independent C++20, with Luau gameplay, JSON definitions and RML/RCSS UI. It is not a Godot/Unity/Unreal wrapper. External directory/CLI agents are first-class users. Games run without an LLM account, Python authoring tools, a sampler or Studio. Optional export/setup tools may require Python and platform toolchains.

Additional user preferences and contracts:

- Keep work, caches, downloads and generated artifacts inside the authorized workspace. Do not install global tools. Remote work requires an explicitly authorized remote workspace.
- Maintain separate runtime-only and Studio distributions. Keep provider SDKs, credentials and conversation data out of the runtime and exported games.
- Game projects belong in their own repositories. The engine's `/games/` directory is ignored; bundled `examples/` are maintained engine sources.
- Never inherit another game's SSH configuration, signing team, Mac workspace or device. Set up each game/machine explicitly in ignored local target records.
- Use free/open-source dependencies with pinned revisions and license notices. Do not patch dependency checkouts to fix our integration.
- Prefer native validation, checking, simulation, captures and receipts for agent QA. Computer-use playthroughs do not replace human playtesting. Focused UI/input automation is appropriate technical verification.
- Do not edit, reset, inject inputs or reload an active human play session. Continue independent work while awaiting feedback.
- Plans, library support and successful host tests do not prove a platform port or physical-device behavior. Keep evidence attached to the actual runtime/artifact and hardware tested.

## 3. Repository and local checkpoint

At handoff preparation, this is a Git repository on branch `main`, with HEAD `3c55bae` (`Fix README UTF-8 text encoding`). Earlier commits include `4fa75c0` (ignore generated artifacts) and `07a994f` (initial 0.5.0 source import). An `origin` Gitea remote is configured; inspect `git remote -v` locally if needed. No credential belongs in this document. No commit or push was performed for the UI-editor work or this handoff.

**The checkout has substantial uncommitted changes. A fresh clone of HEAD does not reproduce the current working implementation.** Before this handoff was added, `git status --porcelain` listed 78 modified/untracked entries, some of which represent whole directories. This count is a checkpoint, not an invariant.

The existing changes include expressive audio, macOS support, mobile/workflow corrections, sprite/physics and recovery work, and the UI editor. They did not all originate in the editor task. Some files contain changes from several tasks. Do not stage an entire shared file as if every hunk belonged to a new fix.

Important untracked source groups include:

- `docs/28-macos-guide.md` through `docs/35-ui-editor-qualification.md`, audio/UI release evidence JSON, and new music/UI prompt examples.
- `scripts/macos/`, `scripts/music/`, `scripts/ui-editor/` and `examples/music/`.
- `src/audio_backend.cpp`, `src/recorded_music.cpp`, `src/macos_main.cpp`, `include/langmake/recorded_music.hpp`, recorded-audio tests and new licenses.
- `studio/renderer/ui-editor.js` and music/UI-editor tests.

The local `.cache/ui-editor/preexisting.patch` records the tracked diff at the start of the editor task. It is historical comparison material, **not a complete backup**: ordinary `git diff` does not capture untracked files. Do not automatically apply it to this already-modified checkout.

`.gitignore` excludes `build/`, `.deps/`, `.cache/`, `dist/`, `/games/`, all `.langmake/` directories, native binaries, compiler outputs, node modules and common signing/credential files. `studio/.gitignore` also excludes `.data/`. Preserve user games and local histories even though Git ignores them. Git ignore is not itself a release privacy boundary; package manifests and allowlists perform that job.

No build or test process needs to be resumed from the prior editor task. Re-check processes before removing locks or cleaning artifacts; the human may have opened Studio or a game since this checkpoint.

## 4. Source map

### Native runtime

| Paths | Responsibility |
| --- | --- |
| `CMakeLists.txt`, `dependencies.lock.json`, `THIRD_PARTY_NOTICES.md`, `licenses/` | Build targets, pinned dependencies, provenance and licensing |
| `src/main.cpp` | Native CLI parsing/dispatch and authoring-tool facade entry points |
| `src/project.cpp`, `include/langmake/project.hpp` | Project loading, schema/capabilities, validation and referenced content |
| `src/world.cpp`, `src/world_services.inc`, `include/langmake/world.hpp` | Simulation, entities, physics, gameplay services and persistence state |
| `src/script_api.cpp` | Luau bindings and generated API declarations |
| `src/timeline.cpp`, `include/langmake/timeline.hpp` | Fixed-tick input execution shared by verification paths |
| `src/preview.cpp` | Native presentation, controls, captures, live publication and platform integration |
| `src/io.cpp`, `include/langmake/io.hpp` | Shared file I/O and publication reliability |
| `src/assets.cpp`, `src/asset_runtime.cpp`, `src/asset_renderer.cpp` | Text asset compilation, runtime resources and rendering |
| `src/model.cpp`, `src/navigation.cpp` | Models/animation/import and navigation services |
| `src/ui.cpp`, `include/langmake/ui.hpp` | RmlUi rendering/input, observable UI and editor geometry |
| `src/music.cpp`, `src/audio_mixer.cpp`, `src/audio.cpp`, `src/audio_backend.cpp`, `src/recorded_music.cpp` | Scores, shared DSP/mixing, device output and recorded music |
| `src/cook.cpp` | Resolve/validate/copy the referenced graph into target content |
| `src/android_main.cpp`, `src/android_input.cpp`, `src/ios_main.mm`, `src/macos_main.cpp`, `platform/` | Native platform entry points, lifecycle, wrappers and input |
| `shaders/`, `include/langmake/platform_shaders.hpp` | Native rendering shaders and backend selection |
| `tests/`, `scripts/*tests.py` | Native unit/contract and command/graphics integration checks |

Do not treat a header or an early planning document as proof that every path is supported. Consult the concrete implementation, discovery output, tests and qualification together.

### Supporting stack

The current stack includes SDL3/bgfx (platform, input, graphics), Flecs (components), Box2D/Jolt (physics), Luau, Manifold (procedural solids), cgltf/ozz-animation (models), Recast/Detour (ground navigation), RmlUi/FreeType (UI), and miniaudio (audio). Optional sfizz authoring converts sampled text compositions into runtime recordings. Exact revisions/build options belong to the lock file and CMake, not this handoff. Previously selected RmlUi 6.3, FreeType 2.14.3, ozz 0.17.0 and Recast 1.6.0 are pinned integration choices, not permission to silently upgrade them.

## 5. Runtime support and boundaries

Implemented foundations include version-1 compatibility; version-2 scene/module/prefab/UI/definition registries; JSON-compatible observable state; deterministic input scenarios; dynamic prefab instances; independent visual and collision controls; spatial queries; character/game cameras; animated native models and bounded glTF imports; screen-space UI; one active scene with transitions; versioned saves; ground navigation; editable gameplay modules; text assets; and shared live/offline audio.

Important concrete limits and semantics:

- `project.json` remains the entry point. Registered sources/dependencies participate in revision tracking, validation, cooking and checkpoints. Arbitrary unregistered files are not automatically runtime content.
- Tick zero is initialized state before updates; tick N follows N completed steps. Graphical capture freezes at its requested tick to bind pixels and state. Keep source/asset revision, camera, scene, viewport and scenario identity together.
- Live publication must not terminate gameplay on transient file contention. Receipt delivery failure must not replay a mutation. Inspect selectively; use `--check-source` explicitly when comparing loaded/current revisions.
- `play` is the human/player mode with developer overlays, source watching and developer shortcuts disabled. `preview` is the development mode; reload restarts gameplay.
- Use declared state for save-critical progression, not only Luau locals. State reads return JSON copies; write modified values back. Save reconstruction is not VM/physics-solver serialization.
- Maximum 4,096 active entities; up to 256 entities in a prefab instance. Reserved deterministic spawn IDs and safe mutation boundaries must remain consistent with replay, destruction and persistence.
- Visual scaling/visibility/opacity are independent of physical participation. Hiding an object does not remove its collider.
- Legacy primitive physics and 3D remain axis aligned. Current source also has an opt-in shaped/rotating Box2D `physics2d` extension; read the runtime guide rather than extrapolating the README's broad primitive summary. That extension has Windows host coverage, not automatic mobile qualification.
- Models are bounded to 128 nodes/joints, 64 clips, 65,536 animation keys and 100,000 vertices/300,000 indices, with further resource budgets. Skinning is CPU-side. No FBX, automatic retargeting, root motion, IK or full PBR pipeline is provided.
- Navigation supports ground movement including top-down 2D, up to 256 active navigation agents. The strategy reference verifies 128 simple followers, not 128 arbitrary production characters. Handle partial/unreachable paths explicitly.
- JSON game state and UI state each have a 1 MiB budget. Project graph/resource budgets can reject content before a subsystem's nominal maximum.
- Profiles replace complete supported top-level presentation objects; they are not deep merges. Keep game logic/progression shared while explicitly choosing phone/tablet/desktop presentation.
- RML/RCSS is not browser HTML/CSS. Use supported properties, dp for mobile sizing, explicit stylesheet links and real native `ui-check`/captures. Safe area is x/y/width/height, not four insets.

Reusable modules/references are starting points, not genre completeness. There is no demonstrated commercial-engine parity, networking, world streaming, console release path or broadly qualified VR pipeline. Large Zelda/Pikmin-scale works still require substantial iterative content and engineering. See [runtime guide](13-runtime-guide.md), [text assets](10-text-assets.md), [workflow](26-game-workflow.md) and current discovery.

## 6. Studio: architecture, providers and security boundary

Studio is an optional Electron 44.4.3 application. Its checked-in `package-lock.json` pins tooling, including Electron packager and Playwright. Node 22.12+ is required for source setup/tooling. The qualified portable Studio distribution is Windows x64; source Mac adapters do not establish a qualified Mac Studio package.

| Paths | Responsibility |
| --- | --- |
| `studio/main.cjs`, `studio/preload.cjs` | Main process, constrained IPC, renderer bridge and OS credential protection |
| `studio/renderer/app.js`, `index.html`, `style.css` | Project/chat/settings/files/targets/Play interface |
| `studio/core/broker.cjs` | Project/chat orchestration, provider runs, checkpoints, memory, writer/play locks and UI-editor entry |
| `studio/core/store.cjs` | Durable Studio state |
| `studio/core/tools.cjs` | Bounded project file/image/documentation/native engine tools |
| `studio/core/host.cjs`, `mcp-bridge.cjs` | Host integration and local tool bridge support |
| `studio/core/android.cjs`, `ios.cjs` | Explicit platform command adapters |
| `studio/providers/api.cjs` | Streaming OpenAI-compatible Chat Completions and Anthropic Messages adapters |
| `studio/providers/codex.cjs` | User-supplied Codex app-server integration and Studio dynamic tools |
| `studio/providers/claude.cjs` | User-supplied Claude Code integration via private local MCP bridge |
| `studio/scripts/package.cjs`, `studio/test/` | Distribution assembly/integrity and tests |

The human can configure a supported API endpoint/model/key, a compatible local server, a Codex executable/account connection, or a Claude Code executable. Studio does not host/download models or bundle those coding-agent executables. A consumer subscription is not a universal API credential. Do not hardcode claims that a named model or effort level is available on a user's account.

At this checkpoint, profile persistence stores name, type, model, endpoint, executable and protected secret. There is **no implemented general effort/reasoning selector passed by the adapters**. If the human later requests that, add provider-specific capability handling and tests rather than assuming strings such as “ultra” work everywhere. Current API adapter is Chat Completions-compatible, not a blanket implementation of every provider API. Verify current official provider protocols before changing integration code.

Provider switching works between completed/cancelled turns through explicit context: conversation, brief, accepted decisions, tasks and handoff memory. Hidden provider reasoning or consumer-app chat history does not transfer. Tool/vision support is model-dependent.

Security and coordination contracts to preserve:

- Renderer has context isolation, sandboxing and no Node integration; validate IPC in the main process.
- API secrets are protected through Electron `safeStorage`; failure to encrypt must not fall back to plaintext. Secrets never go into project files, renderer state, logs or exports.
- Studio data defaults to `.data` beside the packaged executable or source module, overridden by `LANGMAKE_STUDIO_DATA`. It includes private account directories, chats and checkpoints. Never distribute a used data directory.
- Studio workers receive bounded project tools, not unrestricted shell/disk access. Codex built-ins are disabled in favor of dynamic Studio tools; Claude uses a restricted private bridge. External directory agents remain a separately authorized workflow.
- Revision-checked writes, contained paths and junction/symlink rejection matter. A model-provided path must not bypass the broker's trusted project/runtime selection.
- Checkpoint before worker edits and UI Apply; serialize Studio writers and preserve Play locks server-side, not only by disabling buttons.
- External writers are not magically locked by Studio. Coordinate them explicitly.
- Android/iOS build/deploy panels have their own prerequisite/selection rules. Do not claim that all remote workflow-facade operations already exist as Studio buttons. Windows Studio's iOS panel stages content; local Mac operations and independent remote CLI workflows are distinct paths.

Current tests establish tool/UI wiring, simulated provider protocol behavior and limited unauthenticated Codex protocol checks. They do **not** establish live creative output from every provider/account/model. Read [Studio README](../studio/README.md) for exact routes, restrictions and qualification.

## 7. Light UI editor: implementation and extension points

The editor is complete for its documented initial scope. Its purpose is modest human adjustment and precise agent direction, while agents continue authoring the game through text. Read the [implementation plan](33-ui-editor-plan.md), [guide](34-ui-editor-guide.md), [qualification](35-ui-editor-qualification.md) and [prompt examples](../prompts/13-ui-layout.md).

### Data flow and files

1. Studio calls `Broker.uiEditor()` under the existing exclusive writer/play lock. It resolves the service adjacent to the selected runtime, with a source-development fallback.
2. `scripts/ui-editor/editor.cjs` validates and cooks an isolated draft beneath `GAME/.langmake/ui-editor/UUID/game`.
3. The service invokes the real runtime capture at an explicit profile, viewport, density, safe area, tick and optional input timeline. The preview is native pixels, not a browser approximation.
4. `studio/renderer/ui-editor.js` displays the capture, native element bounds, ID list and editable properties. Dragging shows a provisional outline; a completed change triggers another native capture.
5. Apply verifies the original full-project revision and document hash, validates the draft, and atomically writes the selected RML file. Studio first takes a checkpoint.
6. Export creates `ui-design-reviews/UUID-VERSION/` containing screenshot, native sidecar, desired RML and review JSON, plus discoverable `GAME/ui-design.json`.

`scripts/ui-editor/ui-editor.cjs` exposes the same service to an external agent through one JSON request file. Studio is unnecessary. Node is an optional authoring dependency; it never enters an exported game.

Native observation revision 1 is emitted from `UiRenderer::inspect()` in `src/ui.cpp`: document identity, parent/offset-parent bounds, content size, transform indication and supported computed properties. `src/preview.cpp` now emits `ui_renderer` on Windows as well as the other applicable paths. Missing Windows metadata was an integration defect found and fixed during editor implementation. `src/project.cpp` advertises `capabilities.ui_editor`.

### Operations and state

Service operations: `discover`, `open`, `read`, `capture`, `patch`, `undo`, `redo`, `note`, `apply`, `export`, `discard`. All mutations use the last returned session/version; `read` can recover the latest state without a version. Do not hardcode version increments: capture and mutation can each advance them.

- `session.json` retains original source, bounded history, selected configuration, notes and capture references.
- Minimal inline-style edits target a unique quoted stable ID, preserving children, bindings, actions and unrelated markup. Clearing an override exposes the stylesheet value.
- A per-game `operation.lock` serializes service operations. A crashed process may leave it behind; verify the process stopped before removing it.
- A capture can fail after a draft edit was saved. The Studio UI reads the current saved version after failure rather than repeating the mutation. Do not replay an uncertain Apply or edit blindly.
- Undo/redo operate on the draft. Applied drafts are immutable for further editing; open a new one or use Studio History to restore source.
- Closing retains the draft. Saved drafts can be resumed after restart; configuration fields are restored from that draft.
- Discard removes the isolated draft and never reverses already-applied source.
- Notes marked `locked` are advisory human design intent, not filesystem permissions or authority over later human instructions.
- Export is local and does not silently send content to a provider. An exported proposal with `applied:false` is not installed source. Agents must reconcile changed source with the human's intent, not blindly replace it.

### Initial editor limits

Existing stable-ID elements, one root UI document from the selected profile per draft. No widget creation, reparenting, text-content editing, shared-stylesheet rewriting, scene-specific document selection outside that root list, animation editor or arbitrary CSS round-tripping.

Supported adjustments include offsets, size, margins/padding, font size, colors, opacity, display, text alignment and drawing order. Absolute elements can be dragged/resized; flow elements use properties or an explicit absolute anchor. Transformed elements/ancestors cannot be dragged/anchored. Complex inline functions remain source work. Resize uses native content-box size to avoid repeatedly adding padding.

Bounds: eight saved drafts per game, 30 edits per draft, 1 MiB RML, 128 notes of at most 2,000 characters, 128..2560 physical pixels per viewport axis, density 0.25..8 and ticks 0..3600. Native graph budgets still apply. Review exports persist intentionally; manage them as project authoring files. No filesystem-wide transaction with unrelated external editors is promised.

Phone/tablet presets check Windows host layout only. They do not qualify device fonts, touch, cutouts or performance. The canvas selects UI elements; it does not forward gameplay input. Reach a state through an explicit native timeline/tick.

### Tests and future inspection points

Canonical tests are `scripts/ui-editor/editor.test.cjs`, `scripts/ui-editor/integration.cjs`, `studio/test/ui-layout.test.cjs` and `studio/test/ui-layout.cjs`. The last exercises a real Electron app, with `LANGMAKE_STUDIO_PACKAGE` selecting a packaged distribution.

If extending the editor, preserve source compatibility and native evidence first. Potential improvements to investigate only if authorized include richer structural editing, scene-specific document discovery, better flow-layout affordances, capture failure cleanup/retention, and clearer property grouping. These are possible follow-ups, not promises or a diagnosed blocker to the delivered scope.

## 8. Workflow, platforms and deployment

The independent facade under `scripts/workflow/` supports per-game targets, immutable build attempts, resumable operations, verification scenarios, receipts and generated `HANDOFF.json`. Begin game work with `langmake context --project GAME` and [workflow guide](26-game-workflow.md). This engine-development handoff is distinct from a generated per-game handoff.

| Platform | Current supported route | Qualification boundary |
| --- | --- | --- |
| Windows | Native runtime, preview/play, portable standalone game exports through `scripts/windows/` and workflow facade | Current UI1 editor/runtime checks use Windows/D3D11 |
| Android | `scripts/android/android.py`, standalone APK/AAB tools, explicit ABI/serial/profile, native touch/lifecycle/receipts | Physical SM-A146U and emulator evidence exists; new feature/device coverage must be checked separately |
| iOS/iPadOS | `scripts/ios/ios.py`, authorized Mac/Xcode, explicit signing team/device, phone/tablet profiles; workflow SSH transport | Historical iPhone/iPad workflow and update tests exist; newer audio has simulator evidence, not new physical Apple listening qualification |
| macOS | SDL/Metal native desktop path and `scripts/macos/macos.py` local app packaging | Limited local evidence, ad-hoc signing; no broad Intel/Studio/notarization qualification |
| Linux, consoles, VR and other exports | Broader product direction | Do not advertise a qualified release merely because libraries support a backend |

Native workflow source map: `scripts/workflow/targets.py` handles explicit setup, `transport.py` handles transport, `operations.py` handles deployment/recovery, `verify.py` handles scenarios/observations, and `workflow.py` exposes commands/context. Consult actual help rather than inventing flags.

Per-game `.langmake/targets.local.json` is machine/directory-bound and ignored. Portable app identity, icon, build/version and presentation profiles live in authored data. Verification targets use a separate application identity/storage from human sessions. Source/profile/runtime/target fingerprints must match an installed artifact before inspecting it as current; editing source does not update a phone.

Historical Apple setup used a user-owned Mac and project-local SSH tools. **Do not hardcode that machine/account into the engine or reuse its setup for another game.** Discover the authorized existing target only within the game's local configuration. Pairing, Developer Mode, signing identity and connection state must be rechecked when needed.

September 23, 2026 local-checkout note: a game-specific iOS port reused an explicitly authorized Apple target. The SSH host/account, Mac workspace and signing team are private and belong only in ignored `.langmake/targets.local.json` and workspace-local SSH configuration. They are intentionally omitted from this public handoff. Recheck the selected game target before any remote action.

Mac Keychain signing sometimes required a generated `sign-local.sh` to be run by the human locally. Resume the recorded operation after that step; do not repeatedly rebuild, expose private keys, accept agreements for the user or assume an old helper is the current one. Network installation to a paired iPad was verified, but does not remove pairing/reachability requirements.

Keep the earlier transport corrections: receipt delivery may fail after execution; observe the original request ID rather than resending mutations. Distinguish verified missing optional files from generic transport errors. Android binary pulls use checked base64 transport after newline corruption was reproduced. Do not replace that with an apparently simpler unverified raw pull.

Read platform instructions before work: [Android agent guide](21-android-agent-guide.md), [Android qualification](20-android-qualification.md), [iOS agent guide](23-ios-agent-guide.md), [iOS qualification](24-ios-qualification.md), [workflow qualification](27-workflow-qualification.md), [Mac guide](28-macos-guide.md), and the corresponding `scripts/*/README.md`.

## 9. Music and sound context

Audio feature revision 1 is implemented. Native v1 synthesis remains compatible; v2 WAV/FLAC recordings, synchronized adaptive layers, recorded SFX, shared mixer/DSP, EQ/compression/limiting and offline rendering are available. Optional text composition uses pinned sfizz and a hash-checked CC0 VSCO-CE source pack. Baked games need neither sfizz nor the source bank. Curated source samples are an optional roughly 395 MiB download, not a Git payload.

Read [music and sound guide](30-music-and-sound-guide.md), [qualification](31-audio-qualification.md), [authoring implementation](../scripts/music/README.md), and [music prompts](../prompts/12-music-and-sound.md). `music status`, `music instruments`, `music schema` and `asset schema --type music` provide current discovery.

**Preserve human feedback:** initial sketches were called tacky; a fuller ensemble was also rejected as artificial/tacky. Passing PCM/clipping tests or adding instruments did not establish orchestral realism or artistic approval. The sample palette has limited articulations and no recorded legato transitions. Imported recordings can supply vocals/rock performances; this is not native singing or a neural song-generation system.

The user subsequently requested an underwater-ocean orchestral example. Its local work is under `games/beneath-the-blue/`, with an audition at `render-v2/audition.wav`. This is an ignored personal game/artifact, not part of the engine's canonical starter distribution; preserve it. Do not invent listening approval for it.

Resident decoded PCM limits are 96 MiB per asset and 192 MiB per compiled project; streaming is not implemented. Saves preserve music/layer targets but restart cursor/DSP state. Authoring-library verification on Windows does not qualify that library on all hosts; exported recordings are the portable runtime route.

## 10. Local tools, builds and tests

All commands below assume the repository root. Keep outputs under this workspace. Use the existing tools rather than globally installing replacements.

Windows native setup uses CMake, Ninja, MinGW g++, Git and Python. Use the authorized local tools on PATH; do not assume a particular installation directory. Historical checks used Python 3.9. The PATH Node used during recent work was too old for current Studio packaging. The tested workspace-local Node is:

```powershell
$node = '.\.cache\expressive-audio\tools\node-v24.21.0-win-x64\node.exe'
& $node --version
```

That path is a local convenience, not a fresh-clone prerequisite. On another machine prepare Node 22.12+ inside the authorized workspace. Keep npm/Electron caches local through `studio/launch.ps1`. Do not use Electron's Node mode to run the package tool; use a real supported Node executable.

Native build/regression:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Jobs 4
ctest --test-dir build --output-on-failure
powershell -ExecutionPolicy Bypass -File scripts/test.ps1
# Adds temporary windows/live and graphics workflow checks:
powershell -ExecutionPolicy Bypass -File scripts/test.ps1 -Graphics
```

`build/langmake.exe` is the latest editor-capable build at this checkpoint. `build/audio/` is an older audio release build and must not be substituted for UI observation revision 1. Keep adjacent shaders and fonts with an executable. Build and package scripts support explicit build directories; inspect their parameters before using an alternate tree.

Studio setup and focused checks:

```powershell
powershell -ExecutionPolicy Bypass -File studio/launch.ps1 -Install -NodePath PATH_TO_LOCAL_NODE
# Add -NpmCli PATH_TO_NPM_CLI_JS if npm is not discoverable.
$tests = @(Get-ChildItem studio/test -Filter '*.test.cjs' | ForEach-Object FullName)
& $node --test @tests scripts/ui-editor/editor.test.cjs
& $node scripts/ui-editor/integration.cjs
& $node studio/test/ui-layout.cjs
```

To check the existing packaged app:

```powershell
$env:LANGMAKE_STUDIO_PACKAGE = './dist/ui1/Langmake-Studio-0.5.0'
& $node studio/test/ui-layout.cjs
Remove-Item Env:LANGMAKE_STUDIO_PACKAGE
```

The real Electron test creates an isolated fixture and private Studio data under `studio/.cache/`; it does not use the human's normal account/chat store. General Studio tests include `studio/test/ui.cjs`, selection and platform suites. Some use explicit fake providers: report that honestly.

Additional focused suites are `scripts/music/test_music.py`, `scripts/music/integration_tests.py`, `scripts/android/test_android.py`, `scripts/ios/test_ios.py`, `scripts/workflow/test_operations.py`, `scripts/workflow/test_verify.py`, `scripts/test_mobile_assets.py` and `scripts/windows/test_windows.py`. Native integration scripts have their own arguments; read help/source. Run relevant checks after a change; do not repeat unrelated device deployments just because a documentation file changed.

`scripts/create_gameplay_examples.py --verify --graphics --engine build/langmake.exe` verifies references. Running it without `--verify` regenerates engine-owned examples. Do not use regeneration to edit a personal game or accidentally overwrite intentional source changes.

Text files should be read/written explicitly as UTF-8. Python 3.9's default Windows encoding can damage Unicode. README mojibake was fixed previously; an older Studio README line still contains a garbled arrow in its iOS-stage description. That is a documentation follow-up, not a reason to recode the entire tree.

## 11. Current evidence and artifacts

UI1 checks actually completed before this handoff:

- 13 native CTest suites passed.
- 79 Node tests passed across Studio/editor.
- Native editor integration verified capture/source identity, isolated drafts, geometry, undo/redo, revision guards, notes/pins/export, atomic Apply, persisted drafts, phone-density/safe-area selection and external stylesheet conflicts.
- Actual packaged Electron verified selection/property editing, drag displacement against native bounds, resize without padding growth, anchor preservation, saved-draft resume after restart, notes/export, Apply and Play locks. A screenshot was visually inspected.
- All eight packaged starters created, validated, checked, simulated and rendered from a separate directory with an empty PATH. Runtime ZIP extraction verified file hashes and native rendering.
- The CLI from the extracted runtime opened, patched and exported a draft while leaving original source unchanged.
- Studio ZIP entries matched every manifest hash and ZIP CRC checks passed. The source archive passed its own inventory/integrity verification.

These are earlier executed checks, **not tests rerun merely by preparing this documentation**. No creative approval or new mobile device qualification was inferred.

Sources of evidence:

| Record | Meaning |
| --- | --- |
| [UI qualification](35-ui-editor-qualification.md), [committed summary](releases/ui-editor-1-evidence.json) | Canonical UI revision scope and executed checks |
| `.cache/ui-editor/ctest.log`, `unit-final.log`, `integration.json`, `electron-evidence.json` | Local detailed native/Node/editor evidence |
| `.cache/ui-editor/package-smoke.log`, `extracted-cli.json`, `studio-archive.json` | Local package/extraction/CLI/archive evidence |
| `studio/.cache/layout-1790059876880/editor.png` | Visually inspected packaged editor screenshot |
| `dist/Langmake-0.5.0-ui1-evidence.json` | Combined local distribution report |
| [Audio qualification](31-audio-qualification.md), [audio evidence](releases/audio-feature-1-evidence.json) | Audio feature 1 checks and rejected musical examples |
| [Workflow qualification](27-workflow-qualification.md), [workflow evidence](releases/0.5.0-evidence.json) | Earlier physical Android/iPhone/iPad workflow checks and human feedback |

UI1 Windows executable SHA-256: `a27a64f8aa3de3e34f8f56abfdc4997948e99f97fc3344eef4613e8e196bab99`.

UI1 service SHA-256: `a583c20c827b5bf2652401cb9616fc2087b62f46ddaeed001e44c35a806c8171`.

Prepared distributions (ignored generated artifacts):

| Path | Contents |
| --- | --- |
| `dist/Langmake-0.5.0-ui1/` | Runtime, optional authoring tools, starters, docs and manifest |
| `dist/ui1/Langmake-Studio-0.5.0/` | Portable Studio with matched runtime |
| `dist/Langmake-0.5.0-ui1-windows.zip` | Runtime archive |
| `dist/ui1/Langmake-Studio-0.5.0-ui1-windows.zip` | Studio archive |
| `dist/Langmake-0.5.0-ui1-source.zip` | Source snapshot including Studio; produced by the iOS source packager, not a signed Apple build |
| `dist/Langmake-0.5.0-ui1-HANDOFF.md` | Human launch/use instructions |
| `dist/Langmake-0.5.0-ui1-SHA256SUMS.txt` | Archive hashes |

Archive hashes at this checkpoint:

```text
89e814cceb6704b39094610d6042c1c3de85c1c85ae44ad02c8410495e73747d  Langmake-0.5.0-ui1-windows.zip
356421b1382517875184210ecf28be6d49b08965c9aa5386171357cda8d044e9  Langmake-Studio-0.5.0-ui1-windows.zip
b25add1a7ac70e7c70d6b47425692f141d08b9168ea93f6fdb19df56336ad3b1  Langmake-0.5.0-ui1-source.zip
```

Earlier audio1 and platform releases remain preserved. An old binary with version 0.5.0 is not interchangeable with UI1. The UI1 Android player input comes from the previously qualified audio1 binaries; this editor release did not requalify new physical Android/iOS editor behavior.

## 12. Packaging and clean-machine continuation

Choose a **new** package destination for a new release; do not overwrite the evidence-bearing paths above. Update canonical docs first, then regenerate copies. Do not edit only `dist/`.

The UI1 runtime was produced using these local inputs; for a future iteration use a fresh package name:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package.ps1 `
  -BuildDirectory build `
  -AndroidPlayerDirectory .cache/expressive-audio/android-player `
  -PackageDirectory dist/Langmake-NEXT `
  -IncludeMusicAuthoring `
  -MusicExampleDirectory .cache/expressive-audio/music-room-final-cooked
python scripts/package_smoke.py --package dist/Langmake-NEXT --archive dist/Langmake-NEXT-windows.zip
& $node studio/scripts/package.cjs --runtime ./dist/Langmake-NEXT --output ./dist/NEXT
python scripts/package_ios_source.py --output dist/Langmake-NEXT-source.zip --include-studio
```

`NEXT` is a placeholder. Confirm all paths and parameter requirements; do not literally reuse an existing destination. Studio's directory/version naming is still 0.5.0 internally until intentionally changed. The runtime packager's default Android input points at an older workflow cache; pass the correct matching player input explicitly rather than silently mixing revisions.

The `.cache` inputs and `.deps/music/bin` are generated local resources, **not clean-clone prerequisites present in Git**. On another machine, build the runtime and necessary target players, prepare optional authoring dependencies and cook the demo before passing those paths. Skip optional sampled authoring/demo packaging only when that release's scope intentionally excludes them; document the resulting capability boundary.

The source packager collects `docs/`, prompts, scripts, native source and optionally Studio; it excludes local accounts, caches and personal games. Its manifest labels the archive as an iOS source preview and `ios_qualified:false`; that means source packaging is not native signing/device qualification, even though it is useful for another engine checkout. Git remains the normal collaborative source route once the current uncommitted work has been intentionally reviewed and committed.

The already verified UI1 archives predate this new handoff document and README link. They are intentionally unchanged, preserving recorded hashes. Future generated packages will collect the new canonical document. This documentation-only addition does not require a rebuild or retroactive package qualification claim.

For every future release: verify required notices, matched shaders/fonts, selected player versions, package allowlists and per-file hashes; run from an extracted directory; exercise the optional editor against the packaged runtime; keep final archive hashes outside payloads to avoid self-referential hashing. Exclude `.data`, `.langmake`, credentials and unrelated games even when packaging an installation someone has used.

## 13. Documentation and prompt maintenance

Documentation authority/order:

1. The human's current instruction and applicable AGENTS.md.
2. Current source, `capabilities`, schemas and generated script declarations for implemented contracts.
3. Current working guides and feature qualification for operational instructions and evidence.
4. Older release records for historical facts; architecture/plans for rationale or future direction only.

Useful source index:

| Read | For |
| --- | --- |
| [08 agent API](08-agent-api.md) | Native commands, human-directed verification, inspection/input/capture semantics |
| [13 runtime guide](13-runtime-guide.md) | Gameplay, modules, scenes, saves, UI, models, physics and audio |
| [10 text assets](10-text-assets.md) | Sprites, solids, models and score recipes |
| [15 game brief](15-game-creation-brief.md), [prompts](../prompts/README.md) | Starting a game with another agent |
| [17 Studio](17-studio.md), [module README](../studio/README.md) | Providers, project/chat workflow, setup and testing |
| [21 Android](21-android-agent-guide.md), [23 iOS](23-ios-agent-guide.md) | Mandatory platform authoring/deployment instructions |
| [26 workflow](26-game-workflow.md) | Targets, profiles, held controls, identity, scenarios, receipts and recovery |
| [28 macOS](28-macos-guide.md) | Native Mac local testing/export boundary |
| [30 music](30-music-and-sound-guide.md), [31 qualification](31-audio-qualification.md) | Audio feature 1 and human listening status |
| [32 sprite rotation](32-sprite-rotation-correction.md) | Recent rendering correction and its evidence boundary |
| [33 editor plan](33-ui-editor-plan.md), [34 editor guide](34-ui-editor-guide.md), [35 qualification](35-ui-editor-qualification.md) | Optional layout editor and native review contract |
| [01 foundations](01-foundations.md), [02 architecture](02-product-and-architecture.md), [03 platforms](03-platforms.md) | Long-term rationale; not a support matrix |

When changing commands, schemas, capabilities, templates, saves, platform support or human/agent workflow, review the affected prompt examples in the same change and record version/date per `prompts/README.md`. UI changes particularly affect `prompts/13-ui-layout.md`; audio affects `prompts/12-music-and-sound.md`. Update `scripts/workflow/workflow.py` context discovery, Studio's documentation tool, package aliases and packaged instructions when adding required entry points.

Keep documentation technically honest: modern audio is implemented but its early compositions were rejected; older successful mobile interaction is not evidence for every newer native change; a fake-provider integration test is not a subscribed-model test; an editor screenshot is not human approval. This separation is central to how Langmake is developed.

## 14. Next-step triage

No new feature priority is authorized by this handoff. When the human supplies the next request:

- Identify the owning layer: runtime/gameplay, optional authoring facade, Studio/provider orchestration, editor, or platform export. Keep changes in the smallest appropriate layer.
- Inspect the existing diff before editing shared files. Preserve the uncommitted feature work and personal games.
- Reproduce an observed problem with a bounded native or Studio test. Avoid broad refactors based solely on speculation.
- Define observable contracts first: source/schema, revisions, persistence, failure handling and evidence. Shared semantics must remain consistent across headless/capture/player paths.
- Run the appropriate checks and fix integration defects before claiming readiness. Qualify a target build/device when its native behavior changed.
- Give the human a concrete playable/editor milestone and focused feedback prompts. Human feedback determines the next creative iteration.

Immediate opportunities for human review are the new editor's selection/drag/resize clarity, useful property coverage, and whether another agent follows exported screenshot notes accurately. Broader future work might include provider-specific effort controls, stronger Studio platform integration, richer layout editing, improved orchestral authoring/performance or new engine/platform systems. Each needs its own explicit scope and evidence; none should be silently inferred as the next task.

## Subsequent Studio update — September 22, 2026

The earlier completion/package evidence above remains historical. A later human-authorized Studio usability iteration is recorded in [37-studio-usability.md](37-studio-usability.md). Use its studio2 launch instructions for the updated chat, local-model setup and editor discovery; do not treat the old root dist/Studio or ui1 packages as automatically updated.
