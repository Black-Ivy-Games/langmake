# iOS/iPadOS agent workflow and documentation index

For sampled music or recorded sound effects, also read [the music and sound guide](30-music-and-sound-guide.md). Bake on the authoring host, then include only recipes/recordings in the app; build the current player source on the Mac. Sample banks, Python and sfizz stay out of the iOS app. Check [audio qualification](31-audio-qualification.md) separately from historical iPhone/iPad evidence.

For 0.5.0 authoring, start with `langmake context --project GAME` and [the per-game workflow](26-game-workflow.md). Explicitly configure this game's authorized Mac/SSH reference, signing team and device; templates and new games do not inherit another game's settings. Application icons/versions, presentation profiles, held custom controls and persistent deployment/signing operations are supported. Use the native tools below independently of Studio when needed. The historical platform evidence in the next paragraph is not automatic qualification of new 0.5.0 behavior.

Historical source qualification on the Langmake 0.4.1 baseline, recorded September 20, 2026: **The signed adventure runs on an iPhone 17 Pro Max, with nine capture/state checks and positive initial human feedback.** The human confirmed movement, interaction, music and readable text. At that milestone, physical persistence/lifecycle checks and final release packaging were pending; broader coverage came from 2D/3D simulator scenarios. Read the [qualification record](24-ios-qualification.md) before promising coverage for a particular game or device.

The native app targets both iPhone and iPad. That configuration and the shared iPadOS deployment path do not establish a physical iPad test result. See the [current workflow qualification](27-workflow-qualification.md) for newer recorded checks; keep its iPhone, iPad, simulator and human-feedback entries separate.

## Read in this order

1. The installation's `AGENTS.md` and [human-directed workflow](08-agent-api.md). The human is the decision-making developer and primary gameplay playtester.
2. This guide and the [iOS qualification record](24-ios-qualification.md), including missing prerequisites and known limits.
3. The [standalone iOS tool reference](../scripts/ios/README.md). Use its actual commands and returned artifact paths; Android and desktop launch flags are not interchangeable with Apple's deployment tools.
4. The [runtime authoring guide](13-runtime-guide.md) and [text asset guide](10-text-assets.md), plus executable `capabilities`, `schema`, `script-api` and relevant asset schemas.
5. The [mobile touch and UI authoring contract](18-android.md) for shared `input.touch`, named actions, density-independent units and persistent gameplay state. Android SDK/ADB commands apply only to Android.
6. Optional [Studio documentation](17-studio.md) when using that module; external directory/CLI agents remain fully supported.

The [implementation plan](22-ios-plan.md) explains the engineering and acceptance gates. The [iPhone/iPad prompt example](../prompts/11-ios-game.md) is an adaptable brief, not proof that its proposed game has been run.

## Establish the target before building

Record the game directory, stable `project_id`, unique Apple bundle identifier, display name, orientation, configuration, SDK destination and architecture. Initial settings are iOS 17+, Metal, arm64 devices, and arm64 or x86_64 simulators. Match simulator architecture to the Mac/runtime. Do not silently select the first connected device or use an unrelated app's identifier.

Use `devices --destination device` to identify physical hardware. Xcode 27 can include simulators in raw `devicectl` output with a paired status. The Langmake tool normalizes legacy and modern records, exposes `reality`, `pairing_state` and `reachable` separately, and lists excluded simulated/unknown targets with a reason. A paired name alone is not proof of a connected phone. Physical operations check fresh classification and reject simulators or unverified hardware. Select a returned physical iOS/iPadOS ID and verify its model with the human when needed.

Native iOS compilation and deployment require an authorized Mac workspace, full Xcode with a usable selected developer directory, the requested SDK/runtime, Python, CMake and Git. Device builds also need a signing team/provisioning configuration and a paired, unlocked iPhone or iPad with Developer Mode enabled. A simulator does not replace signing/device checks. The tools must report missing prerequisites; an agent must not invent Apple credentials or claim a Windows machine can compile against Xcode.

After an Xcode installation or update, the human must finish its license and first-launch setup. `doctor` checks `xcodebuild -checkFirstLaunchStatus`; an empty response alone does not mean success, because its exit status matters. SDK availability permits compilation but does not establish an installed simulator runtime. Check `simulator-list` separately and have the human install the needed iOS runtime through Xcode when the list is empty. Project-local CMake may be added to the command's `PATH`; no global installation is required.

