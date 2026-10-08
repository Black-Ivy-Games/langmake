# Agent API: files, commands, and observable state

For human UI corrections and agent review exports, read [the UI layout guide](34-ui-editor-guide.md). Before UI changes, inspect the game's `ui-design.json` when present and preserve pinned human intent unless the developer changes it.

For music or sound work, read [Music and sound: agent authoring guide](30-music-and-sound-guide.md). `music instruments`, `music schema` and `asset schema --type music` distinguish the editable composition format from native synthesized/recorded assets. Offer contrasting audible sketches and let human listening direct the musical iteration.

Langmake 0.5.0 workflow alpha · September 2026

The engine is operated through project files and a JSON-producing command line. Any external agent that can read and write files and run a process can use it. Conversation, inference, credentials, and model selection belong to that external agent; the engine has no model dependency. This guide describes the implemented alpha, not the later platform and feature roadmap.

**Start with `langmake context --project GAME` and the [game workflow guide](26-game-workflow.md).** It documents per-game targets, profiles, icons/versions, held custom controls, scenario verification, resumable exports and handoff. Local SSH/device/team settings are never inherited from another game. `context`, `target`, `deploy`, `operation`, `verify`, `receipt`, `handoff` and `session` delegate to optional Python tools; exported games remain independent of them and Studio.

**Android target:** read the [Android agent workflow and ordered reading list](21-android-agent-guide.md) before starting a game or port. It connects authoring, host checks, APK export, device verification and human handoff, including the differences from desktop inspection/capture commands.

**iOS target:** start with the [iOS agent workflow](23-ios-agent-guide.md) and [qualification record](24-ios-qualification.md). The signed adventure app runs on an iPhone 17 Pro Max; nine native Metal captures match headless state, and the human confirmed movement, interaction, music and readable text. Physical persistence/lifecycle checks and full release qualification remain pending; their broader current evidence is from the simulator. Native builds require an authorized Mac with Xcode. Use the separate iOS tools rather than guessing Android or desktop flags; their device listing distinguishes physical hardware from simulator records even when Xcode reports both as paired.

[Studio](17-studio.md) is an optional, separate authoring application over these same files and commands. External Codex/Claude sessions remain fully supported. Keep human decisions in project notes for handoff and avoid concurrent writers or disruption of a human Play session. [Android tools](18-android.md) are callable without Studio; consult the [Android qualification record](20-android-qualification.md) before claiming device coverage.

For optional brief structures and filled examples, see the [prompt library](../prompts/README.md). Humans and agents can adapt its new-game, feedback, bug, asset and plan-only prompts. Examples do not authorize work or replace the human's actual direction.

## Human-directed development and playtesting

**Read this before starting a game project.** Langmake exists to dramatically reduce the human developer's time and effort in creating a game of their choosing. The human is the decision-making developer and primary gameplay playtester. AI agents are worker developers: they implement ideas, produce assets, investigate defects, run technical checks and prepare iterations for the human to experience.

The human owns creative direction, scope, priorities and acceptance. Agents can suggest options and make routine implementation choices within the authorized work. Complete that work without requesting permission for every edit; a feedback milestone is not a reason to leave the agreed iteration unfinished. Reuse decisions already given and ask for clarification only when missing information materially affects the result.

Use this development loop:

1. **Understand the human's direction.** Establish the intended experience and the next useful playable milestone from the existing brief. Record important decisions and unresolved choices in the game's own notes.
2. **Implement the authorized iteration.** Author JSON/Luau and assets, keep changes within scope, and fix technical defects. Bring substantial creative alternatives back to the human instead of silently changing their vision.
3. **Verify technically.** Prefer `validate`, bounded `simulate` scenarios, state inspection, asset renders and PNG captures with sidecars. Check the behaviors affected by the changes; repeat or broaden checks when failures or new changes justify it. An automated win proves that a scenario can complete, not that movement, challenge, pacing, story, art or sound satisfy the human.
4. **Hand over a playable milestone.** Launch or provide the exact `play --project PATH` command for the correct project, verify that it is ready for input, and give controls, the goal, what changed and known issues. Ask a few focused questions about the experience, such as movement feel or whether a goal is clear. The human is expected to actually play and provide feedback.
5. **Iterate from that feedback.** Record what the human observed and chose, implement the next agreed changes and repeat. If the next creative decision depends on missing feedback, say that the milestone is ready and awaiting human playtesting. Continue independent authorized work when useful; do not invent feedback or declare human acceptance.

