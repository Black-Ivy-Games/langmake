# One game, explicit targets, repeatable iterations

Langmake 0.5.0 adds an optional command-line authoring workflow around the independent runtime. External Codex, Claude and other directory agents remain supported. Studio can call the same tools; neither Studio nor a provider account is required to run an exported game. Python is required for the workflow/export commands, not for the player.

The human chooses the game and is its primary gameplay playtester. Agents implement the authorized iteration, verify technically and hand over a playable result. A passing scenario is not creative approval or a physical multitouch test. Read [the human-directed agreement](08-agent-api.md#human-directed-development-and-playtesting), then the platform guide for each intended target: [Android](21-android-agent-guide.md) or [iOS](23-ios-agent-guide.md). The [runtime guide](13-runtime-guide.md) and actual `capabilities`, `schema` and `script-api` remain the feature authority.

## Start and discover

Use a fresh game directory inside the authorized workspace. Source examples below use `build/langmake.exe`; in an extracted package use `langmake.exe` beside `START-HERE.txt`.

```powershell
build/langmake.exe new --template workflow-2d --output games/my-game
build/langmake.exe context --project games/my-game
build/langmake.exe target list --project games/my-game
```

`context` returns the selected runtime/version/hash, capabilities, project identity/revision, profiles, scenarios, application metadata, local target record and documentation paths. It does not probe hosts or devices unless `--target NAME` is explicit. `context --project games/my-game --target phone` and `target check --project games/my-game --name phone` check that selected target's prerequisites. Missing SSH configuration and rejected authentication are distinct errors; the tools never search unrelated keys.

Without `--target`, context resolves the project's default profile. With a target, it resolves that target's configured profile and probes its selected host. `context` describes current authored files; a deployment record describes the installed artifact. Their source/profile, engine and target fingerprints must match before mobile `verify`, `receipt` or `session launch` proceeds. An edit does not update an installed app. Run a new deployment after changing registered sources, profiles, engine or target settings; do not edit the recorded hashes to bypass a stale-artifact diagnostic. Use separate target names for different physical devices.

The [Glow Garden starter](../examples/workflow-2d/README.md) demonstrates a held custom action, a movement stick, pause/save/load, an icon, small tuning data, honest collection/delivery labels and two scenarios. Choose new stable project/application identities when cloning it. Its genre is optional; keep what serves the human's actual game.

## Configure each game independently

Portable `project.json` describes the game. `.langmake/targets.local.json` describes **this game on this machine**. It is bound to the machine and absolute game directory; moving/copying it requires explicit setup again. No template or distribution contains a Mac address, SSH key selection, device serial or signing team. Register targets using a local settings file that you write from the human's authorized choices:

```powershell
build/langmake.exe target add --project games/my-game --name pc --file games/my-game/.langmake/pc.json
build/langmake.exe target check --project games/my-game --name pc
```

Minimal Windows settings:

```json
{"platform":"windows","configuration":"debug","profile":"desktop","role":"human","host":{"transport":"local"}}
```

Android settings use an explicit serial and ABI:

```json
{"platform":"android","serial":"DEVICE_SERIAL","abi":"arm64-v8a","configuration":"debug","profile":"phone","role":"human","host":{"transport":"local"}}
```

For remote iOS builds, select a previously authorized SSH configuration reference and workspace. Replace placeholders; they are not defaults:

```json
{
  "platform":"ios", "destination":"device", "device":"COREDEVICE_ID",
  "abi":"arm64", "team_id":"TEAM_ID", "configuration":"debug", "profile":"phone", "role":"human",
  "game_directory":"games/my-game",
  "host":{"transport":"ssh","alias":"SELECTED_HOST_ALIAS","config":"PATH_TO_SELECTED_SSH_CONFIG","workspace":"PATH_TO_AUTHORIZED_MAC_WORKSPACE"}
}
```

`team_id` must be the actual ten-character Apple team ID. Physical device selection uses the CoreDevice ID reported by the iOS tools; Xcode provisioning uses its separately verified hardware UDID. `allow_provisioning_updates:true` permits Xcode account provisioning, and `register_device:true` explicitly permits registering that selected phone. Configure those only within the human's authorization. Xcode licenses, Apple account agreements and Keychain approval remain human actions. A local Mac uses `host:{"transport":"local"}`. See [the iOS tool reference](../scripts/ios/README.md) for native prerequisites.

An automated mobile test target must have `role:"verification"` and a distinct `verification_application_id`, such as `org.example.mygame.verification`; keep the human identity in `application_id`. It is a separate app, with separate storage. The tools reject a verification target that would take over the human app. Configure the target in each game's own directory rather than inheriting another game's phone/Mac record.

`target remove --project ... --name ...` removes only that local record. It does not uninstall an app or revoke host credentials. Do not put private keys, passwords or tokens in the game manifest, target record, prompt or handoff.

## Share gameplay and choose presentation

Schema version 2 supports named `profiles`. An explicit `--profile NAME` wins over `default_profile`. Each profile replaces complete top-level `input`, `ui`, `camera`, `game_camera` or `application` objects; this is not a recursive merge. Include the full replacement object. Profile resolution follows scene overrides. Entity/physics geometry is not silently widened or copied into a platform fork.

```json
{
  "default_profile":"desktop",
  "profiles":{
    "desktop":{},
    "phone":{"ui":{"documents":{"hud":"ui/phone.rml"},"styles":{"layout":"ui/phone.rcss"}}},
    "tablet":{"ui":{"documents":{"hud":"ui/tablet.rml"},"styles":{"layout":"ui/tablet.rcss"}}}
  }
}
```

Profiles participate in revision tracking, validation, cloning and cooking. Inspect/cook output records the selected profile and resolved presentation. The target's configured profile is passed through export. Keep scripts, definitions, assets and progression shared. When adapting a playfield, explicitly decide whether to preserve its mechanics or change authored geometry; changing aspect ratio is not permission to stretch sprites or collision shapes.

For styled held controls:

```xml
<button id="light-button" data-action="lamp" data-hit-padding="10"><span>LIGHT</span></button>
```

Register `lamp` in `input.actions`. `engine.input("lamp") > 0` reads holding, with `input_pressed`/`input_released` for edges. Use `:active` for pressed appearance. Padding is a numeric dp amount; it expands the hit region independently of visual opacity. A fully transparent action control still receives input. Hiding/disabling the control cancels its hold. Moving outside its hit region releases it; re-entering that same captured control restores the hold while the pointer remains down. Different fingers retain independent UI/native-stick ownership. Do not implement a requested hold as a toggle to work around appearance.

Use `dp` for phone text and hit regions, percentage anchors and a full-size body. RML/RCSS is not browser HTML/CSS. Link shared and profile stylesheets explicitly in the RML document, in that order; do not assume browser CSS imports or shorthand properties work. Use supported properties such as `background-color`. Validate layout and authored glyph coverage before native export:

```powershell
build/langmake.exe ui-check --project games/my-game --profile phone --width 1920 --height 1080 --density 2 --safe-area 72,0,1776,1032 --output games/my-game/.langmake/layout-phone.json
```

Bounds are physical pixels; density converts them to logical units. `safe-area` is `[x,y,width,height]`, not four insets. Output includes element visual/hit bounds, held actions, safe content area and diagnostics for offscreen/overlapping interactive elements or unregistered actions. Add `ui.expected_characters` for dynamic strings; `--text` checks extra text. The bundled font has documented Latin coverage, so do not assume decorative Unicode or arbitrary languages are available. `engine.viewport()` exposes runtime viewport information for explicit game-owned adaptation; graphics and simulation accept matching viewport settings. Device observations are authoritative for real density/cutouts.

## App identity, icons and versions

Declare portable installation metadata in `project.json`:

```json
{"application":{
  "id":"org.example.mygame", "label":"My Game", "version":"1.0", "build":1,
  "icon":"assets/icon.png", "orientation":"landscape",
  "platforms":{"android":{"id":"org.example.mygame.android"},"ios":{"id":"org.example.mygame.ios"}}
}}
```

Each platform override supports `id`, `icon` and `orientation`. Explicit native CLI options override resolved metadata. Keep app identity stable for updates and increment the build number intentionally. `version` uses one to three numeric components; `build` is 1..2,100,000,000. Neither changes `project_id`, `save_version` or the assessed save contract.

Icons are project-relative opaque square PNGs without interlacing: 8-bit RGB/RGBA or 1/2/4/8-bit indexed color, including native sprite exports. Supply 1024..4096 pixels for iOS, 192..4096 for Android, and 256..4096 for Windows; a 1024px shared source works for all three. Indexed palettes and optional palette alpha are supported; every color actually used in the artwork must be opaque. Grayscale/animated PNGs, malformed palettes and transparent pixels receive actionable diagnostics. Exporters use only the Python standard library. Keep original artwork; the starter includes a native sprite recipe that regenerates its PNG through `asset render`.

The iOS exporter generates an AppIcon asset catalog and bundle metadata before signing. Android generates launcher/adaptive resources; keep important artwork in the central 66/108 area because launcher masks vary. The Windows exporter adds icon/version resources to the exported executable copy, verifies them with Win32 APIs and checks that the shared engine binary remains unchanged. Reports include source/resource/artifact fingerprints and actual packaged metadata. For new application metadata, missing icons warn in debug and fail release export. Legacy projects retain their prior icon-less behavior. Store publishing is separate from sideload/portable export.

## Build, recover, deploy and hand over

```powershell
build/langmake.exe deploy --project games/my-game --target pc --stop-after build
build/langmake.exe deploy --project games/my-game --target phone
build/langmake.exe operation show --project games/my-game --id OPERATION_ID
build/langmake.exe operation resume --project games/my-game --id OPERATION_ID
```

Restart an already deployed mobile artifact without rebuilding or reinstalling:

```powershell
build/langmake.exe session launch --project games/my-game --target phone
build/langmake.exe session launch --project games/my-game --target phone-tests --development --fresh
```

The default is ordinary player mode and automatically protects the human session, including a verification app handed to the human. `--development` and `--fresh` are available only for debug targets with `role:"verification"`; a development launch does not create a human-session guard. Once the human confirms they have finished, use `session release` or explicitly add `--end-play-session` to the next launch. Source/profile, engine and target fingerprints must still match the deployed artifact. Launch is sent once and a debug app must publish a new matching session; missing acknowledgment or observation is recorded without retry. Release launch reports private inspection as unavailable. Windows uses the exported `Play.cmd` or native `play --project PATH` instead.

Each operation records prerequisite/stage/transfer/build/verification/install/launch observations and immutable attempt evidence under the game’s `.langmake/` directory. `--stop-after build|install|launch` bounds the requested work. A failed signing attempt remains failed evidence even after a later resume succeeds. The latest operation identifies its blocker, next action and already completed work. Native caches key engine/toolchain/configuration/architecture inputs rather than game content, so UI/icon changes reuse valid compilation. Every exported artifact still verifies content, revision, architecture and signature where applicable.

If iOS compilation reaches signing but Keychain access is unavailable, the native build preserves a verified candidate, Xcode-selected certificate/entitlements and an LF-safe `sign-local.sh`. Run that generated helper locally on the authorized Mac, approve the system prompt if appropriate, then resume the outer game operation. Resuming signs an attempt copy and performs final verification; it does not recompile the player. `errSecInternalComponent` is recorded evidence, not proof of one universal cause. Unsupported/incomplete signing failures remain actionable blockers rather than guessed signing commands. No passwords are requested through Langmake.

The underlying tools remain independent: `scripts/android/android.py`, `scripts/ios/ios.py` and `scripts/windows/windows.py`. Native iOS operation IDs differ from the outer workflow ID: use `ios.py operation --operation ID` or `ios.py resume --operation ID` only for that native layer. `langmake operation show --project GAME --id OPERATION_ID --full` exposes detailed workflow records when needed.

Updates reuse the same app identity/signing key and preserve available recovery state; they do not uninstall to bypass conflicts. Manual saved slots and automatic platform recovery are different. A manual-save test must explicitly load its slot after relaunch; ordinary relaunch restores the platform recovery slot. Corrupt/incompatible saves must preserve the running game and original bytes.

Human deployment and ordinary `session launch` record an active play session. When handing an already-running verification app to the human, explicitly use `session protect --project ... --target ...` too. Disruptive workflow operations refuse an active session until the human has finished, including a different app/target alias on the same device. Then use `session release --project ...`, or explicitly pass `--end-play-session` for an authorized restart. Releasing a record does not itself stop the app. Do not clear it merely to bypass human-session protection.

Windows output contains the runtime, game, required resources, licenses/notices and `Play.cmd`; the player runs without Python, Studio or an AI account. Mobile launches are native apps. Release mobile builds disable private inspection; successful launch acknowledgement alone cannot prove gameplay initialization or human acceptance.

## Declare technical scenarios

Register a scenario file in `project.json` as `"scenarios":{"smoke":"scenarios/smoke.json"}`:

```json
{
  "steps":120,
  "input":{"events":[{"from":0,"to":60,"move_x":1,"actions":{"lamp":1}}]},
  "capture_ticks":[0,60,120],
  "assertions":[{"path":"/state/held_ticks","equals":60}],
  "viewport":{"width":1280,"height":720,"density":1,"safe_area":[0,0,1280,720]},
  "tolerances":{"absolute":0.0001,"relative":0.000001},
  "save_roundtrip":true
}
```

Input can also name a project-relative JSON timeline. Assertions use JSON Pointer on the raw world snapshot: declared game state starts at `/state/`, not `/state/state/`. Conditions are `equals`, `min`, `max` and `exists`. At most 36,000 steps, 128 increasing capture ticks and 256 assertions are supported. Tick zero is initialized state; ranges are inclusive/exclusive and simultaneous actions belong in the same timeline entry.

```powershell
build/langmake.exe verify --project games/my-game --scenario smoke --profile phone
build/langmake.exe verify --project games/my-game --scenario smoke --profile phone --graphics
build/langmake.exe verify --project games/my-game --scenario smoke --target phone-tests
build/langmake.exe session launch --project games/my-game --target phone-tests --development --fresh
build/langmake.exe receipt --project games/my-game --target phone-tests --action capture --timeout 30
```

These mobile commands require a previously deployed debug verification target. A bounded scenario ends its native session, so launch a new interactive session before requesting a live receipt. The workflow passes mobile timeline paths relative to the app's private storage; do not substitute desktop paths or Android absolute paths.

A capture is complete only after its PNG and sidecar have been retrieved and checked. Native capture receipts may omit `session` and `action`; the workflow binds their unique request ID and revision to the `evidence/ID.png` path, then requires the matching sidecar's session, revision, image path and integer tick. Ordinary command receipts must carry their session and action. An uploaded request or a capture receipt alone is not verified screenshot evidence.

Device-copy acknowledgment can fail after the game consumes a request. The facade still sends the mutation exactly once and polls that same unique request ID within its receipt timeout. A matching native receipt can establish success even when the copy acknowledgment was lost; the result includes `delivery_warning` pointing to the preserved raw delivery error and records `resolved_by: verified_native_receipt`. All session/action/revision checks still apply, including the mandatory capture sidecar. If no receipt arrives, delivery remains uncertain; a read failure remains an observation error. Neither case automatically resends the mutation.

Optional recovery files may be absent. A failed read is not evidence of absence: on physical iOS devices, the native tool reports `code: file_not_found` only after a successful listing of the selected app's parent directory proves the file is missing (`evidence: successful_parent_directory_listing`). Simulator absence uses `simulator_container_lookup`. Generic CoreDevice errors, authorization failures, disconnects and failed listings remain errors. The facade accepts a verified missing file only where the operation explicitly allows absence; a malformed save or failed required-file read still blocks the operation. Failed local pulls preserve existing output files.

For a focused live control check, `receipt --input` accepts a **path to one JSON input object inside the game directory**. It does not accept inline JSON or an `events` timeline. `step` applies that same input for all requested steps and leaves the development session paused. Keep transient request files under `.langmake/`, outside the registered source graph, so preparing a control request does not change the deployed source revision:

```powershell
'{"move_x":1,"actions":{"lamp":1}}' | Set-Content -LiteralPath games/my-game/.langmake/control-input.json -Encoding utf8
build/langmake.exe session launch --project games/my-game --target phone-tests --development --fresh
build/langmake.exe receipt --project games/my-game --target phone-tests --action step --steps 60 --input games/my-game/.langmake/control-input.json --timeout 30
build/langmake.exe receipt --project games/my-game --target phone-tests --action save --slot check --timeout 30
build/langmake.exe receipt --project games/my-game --target phone-tests --action load --slot check --timeout 30
build/langmake.exe receipt --project games/my-game --target phone-tests --action capture --timeout 30
```

Use action names registered by the game; `lamp` belongs to the workflow starter. `--input` applies only to `step`. The facade bounds steps to 1..600 and receipt waits to 1..120 seconds. A timeout never causes a mutation to be sent again. Before human handoff, launch ordinary player mode with `session launch --project games/my-game --target phone-tests`; it reserves the human-session guard automatically. For a deliberately fresh handoff of this debug verification app, add `--fresh`.

Verification validates/checks source, checks UI, simulates reference ticks, evaluates assertions and optionally compares graphical sidecars. Save roundtrip restores the final save in fresh processes twice. Keep `on_load` presentation refresh idempotent and derive the same UI values from restored state. Device scenarios require a previously deployed debug verification target; they verify session/revision/request identity and use bounded receipts instead of replaying mutations when delivery is delayed. A successful upload means delivery, not execution. Full logs/states stay in files; summaries report their paths.

Evidence distinguishes `host_tested`, Windows rendering, simulator rendering, physical rendering and human/input testing. Timelines do not establish physical hold/release/cancellation comfort, sound audibility or creative acceptance. Ask the human to test those at playable milestones. The 0.5.0 [release evidence](releases/0.5.0-evidence.json) records what was actually exercised; prior [Android](20-android-qualification.md) and [iOS](24-ios-qualification.md) evidence remains historical coverage, not automatic qualification of new games or changed subsystems.

## Save compatibility and the next agent

An opt-in save declaration requires stable `project_id`:

```json
{"project_id":"my-game","save_version":1,"save_compatibility":{"id":"my-game-state-v1","allow_presentation_changes":true}}
```

This explicitly assesses compatible presentation changes while structural fingerprints still guard scenes/entities, scripts/modules, state defaults, gameplay assets/data and action names. Application icons/versions and UI-only presentation are separate from gameplay compatibility. Do not infer safety from an unchanged save version. Use `save-check --project ... --file SAVE.json` to inspect compatibility, and retain game-authored migration for changes that require it. Preserve old save bytes and verify loading migrated state twice cannot duplicate rewards.

```powershell
build/langmake.exe handoff --project games/my-game --brief brief.md --notes playtest-notes.md
```

`HANDOFF.json` records portable project/profile/application/save information, brief/feedback references, recent artifact and verification identities, pending human playtesting and the local configuration reference. It excludes SSH/device settings and credentials. `.langmake/` stores local evidence/configuration; keep it out of portable source handoff. A second agent should read the brief, handoff and `context`, use the game's explicitly configured targets, and continue the human's accepted direction without reconstructing setup from chat history.