Stay inside the workspace authorized by the human. A Windows game directory does not authorize access to an arbitrary Mac. Obtain an explicit host/connection and destination directory before transferring files or running commands remotely. Historical qualification hardware and paths are evidence, not defaults for a new game. The optional per-game workflow facade can use an explicitly configured SSH target; the native iOS tools themselves run locally on the selected Mac. Neither discovers or selects a remote host automatically.

## Author and verify the shared game

Start a new game with `langmake new`, or inspect the existing project. Preserve its registered modules, scenes, definitions and assets. Use declared engine state for progression and migrations for save-affecting changes. Keep native recipe assets and bundled Luau within the registered source graph; exports exclude chats, credentials, caches and unregistered files.

Use named actions for game controls and an authored mobile touch layout. Specify phone UI dimensions/fonts in `dp`, keep important controls within the reported safe area, and avoid mouse-only interactions. Custom RML controls now bind directly with `<button id="fire" data-action="fire" data-hit-padding="10">Fire</button>`. They support holding and cancellation independently of styling; use `:active` appearance and verify simultaneous movement/holding on hardware. Existing click/change events remain available. See [the workflow contract](26-game-workflow.md) for release/cancel, focus and transparent hit regions.

On a qualified Windows host, run `validate`, `check`, bounded `simulate`, and exact-tick captures with sidecars. These establish shared game behavior but do not prove iOS pixels, touch, audio or lifecycle behavior. Keep verification sessions separate from any human play session.

## iPad selection and tablet presentation

An iPad uses the same `platform:"ios"`, `destination:"device"`, `abi:"arm64"` target and iPhoneOS SDK as an iPhone. Select the actual iPad CoreDevice ID from fresh `devices --destination device` output; do not reuse the disconnected iPhone's ID or provisioning assumption. Add an explicit target for this game, such as `tablet`, with `profile:"tablet"`. Pair/trust and unlock that device, enable Developer Mode, and ensure the selected signing profile includes it. Device registration remains an explicit authorized action through `--device ... --allow-provisioning-updates --register-device`.

Choose an authored tablet profile explicitly: `ios.py stage --project GAME --profile tablet ...`, or configure the per-game target's profile before `langmake deploy --project GAME --target tablet`. Profiles are resolved during cooking; the runtime does not automatically switch from phone to tablet files based on device model. Use one shared gameplay source, adapt touch reach, font sizes and framing intentionally, then inspect actual `viewport` and `ui_renderer.safe_area` observations. Run `ui-check` using those physical dimensions/density and review a native iPad capture. A nominal screen size is not evidence of the available safe area.