**Agent testing remains supported.** Supplementary agent gameplay tests are appropriate when the human requests them or a focused defect needs reproduction. Prefer native engine controls and reproducible inputs. Do not automatically use a computer-use skill to play through the game in place of handing it to the human. Computer use is appropriate when explicitly requested or when a specific input/window/UI behavior cannot be checked through native commands. Even a successful agent playthrough does not replace the human's evaluation.

Protect the human's play session: do not edit watched source files, reset/reload, pause, move the camera or inject controls while they play unless requested. Source edits can restart gameplay. Coordinate the next edit window or work on independent tasks. When handing over after automated stepping, resume the preview or start it in a clearly described initial state; do not silently leave it paused.

A useful handoff separates these facts:

- **Ready to play:** the project/launch command, controls and the goal for this iteration.
- **Changed and checked:** what was implemented, which technical checks passed, and known limitations.
- **Human feedback:** two or three relevant questions and whether the human has played this revision yet.

Use “ready for human playtesting” when that is the verified status. Report any gameplay approval as the human's actual feedback. Keep engine regression tests and game-design acceptance distinct.


## Start an edit–run–observe loop

Use the current executable's `capabilities`, `schema`, `script-api` and `asset schema --type TYPE` for discovery. This guide covers the current runtime; the [runtime guide](13-runtime-guide.md) describes authoring formats and gameplay bindings. Build with `scripts/build.ps1`.

~~~powershell
$lmEngine = "./build/langmake.exe"
$lmProject = ".cache/my-adventure"
& $lmEngine new --template adventure --output $lmProject
Copy-Item -LiteralPath "examples/adventure/verification-input.json" -Destination "$lmProject/verification-input.json"
& $lmEngine validate --project $lmProject
& $lmEngine check --project $lmProject
& $lmEngine inspect --project $lmProject --ids player --fields position,asset,animation
& $lmEngine simulate --project $lmProject --steps 600 --input "$lmProject/verification-input.json" --output .cache/game-state.json
& $lmEngine capture --project $lmProject --steps 600 --input "$lmProject/verification-input.json" --ticks 0,300,600 --output-dir .cache/game-captures --no-overlay
& $lmEngine play --project $lmProject
~~~

`new` requires a nonexistent destination. The eight templates are `2d`, `3d`, `rockets-2d`, `rockets-3d`, `adventure`, `strategy`, `shared-2d` and `workflow-2d`. The last demonstrates shared presentation profiles, held custom controls, application icons, tuning data, saves and registered verification scenarios. Alternatively `new --dimension 2d|3d` selects an original platform starter; do not combine the selectors. Schema-2 clones receive their own project identity and application ID; choose stable game identities before first deployment.

`new` copies the registered source graph, not unregistered test timelines. The explicit copy above supplies the bundled adventure scenario for this quickstart. Game authors should maintain and update their own input timelines as mechanics, controls and levels change.

Each completed command writes JSON to stdout. Check the exit code and `ok`; existing output files do not prove success. Schemas return JSON Schema itself. A long-running preview/play command returns its final result on exit, so inspect live state while it runs. Flags take no value; other options use `--option value`. Unknown/duplicate options are rejected. Always supply `--project`; the fallback is `examples/3d`.

## Command reference

