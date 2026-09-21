# Langmake — an agent-first game engine

Langmake is an independent C++20 runtime with Luau gameplay and JSON projects. External coding agents create games through files and commands. A native window lets the human developer play the result. The engine does not own conversation, inference, credentials or model selection; games run without an LLM account.

**Status: 0.5.0 workflow alpha with optional Studio.** New [game workflow tools](docs/26-game-workflow.md) add per-game target setup, shared presentation profiles, styled held controls, application icons/versions, resumable iOS signing, named verification scenarios and portable handoff records. The runtime retains live inspection, fixed-tick captures, UI, scenes/saves, spawning, navigation, animated models and shared live/offline audio. The separate [Studio application](docs/17-studio.md) adds projects, chats and provider adapters. External Codex, Claude and other coding agents use the same runtime and game folders directly. Games require neither Studio nor an AI account.

Start a game with `build/langmake.exe context --project GAME` and the [workflow guide](docs/26-game-workflow.md). No Mac, SSH, signing team or phone is preconfigured: targets are explicitly selected for each game/machine. Python supports the optional authoring/export facade; players run independently. The [0.5.0 evidence](docs/releases/0.5.0-evidence.json) is separate from historical platform qualification.

For a previously deployed mobile target, `build/langmake.exe session launch --project GAME --target NAME` reopens the matching installed artifact without rebuilding. The workflow guide covers debug verification sessions, compact native receipts and the separate human-session guard. Current source/profile, runtime and target settings must match the deployment record.

The [Android runtime and export tools](docs/18-android.md) build standalone game APKs and AABs, with authored touch controls, screen-safe UI, orientation selection and lifecycle recovery. The native player has run on a Samsung SM-A146U (Android 15) and an Android 16 emulator. See [0.4.1 Android qualification](docs/20-android-qualification.md) for exact checks, packaged artifacts and limits; the [0.4 Windows/Studio record](docs/19-release-evidence.md) remains preserved. Previous 0.3 and 0.4 distributions are retained.

**Creating an Android game with an AI agent? Start with the [Android agent instructions and documentation index](docs/21-android-agent-guide.md).** The same guide ships as `ANDROID-AGENT-GUIDE.md` in runtime-only and Studio packages.

**iOS/iPadOS deployment supports explicitly configured Macs and signed iPhone/iPad apps.** The 0.5.0 workflow starter has passed native Metal capture/state scenarios on an iPhone 17 Pro Max and a ninth-generation iPad, including network deployment to the paired iPad. Read the [iOS agent instructions](docs/23-ios-agent-guide.md) and [current workflow qualification](docs/27-workflow-qualification.md) for exact device, update, human-feedback and distribution evidence. The [earlier iOS qualification record](docs/24-ios-qualification.md) preserves the adventure's positive iPhone feedback and simulator checks; those historical results do not qualify every new game or iPad behavior.

## Human-directed development

Langmake aims to dramatically reduce the time and effort a human needs to create the game they choose. **The human is the decision-making developer and primary gameplay playtester. AI agents are worker developers who turn that direction into working games.** The human owns the creative vision, priorities and acceptance of the result.

The normal loop is: human direction → agent implementation and technical checks → human playtesting and feedback → the next iteration. Agents should use Langmake's native validation, simulation, state inspection and captures to catch defects, then provide a playable milestone with clear controls and a few focused feedback prompts. Automated checks do not establish whether a game feels right, is enjoyable or fulfills the human's vision.