The current Xcode target declares device families `1,2`, and the bundle supplies both ordinary and `~ipad` orientation arrays. Landscape supports both landscape directions; portrait supports upright portrait; sensor supports those three orientations. Upside-down portrait is not currently requested. The bundle also declares `UIRequiresFullScreen`; Apple has deprecated that key, so do not infer full-screen enforcement or qualified Stage Manager/Split View behavior from it. Windowed multitasking, orientation changes, pointer/keyboard accessories and every iPad size need their own evidence. See [Apple's device-family contract](https://developer.apple.com/library/archive/documentation/General/Reference/InfoPlistKeyReference/Articles/iPhoneOSKeys.html#//apple_ref/doc/uid/TP40009252-SW11) and [full-screen migration guidance](https://developer.apple.com/documentation/technotes/tn3192-migrating-your-app-from-the-deprecated-uirequiresfullscreen-key).

Test simultaneous movement/held actions, release/drag-out, readable HUD, sound, background/resume and save recovery on the selected iPad. When the human takes over, protect its target's play session. An iPhone result does not qualify iPad layout or ergonomics; host layout checks do not qualify physical input.

## Wireless iPhone/iPad deployment

After pairing and trusting the device with the authorized Mac through Xcode, it can use Xcode's network connection on the same reachable local network. Apple's current instructions require a cable for initial pairing on iOS/iPadOS versions earlier than 27; cable-free first pairing depends on newer OS/Xcode support. Follow [Apple's device-pairing instructions](https://developer.apple.com/documentation/xcode/pairing-your-devices-with-your-mac), including its network requirements. Keep Developer Mode enabled, and keep the device awake and unlocked when installing or launching. A paired but unreachable or locked device is not ready for a launch.

Refresh `ios.py devices --destination device`, then use the explicit returned ID in the same per-game target and normal install/launch commands. `devicectl` chooses the available transport; Langmake has no `--wireless` flag and does not discover another Mac or bypass pairing/signing. The selected Mac still compiles and signs the app. Network isolation, filtering or a changed connection can make a paired device unavailable; diagnose the returned error rather than selecting a different device implicitly.

The current qualification run installed the game and completed six capture/state comparisons on a ninth-generation iPad (`iPad12,2`, iPadOS 26.6.1) while Xcode reported `localNetwork` transport. This establishes that recorded connection and scenario coverage, not every Wi-Fi network, multitasking case or human input behavior. See [current qualification](27-workflow-qualification.md) for artifact and test details.

## Build and device workflow

There are three useful layouts. The source checkout uses `build/langmake.exe` on Windows and `scripts/ios/ios.py`. A Windows runtime package uses its own `langmake.exe`, `scripts/ios/ios.py` and `ios-player/runtime/` staging resources; that last folder is not a compiled Apple player. The current source handoff contains native engine sources, pinned dependency metadata and eight starters, including `workflow-2d`. Prepared stages under `ios-examples/` are included only when the source package was built with that option; check its manifest. Otherwise transfer your freshly verified stage and substitute its path in the Mac commands below. The September 19 archive's seven-stage count is historical. Extract the selected current archive into the Mac workspace authorized by the human. CMake fetches the pinned dependency sources inside that workspace; Xcode and its SDKs remain external prerequisites.

From this Windows source checkout, a new stage is prepared with:

```powershell
python scripts/ios/ios.py doctor
python scripts/ios/ios.py stage --project examples/rockets-2d --bundle-id org.example.starpatrol --label "Star Patrol" --orientation landscape --configuration debug --host-build build --output .cache/ios/star-patrol-stage
python scripts/ios/ios.py verify-stage --staged .cache/ios/star-patrol-stage
```

The doctor deliberately returns `ok:false`/a nonzero exit on Windows because it cannot build an iPhone app; its `can_stage:true` allows the second command. The stage output must be new. In a Windows runtime package use `--host-build .`; game and output paths must remain inside the workspace containing the tool. Do not run repeated staging commands against the same output.

On the authorized Mac, run these from the extracted source-handoff root. Use current source/tooling: the September 19 handoff predates the actual Mac integration fixes. The prepared example needs no Mac host runtime executable; the build tool builds its native host shader compiler automatically with a deployment target matching the running macOS. For an Apple Silicon simulator:

```sh
python3 scripts/ios/ios.py doctor
python3 scripts/ios/ios.py verify-stage --staged ios-examples/rockets-2d
python3 scripts/ios/ios.py build --staged ios-examples/rockets-2d --destination simulator --arch arm64 --output dist/ios/star-patrol-simulator
python3 scripts/ios/ios.py simulator-list
```

Use `--arch x86_64` for an Intel Mac simulator; that architecture is an implementation target and has not been exercised by the current arm64 simulator run. Choose installed device-type/runtime IDs from `simulator-list`, create/boot a workspace-isolated simulator using the [tool reference](../scripts/ios/README.md), then install the build report's `.app` path on that explicit simulator ID. The product is now `LangmakePlayer.app`, with executable `LangmakePlayer`; use the returned path rather than the earlier `langmake.app` name, which collided with the bundled `langmake/` content directory. This is separate from the human's default simulator device set.

For a real iPhone or iPad, replace `YOURTEAMID` with the 10-character signing team from the configured Xcode account, then choose the explicit device ID returned by `devices`:

```sh
python3 scripts/ios/ios.py build --staged ios-examples/rockets-2d --destination device --arch arm64 --team-id YOURTEAMID --output dist/ios/star-patrol-phone
python3 scripts/ios/ios.py devices --destination device
python3 scripts/ios/ios.py install --destination device --device DEVICE_ID --app APP_PATH_FROM_BUILD_REPORT
python3 scripts/ios/ios.py launch --destination device --device DEVICE_ID --bundle-id org.langmake.iospreview.gamerockets2d
```

The generic build above works when the signing profile is already configured. For the first phone, use the explicit provisioning path below after selecting the phone from `devices`. Replace the placeholders with that returned CoreDevice `id` and the chosen team; do not substitute a simulator ID or guess a hardware UDID.

```sh
python3 scripts/ios/ios.py devices --destination device
python3 scripts/ios/ios.py build --staged ios-examples/adventure --destination device --arch arm64 --team-id YOURTEAMID --device DEVICE_ID --allow-provisioning-updates --register-device --output dist/ios/adventure-phone
```

Build's optional `--device` refreshes physical-device classification, resolves its hardware UDID internally and passes that destination to an Xcode **scheme** build. A target-only Xcode invocation does not apply the destination correctly. `--allow-provisioning-updates` permits profile updates through the configured Apple account. Add `--register-device` when registering the selected phone with the chosen team is within the authorized task; it requires both `--device` and `--allow-provisioning-updates`. Simulator builds reject these flags. For an already registered phone, explicit `--device` can be used without registration. Use a new output directory for each exported iteration.

Finish the actual Apple prerequisites separately: Xcode's first-launch license/setup is distinct from the Apple account/Program License Agreement (PLA) used for provisioning. The human accepts any required agreement and uses Xcode's **Manage Certificates** for the selected account/team to create or select an **Apple Development** certificate. Initial signing may require a local Keychain approval for `codesign`; a remote `User interaction not allowed` error is not fixed by compiling again. Preserve approvals already given. Passwords and private keys stay on the Mac and must not be sent in chat or placed in project files. Successful unsigned compilation does not produce an installable phone app. Initial export requires a signed `.app` for local installation; IPA export, TestFlight and App Store submission remain separate distribution work.

Use `scripts/ios/ios.py --help` and the [tool reference](../scripts/ios/README.md) for the authoritative, versioned argument contract. Stage content on Windows or the Mac, then build on the authorized Mac. Preserve the generated integrity manifest and build report with the source revision. A staged folder is not an installed app, and a successful native build is not a successful device launch.

Verify the actual native result after a bounded launch: expected simulation tick, capture paths, revision and session must agree. A successful Apple launch response alone cannot prove the app received its requested timeline/frame arguments. The first phone run exposed a wrapper defect where an extra `--` before app flags silently started ordinary play. In direct `devicectl` calls, native flags must follow the bundle identifier without that extra token; use the corrected wrapper and its device evidence for future runs.

Use explicit simulator/device identifiers and the build report's app path for installation. Development builds allow native diagnostic commands; release builds reject developer-only arguments. Apple signing/export configuration is separate from game content. Do not submit to TestFlight or the App Store without explicit publication authorization.

For technical verification, collect a fresh native observation from the launched session, then use session-bound commands and fixed-tick captures according to the tool reference. Distinguish request delivery from its receipt; do not repeat a mutation merely because a receipt is delayed. Ordinary mutation/snapshot receipts identify their session and tick. A successful capture receipt instead points to its image; read the matching `.png.json` sidecar for session, tick, camera and state. Compare native loaded `source_revision` with the build report's `packaged_revision`, since cooking can normalize project sources. Store screenshots and matching sidecars at new evidence paths. Inspect tick zero, input boundaries and the final tick against headless results.

Test 2D and 3D rendering, readable UI over transparent assets, safe areas/orientation, simultaneous touch controls, audio, background/resume, process restart and save recovery. Test updating the same bundle/project identity without deleting its data. A corrupt/incompatible save must preserve diagnostics and avoid silently overwriting the original. Report simulator and physical-device evidence separately. The confirmed simulator background/autosave checks do not establish phone lifecycle behavior, real multitouch, audible phone output, silent-switch behavior or every interruption case. Let the human handle system permission prompts; record their outcome rather than assuming an audio initialization failure has one universal cause.

Automatic recovery restores `saves/_ios_resume.json`. Explicitly saving `progress` writes `saves/progress.json`; a later ordinary launch does not automatically select that manual slot. To verify it, wait for its save receipt, relaunch the test app, inspect the new session, then send `load` for `progress` and check the load receipt/state. Test automatic background recovery separately. Rejected recovery is preserved as `_ios_resume.rejected.json` with its diagnostic; failure to preserve it prevents automatic overwriting.

For automatic recovery, compare the actual background autosave with a paused baseline, then verify progression, entity identity and stored scenes after ordinary process restart. A restored game resumes simulation before a later pause command arrives. In the recorded simulator check it advanced 28 ticks; claiming an identical frozen tick would misrepresent that result.

## Human handoff

Provide the exact exported artifact, bundle identifier, selected presentation profile, build configuration, installed destination, launch command, controls, goal, changes and known issues. State the tested Mac/Xcode/iOS or iPadOS/device versions, relevant performance results, and which tests have not run. Ask the human to assess control feel, UI readability and sound level on that iPhone or iPad. Human feedback, not an agent's technical pass, establishes creative acceptance.

If Apple prerequisites are unavailable, finish independently verifiable content/tooling work and hand over its exact status and next commands. Do not call an uncompiled or uninstalled iOS port ready for human playtesting.