| Command | Contract |
| --- | --- |
| `help`, `capabilities`, `schema`, `script-api` | Discovery. `script-api` includes function metadata and generated Luau declaration text. |
| `context`, `target` | Optional workflow discovery and explicit per-game target setup. `context --project GAME [--target NAME]` resolves current authored presentation; `target list/add/check/remove` manages local target choices. See the [workflow contract](26-game-workflow.md). |
| `deploy`, `operation` | Build/install/launch a configured target; inspect or resume recorded work. `operation show --project GAME --id ID --full` preserves detailed evidence. |
| `verify` | Run a registered `--scenario NAME`, with optional `--profile NAME`, Windows `--graphics`, or a deployed mobile `--target NAME`. Graphics and mobile target modes are separate runs. |
| `receipt` | Mobile native control for a previously deployed debug target. `--input FILE` is one JSON input object inside the game directory and applies only to `--action step`; it is not inline JSON or a timeline. Source/profile, runtime and target fingerprints must match the deployment. |
| `session` | `show`, `protect --target NAME`, or `release` human-session protection. `launch --target NAME` restarts the deployed mobile artifact in player mode; debug verification targets may use `--development` and/or `--fresh`. No rebuild or install occurs. |
| `handoff` | Write portable project context with optional project-relative `--brief` and `--notes` files; excludes local host/device settings. |
| `new` | `--output NEW_DIRECTORY`, optional `--template NAME` or `--dimension 2d|3d`. Copies registered source files without generated state. |
| `cook` | `--project PATH --target android\|ios\|macos\|windows --output NEW_DIRECTORY`. Validates all registered scenes and copies the complete registered source graph with revision/asset manifest. Excludes saves, caches, chats and unregistered files. Existing destinations are rejected. This is a validated source bundle; native asset recipes compile on player load. Mobile tools add SHA-256 integrity hashes and platform packaging; iOS native builds additionally require Mac/Xcode and qualification. |
| `validate` | `--project PATH`. Loads sources, compiles assets and scripts, initializes physics/UI, runs top-level code and `start()` for entry and registered scenes. Does not run updates. |
| `check` | `--project PATH`. Offline Luau checking against engine declarations for entry, scenes and registered modules; not a gameplay proof. |
| `inspect` | Authored summary, or `--live` published runtime state. Entity filters below apply to either. `--check-source` explicitly compares a live observation with source. |
| `simulate`, `profile` | `--steps N` (default 120; 0..36000), optional `--input TIMELINE.json`, `--output RESULT.json`, `--load-save FILE`, `--save-output FILE`, `--audio-output MIX.wav`. Fixed 1/60-second simulation without a window/audio device. Both return measured timing. |
| `preview` | Development window; source watch, developer shortcuts and overlay. `--frames N` (0 interactive; up to 360000), `--capture FILE.png` with positive frames, `--no-audio`, `--no-overlay`, `--width`, `--height`. |
| `play` | Player window. Overlay, source watch and developer shortcuts disabled; game input/UI and save/load remain enabled. Supports bounded frames and window options like preview. |
| `capture` | Legacy `--output IMAGE.png --frames N` (default 30), or exact-tick form below. Isolated verification run with audio disabled. |
| `control` | `--action pause|resume|reset|snapshot|step|capture|save|load`. Requires a live preview or player session; player mode permits only snapshot/capture/save/load. Step: `--steps 0..600` and optional `--input INPUT.json`. Save/load: `--slot NAME`. `--include-state` adds filtered state to an ordinary acknowledgment. |
| `save`, `load` | Live aliases with `--slot NAME` (default `quick`). These store gameplay progress, unlike source checkpoints. |
| `patch` | `--file PATCH.json`. Revision-checked authored entity changes; optional scene selection. |
| `checkpoint`, `restore` | `--name NAME`. Save/restore the complete registered authored source graph. |
| `asset schema` | `--type sprite|mesh|model|music`. |
| `asset list`, `asset inspect` | Compile and describe assets; inspect requires `--id ID`. |
| `asset build` | Optional `--id ID`. Outputs generated files under `.langmake/assets/ID/ASSET_REVISION/`. |
| `asset render`, `asset export` | `--id ID --output FILE`; optional sprite `--scale 1..8`, music `--seconds 0..120` (0 full score). Sprite PNG; mesh/model render PNG or export posed OBJ; music WAV. Model export does not retain a rig/animation bundle. |
| `asset replace` | `--id ID --file RECIPE.json --expected-revision REV`. Compiles a complete candidate recipe before atomic replacement. Uses the project revision. |