Computer-use gameplay is supplementary when requested or needed for a focused input/UI investigation; it is not the default replacement for the human playing. Agents should work autonomously within the authorized iteration, preserve an active human play session, and report technical verification separately from human feedback. **Before creating a game, read the [human-directed workflow in the agent guide](docs/08-agent-api.md#human-directed-development-and-playtesting).**

## Build and try a reference game

For the desktop UI, use the [Studio launch guide](studio/README.md). Studio is a separate module and distribution; the runtime-only package remains available for terminal and external-agent workflows. Provider accounts are connected by the human through supported login/API routes. A consumer chat subscription is not a universal API credential.

The prepared distributions can be opened directly from this workspace:

```powershell
& ".\dist\Langmake-Studio-0.5.0\Langmake Studio.exe"
.\dist\Langmake-0.5.0\langmake.exe play --project .\dist\Langmake-0.5.0\examples\adventure
```

In Studio, configure **Providers & runtime**, then use **New** or **Open folder**. The runtime is included and discovered automatically. Connect your own provider before asking its agent to edit a game.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
.\build\langmake.exe play --project examples/adventure
.\build\langmake.exe play --project examples/strategy
.\build\langmake.exe play --project examples/shared-2d
```

Keep the executable's adjacent `shaders/` and `fonts/` directories. Source builds use CMake, Ninja, Git and MinGW g++ already on PATH. Python runs integration tests; objdump checks package dependencies. Pinned downloads stay in `.deps/`, builds and distributions in `build/` and `dist/`, and temporary files in `.cache/`. No global tool installation is performed. `scripts/build.ps1 -Jobs 4` reduces concurrency.

| Reference | What it demonstrates | Human controls |
| --- | --- | --- |
| [Signal Orchard](examples/adventure/README.md) | Two-area adventure, branching dialogue/rewards, equipment, inventory/quests, animated combat, cinematic camera/actor tracks, save/load | WASD move; E interact; 1/2 dialogue; Space strike; F6 save; F9 load after saving |
| [Mosslight Assembly](examples/strategy/README.md) | 128 animated followers, selection/formations/follow/recall, obstacle navigation, tasks and spawning | WASD move; 1/2/3 select all/halves; C gather; V return; B follow; J recall; N add; H dismiss; F6/F9 save/load |
| [Lantern Post](examples/shared-2d/README.md) | Two-area 2D game using the same inventory, dialogue, quest, HUD and save modules | WASD move; E interact; 1/2 dialogue; F6 save; F9 load |
| [Glow Garden](examples/workflow-2d/README.md) | Shared desktop/phone/tablet presentation, custom held LIGHT, icons, tuning data, declared saves and two named scenarios | WASD move; hold Space/LIGHT; P pause; F6/F9 save/load; phone movement stick |

These contain original text-authored art/music and editable Luau building blocks. They are small technical reference games; their creative quality awaits human playtesting. The original `2d`, `3d`, `rockets-2d` and `rockets-3d` starters remain available.

`play` starts without the developer overlay, source reloads or developer shortcuts. `preview` supports those development tools: P pause, R reset, F5 reload, F2 overlay, F12 capture, right mouse orbit and wheel zoom. Escape closes the window when a UI control has not consumed it. Game-defined actions remain available in player mode.

## Create and verify with an agent

Start with the [prompt library](prompts/README.md) for a fill-in structure and concrete 2D, 3D adventure and group-strategy briefs. It also covers human feedback, bug reports, asset requests and planning without implementation. Humans and agents can adapt these examples to their own ideas; they are not additional bundled games.

```powershell
.\build\langmake.exe capabilities
.\build\langmake.exe schema
.\build\langmake.exe script-api
.\build\langmake.exe new --template adventure --output .cache/my-adventure
.\build\langmake.exe validate --project .cache/my-adventure
.\build\langmake.exe check --project .cache/my-adventure
.\build\langmake.exe simulate --project examples/adventure --steps 600 `
    --input examples/adventure/verification-input.json --output .cache/adventure-state.json
.\build\langmake.exe capture --project examples/adventure --steps 600 `
    --input examples/adventure/verification-input.json --ticks 0,300,600 `
    --output-dir .cache/adventure-captures --no-overlay
.\build\langmake.exe play --project .cache/my-adventure
```

Use a new destination for `new`. The three new reference templates use schema version 2; version 1 remains supported. Source registries make modules, scene files, UI, definitions and imported asset dependencies part of revisions, reloads, copies and checkpoints.

The [agent API](docs/08-agent-api.md) explains commands, live filtering, compact acknowledgments, timeline semantics and recovery. The [runtime guide](docs/13-runtime-guide.md) describes implemented gameplay systems, authorable models, imports, UI and audio. The [asset guide](docs/10-text-assets.md) covers sprites, procedural solids and musical scores. Discover exact machine-readable limits with `capabilities`, `schema`, `script-api` and `asset schema --type model`.

## Verify and package

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -ExecutionPolicy Bypass -File scripts/test.ps1 -Graphics
python scripts/create_gameplay_examples.py --verify --graphics --engine build/langmake.exe
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
python scripts/package_smoke.py
```

Graphics checks open temporary native windows. Reference verification checks fixed-input completion, repeatable observable state, inventory/equipment/rewards, scene/save continuation, actor tracks, 128 followers and capture/state parity. It records evidence under `.cache/gameplay-reference-verification/`. Running `create_gameplay_examples.py` **without** `--verify` regenerates the three engine-owned reference projects; do not use it to edit your own game.

Packaging includes the executable, runtime data, eight starters, workflow/platform tools, guides, schemas, script declarations and license notices. See the release evidence for actual build/package checks and artifact identity. A clean-machine launch remains a separate portability qualification.

## Scope and direction

The stack uses bgfx/SDL3 for Windows rendering/input, Flecs for components, Box2D/Jolt for physics, Luau for behavior, Manifold for solid modeling, ozz-animation/cgltf for animation/import, Recast/Detour for navigation, RmlUi/FreeType for UI and miniaudio for playback. Original sprites, solid parts, character joints/clips and synthesized scores can all be authored as text without another creative application.

This is a foundation for larger iterative games, with concrete smaller references. It does not establish readiness for an Ocarina of Time- or Pikmin-sized production. Current boundaries include one active scene, axis-aligned primitive colliders, ground navigation, CPU skinning and simple base-color materials. There is no multiplayer, streaming world, automatic retargeting, IK, morph animation, recorded instruments or vocals. Console SDK access, approvals and device qualification remain separate prerequisites. Every future platform needs its own evidence.

| Document | Purpose |
| --- | --- |
| [Agent API](docs/08-agent-api.md) | Human workflow, commands, inputs and live observations |
| [Runtime guide](docs/13-runtime-guide.md) | Modules, scenes/saves, entities, controllers, UI, animation, navigation and audio |
| [Game-creation brief](docs/15-game-creation-brief.md) | Give another agent the workflow and start your next game |
| [Prompt library](prompts/README.md) | Reusable structures and filled examples for humans and agents |
| [Release evidence](docs/14-release-evidence.md) | Executed 0.3 checks and qualification limits |
| [0.4.1 Android qualification](docs/20-android-qualification.md) | Actual native, phone, emulator and portable export evidence |
| [0.4 verification](docs/19-release-evidence.md) | Preserved Windows and Studio qualification |
| [Studio](docs/17-studio.md) | Optional project/chat application and external-agent handoff |
| [Android](docs/18-android.md) | SDK setup, exports, touch controls and device workflow |
| [iOS agent workflow](docs/23-ios-agent-guide.md), [qualification](docs/24-ios-qualification.md) | Apple prerequisites, implementation status and explicit test evidence |
| [0.4 implementation checklist](docs/16-studio-android-plan.md) | Completed work and remaining qualification |
| [Text assets](docs/10-text-assets.md) | Native sprite, solid mesh, model and score authoring |
| [Historical 0.2 evidence](docs/11-assets-validation.md) | Preserved asset-release results |
| [Historical 0.1 evidence](docs/09-validation.md) | Preserved first-release results |
| [Foundations](docs/01-foundations.md), [architecture](docs/02-product-and-architecture.md), [platforms](docs/03-platforms.md) | Design rationale and future direction |

Original Langmake code and example content use the [MIT license](LICENSE). Third-party terms and exact revisions are recorded in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [dependencies.lock.json](dependencies.lock.json) and `licenses/`.