Desktop command defaults are 1280×800, width 640..3840 and height 400..2160, using Windows/D3D11 or macOS/Metal. See the [Mac guide](28-macos-guide.md) for local app packaging and its qualification limits. Android uses the device framebuffer dimensions and GLES3; its APK launch, physical input and timeline capture commands are documented in the Android guide. One interactive preview/play can own a project's live bridge. Standalone capture uses an isolated bridge and does not control that interactive session.

## Input timelines and exact-tick captures

A timeline is a JSON object with at most 10,000 `events`:

~~~json
{"events":[
  {"from":0,"to":30,"move_x":1},
  {"from":30,"to":31,"actions":{"interact":1}},
  {"from":60,"to":61,"events":[{"type":"ui","id":"choice1","event":"click","value":""}]}
]}
~~~

Ranges are inclusive `from`, exclusive `to`, within 0..36000. Overlapping events do **not** merge: the last matching object supplies the whole tick's input. Missing axes/actions are zero and missing legacy `jump`/`fire` are false. Axes/action numbers are finite -1..1; action booleans become 0/1. A normalized `pointer:[x,y]` is accepted; semantic `events` is a bounded JSON array. See the runtime guide for bindings and focus behavior.

Timeline indices are relative to the current command, including after `simulate --load-save`. Loading a save at tick 180 and applying an event with `from:0` applies it to the first resumed update; `from:102` applies after 102 resumed updates. Observed `engine.tick()` continues from the restored clock. Author a continuation timeline accordingly rather than reusing absolute saved tick numbers as input offsets.

~~~powershell
& $lmEngine capture --project $lmProject --steps 600 --input "$lmProject/verification-input.json" --ticks 0,150,300,600 --output-dir .cache/evidence --no-overlay
~~~

Tick 0 is after initialization and before updates; tick N is after N fixed updates. Capture ticks must increase, be unique, lie within `--steps` and contain at most 256 entries. Outputs are `tick-N.png` and `tick-N.png.json`. The same timeline parser drives simulation and capture. Capture holds the requested state while rendering and writing evidence, then continues; it ignores live/human input and source edits during that isolated run.

The sidecar records state, tick, scene, source and asset revisions, actual camera matrices, viewport, overlay and scenario information. Compare gameplay observations with simulation; timing/device fields naturally differ. `--steps` cannot combine with `--frames`. Legacy frames count rendering iterations, so use sidecar ticks when interpreting old captures.

`simulate`/ `profile` return `state`, `profile`, `steps`, `fixed_dt`, `source_revision` and elapsed time. State includes declared game/UI state, camera, entities, animation, navigation, contacts, audio selections/events, scene and win/message fields. Assert the expected behavior; `ok:true` does not mean the game won. Repeatability is qualified on the tested build/host, not a cross-platform floating-point lockstep guarantee.

## Lightweight live inspection

~~~powershell
& $lmEngine inspect --project $lmProject --live --ids player --fields position,velocity,animation
& $lmEngine inspect --project $lmProject --live --role worker --visible true --offset 0 --limit 32
& $lmEngine inspect --project $lmProject --live --check-source --limit 0
& $lmEngine control --project $lmProject --action snapshot --ids player --fields position
~~~

Filters: comma-separated `--ids`, `--fields`; exact `--role`; `--visible true|false`; `--offset` and `--limit 0..4096` (default 128). IDs are retained when selecting fields. Matching entities are sorted by ID. Counts and `next_offset` make pagination explicit; omitted entities are not evidence that they do not exist. `--limit 0` observes metadata without entity payloads.

Live state is normally published about every 250 ms. `fresh` means `running:true` and publication age below 2500 ms. Always inspect `age_ms`, `source_revision`, `diagnostic`, `reload_status` and `paused`. `ok:true` means the observation was read. A healthy game and a fresh file are separate facts.

The default live read does not reread the authored source graph. Add `--check-source` when confirming an edit; this adds `source_checked`, `source_valid`, `authored_revision` and `matches_source`, with a source diagnostic on failure. Loading sources is not equivalent to compiling every recipe/script: run `validate` for that.

Windows readers allow read/write/delete sharing, copy bytes promptly and release handles. The background publisher performs bounded transient retries and exposes failures, last success/error and queued receipt counts/bytes under `publication`. Snapshot replacement failure does not terminate gameplay. Stale files remain detectable; an inspection command is not a synchronous render barrier.

## Live control and recovery

~~~powershell
& $lmEngine control --project $lmProject --action pause
& $lmEngine control --project $lmProject --action reset
'{"move_x":1}' | Set-Content -Encoding ascii .cache/one-input.json
& $lmEngine control --project $lmProject --action step --steps 30 --input .cache/one-input.json --include-state --ids player
& $lmEngine control --project $lmProject --action capture
& $lmEngine control --project $lmProject --action resume
~~~

`step` applies **one input object** for the entire step count and leaves the preview paused. `reset` reloads valid authored source and restarts while preserving pause status. `snapshot` is read-only. `capture` responds after its image/sidecar is written under `.langmake/evidence/`. While resumed, ticks may advance between commands. Player mode accepts snapshot/capture/save/load, and rejects developer mutations such as step/reset.

API version 2 ordinary control receipts are compact: request/session, action, tick, source revision, success and pause status. `snapshot` includes filtered state; other commands require `--include-state`. A state payload over 8 MiB is omitted with explicit `state_omitted`; the command outcome is unchanged. Narrow the query when requesting evidence.

Capture receipts instead contain the request ID, image path and source revision; older native builds omit their session/action fields. The workflow verifies `evidence/ID.png` against that unique ID and accepts completion only after reading its sidecar and matching the session, revision, image path and tick. If a capture receipt includes session/action, those must also match. Ordinary receipts retain their strict session/action checks.

The mobile workflow sends each mutation once. A failed device-copy acknowledgment can mean the game already consumed the request, so it continues bounded observation of that same request ID. A strictly verified receipt establishes the outcome and a successful result retains `delivery_warning` with the raw delivery-error record. Missing or unreadable receipts remain uncertain or failed observations; they never trigger a mutation resend.

Optional native-file reads distinguish verified absence from transport failure. Physical iOS `file_not_found` requires a successful listing of the selected app's parent directory proving absence; a generic CoreDevice error does not qualify. The facade skips that result only when absence is explicitly allowed. Required reads and malformed saves still fail, and failed pulls preserve any existing local output.

Commands require a running publication no older than three seconds. CLI waits about ten seconds; requests expire and are session-bound. A timeout is not proof that a mutation never ran. Consult state/result evidence before submitting a new mutation. Completion IDs prevent a receipt delivery retry from executing the same request twice. Consumed receipts are removed automatically; abandoned receipts/commands are retained for bounded recovery, with ten-minute retention. The publisher queue is bounded to 1024 receipts/16 MiB and applies backpressure. Requested screenshots remain evidence, not disposable acknowledgments.

Failed source loads, asset compilation or new render/UI resource creation keep the last good world. Inspection remains available while authored files are invalid. Successful watched reload restarts gameplay, animation, audio and camera; it does not preserve progress. Runtime script errors pause preview and remain visible in diagnostics. Repair source, check the running revision, and resume explicitly if paused.

Do not perform these mutations while the human is playing unless requested. Prefer isolated captures for technical evidence and `play` for human handoff.

## Revision-safe edits, source checkpoints and saves

Revisions cover paths and raw bytes of the **complete registered graph**: manifest, entry/scenes/modules, prefab/definition JSON, asset recipes and external model/image/buffer inputs, and registered UI files. Whitespace edits count. Generated files do not. Revisions are change-detection tokens, not security hashes.

~~~json
{
  "expected_revision":"COPY_FROM_INSPECT",
  "scene":"orchard",
  "changes":[
    {"id":"player","property":"color","value":[0.3,0.85,1,1]},
    {"id":"player","property":"visual_scale","value":[1.8,1,1.8]}
  ]
}
~~~

`scene` is optional for a scene-specific patch. Allowed properties are listed in `capabilities.patch_properties`; they include authored transforms, body/shape/role, visual selection, animation, visibility, collision participation and controller. IDs cannot be renamed or created/deleted through patch; use direct source edits for those changes. Duplicate entity/property pairs are rejected. The candidate is checked before replacing the appropriate source file. On revision conflict, inspect and reconsider rather than blindly substituting the new token. Run validation and affected scenarios afterward.

`checkpoint --name NAME` saves authored sources under `.langmake/checkpoints/`; `restore` verifies source-list hashes/paths, backs up current targets and can recover invalid/missing source. Names are 1..64 ASCII letters/digits/underscore/hyphen; existing checkpoints are not overwritten. Unrelated files remain in place. Ordinary write failures are rolled back; this is not a crash-atomic multi-file transaction. External editors do not participate in the engine mutation lock.

Gameplay saves are separate. Use `save/load --slot NAME` for a live session, `engine.save_game/load_game` from game input, or simulation's `--save-output/--load-save`. Project identity, source revision and save version are checked. Changed game content requires an explicit Luau migration hook unless an opted-in, assessed presentation change passes structural compatibility checks; see the [save contract](13-runtime-guide.md#scenes-and-persistent-saves). Save files reconstruct declared systems, not arbitrary Luau locals or physics solver internals.

Source builds support scene-local replacement asset/module/prefab/definition registries. Keep each scene within its usual limits, preserve unique global state keys, and migrate the complete collection save (active and inactive scenes) when adding a game. Cook manifests include `scene_assets` in addition to the selected entry scene's `assets`. Older packaged runtimes do not accept these scene fields; see the runtime guide before distributing a collection.

## Diagnostics and execution boundaries

Errors are structured JSON, commonly `diagnostics:[{code,path,message}]`; live receipt errors may have singular `diagnostic`. Script errors include phase/file/tick/source revision and Luau source information. Codes include `SCRIPT_ERROR`, `SCRIPT_BUDGET_EXCEEDED` and `SCRIPT_MEMORY_LIMIT`. Branch on available fields instead of assuming every error has the same shape.

Luau has no filesystem, process, networking or FFI bindings. Engine/standard-library tables are read-only and `print` is disabled; use state/UI/message for observations. Registered modules use the same sandbox and budgets. Script source is bounded to 1 MiB, compiled bytecode to 8 MiB, VM allocation to 16 MiB, and each protected call has a 50 ms interrupt budget.

Schema 2 permits 256 MiB of source graph, 4 MiB per text source and 32 MiB per supported binary source; schema 1 retains its 32 MiB graph budget. There are at most 256 assets, 4096 active entities and 256 MiB compiled visual data. Asset/subsystem compilers have additional limits in their schemas. These checks do not isolate the native process or cap every native allocation; source confinement is not an operating-system sandbox.

Report what passed, what remains limited and what the human should try. Technical evidence and human creative acceptance must remain distinct.

Native material boards use contiguous channels, a bounded classic chemistry kernel, active chunks, dynamic sprite surfaces and lazy state providers. See [material grids and bound state](38-material-grid-guide.md). This feature is Windows-qualified; existing mobile qualification does not cover it.
