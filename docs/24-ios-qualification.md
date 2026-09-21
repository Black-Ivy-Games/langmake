# iOS qualification record

Updated September 20, 2026. Baseline: Langmake 0.4.1 with subsequent iOS source changes. **The signed adventure runs on an iPhone 17 Pro Max, passes nine native capture/state checks, and has positive initial human feedback. Full iOS release qualification remains unfinished.** Physical persistence/lifecycle and final packaging are still pending; their broader existing coverage below is from the simulator.

## September 20: actual Mac and simulator progress

The human authorized the Mac workspace `<MAC_WORKSPACE>`. No other Mac or directory is implied by this record.

| Environment | Recorded value |
| --- | --- |
| Mac | Apple silicon (host memory omitted for privacy) |
| macOS | 26.6.2 |
| Xcode | 27.0 (27A266a) |
| iOS SDK / simulator OS | 27.0 |
| Simulator | iPhone 16, arm64, workspace-isolated device set |
| Physical iPhone | iPhone 17 Pro Max, iOS 26.6.2, Developer Mode enabled. Signed adventure installed; nine native captures and initial human playtesting pass their stated checks. |

| Check | Actual result |
| --- | --- |
| Mac prerequisites and tools | Xcode first-launch/doctor checks pass. The updated **49 host tool tests pass on Mac and Windows**, including legacy/modern device records, physical-action guards and explicit device/provisioning build contracts. SDK and simulator-runtime availability were checked separately. |
| Host shader compiler | Native macOS `shaderc` builds. Its deployment target explicitly matches the running macOS, so a newer SDK does not silently require a newer host OS. |
| 2D native player | The rockets reference app compiles, installs and launches with Metal in the arm64 iPhone 16 simulator. |
| 2D fixed-tick replay | Seven capture sidecars match Windows headless gameplay state at ticks **0, 79, 80, 81, 149, 150 and 480**, including input boundaries and the final tick. |
| 3D adventure | The official exporter builds an arm64 debug `LangmakePlayer.app`. Nine native Metal captures at ticks **0, 35, 36, 38, 40, 150, 333, 393 and 600** match all Windows headless snapshot fields with absolute tolerance **1e-4** and relative tolerance **1e-6**. Largest observed numeric difference: **4.76837158203125e-7**. Image inspection confirms readable HUD, lit articulated characters and the expected inventory. |
| Native controls and persistence | **24 rockets requests and 43 adventure requests pass** save/mutate/restore, fresh-process load, same-bundle reinstall/load and corrupt-save rejection without changing declared state. The adventure reaches the relay victory at tick 600; full saves preserve inventory/quests, saved area state, controller/animation, camera and RNG state. |
| Audio backend | Adventure observations report **Core Audio**, initialization **ready**, `device_is_null:false`, `output_available:true` and last result **0**. The final scene reports **290,560 sample frames**, peak **0.0505917668**, and **zero clipped frames or dropped commands**. Scene changes reset mixer counters, so this is not a total across the full adventure. |
| UIKit background/recovery | Foregrounding Settings actually backgrounds the adventure. Its autosave matches all paused-baseline fields; Core Audio suspend/resume returns **0**, and same-process foreground preserves state. After a second background and process restart, recovery reports **restored** with matching progression, entity identities and stored scenes. The running game advances **28 ticks before the verification pause**; this is not a frozen-tick comparison. |
| Native phone build and signing | Arm64 build and signing succeed. After Apple's agreement, registration and profile setup, the local Mac Terminal launcher completed signing. The app is installed on the phone. |
| Phone capture/state checks | The corrected official wrapper completes **nine Metal captures** at ticks **0, 35, 36, 38, 40, 150, 333, 393 and 600**. All headless snapshot fields match within absolute **1e-4** / relative **1e-6** tolerances; maximum observed difference **4.76837158203125e-7**. The final scene is `relay`, with victory reached. |
| Human iPhone playtesting | The human confirms movement, interaction, music and readable text. This initial feedback is positive; it is not full creative approval or release qualification. |

The first audio-enabled attempt aborted in Apple's RemoteIO path. The human then approved a system audio permission prompt, and a repeated 2D scenario completed with 498,432 rendered audio frames, peak 0.430389 and no clipping. The later adventure build adds explicit backend observations, as recorded above. These results do not establish a universal cause for audio initialization failures, audible phone output or completed interruption/silent-switch checks; those still require device testing and human feedback.

The tested adventure export is `dist/ios/adventure-simulator/LangmakePlayer.app` in the authorized Mac workspace. Its executable SHA-256 is `5ae8cc7195c1597730c8e787e9f1d72dac74a38bf9bdd7c0846e359fbf3c8b80`. Verification reports, captures and persistence receipts are mirrored locally under `.cache/mac-bootstrap/evidence/adventure-verification`, `adventure-lifecycle`, `rockets-2d-lifecycle` and `adventure-background-native`.

Xcode 27 initially included a paired iPhone 18 Pro simulator in `devicectl` discovery. Its hardware reality/provider identifies it as simulated. The tool now classifies legacy and modern records, separates hardware reality from pairing/reachability, and refuses physical-device operations for simulated or unverified targets. The subsequently connected iPhone 17 Pro Max above is confirmed physical hardware; visibility alone does not qualify its game runtime.

Actual Mac integration exposed an app-bundle naming collision: an executable named `langmake` could not coexist with the `langmake/` content directory at the bundle root. The product is now **`LangmakePlayer.app`**, with executable **`LangmakePlayer`**; immutable project/runtime content retains the `langmake/` path. Follow the app path in the current build report rather than an older preview's filename.

Machine-readable simulator results and artifact hashes are recorded in [September 20 simulator evidence](releases/ios-simulator-evidence-20260920.json). These supplement the preserved Windows evidence below. The initial physical check is recorded separately in the next section. The implementation targets iOS 17+, arm64 devices and arm64/x86_64 simulators; executed coverage is limited to the named iOS 27.0 simulator and iOS 26.6.2 phone checks.

## Physical iPhone capture and human handoff

The signed executable SHA-256 is `588918812511a3e0386b46e6ba31ebd718595b02fd90a0254f928b1a4403faee`. A direct `devicectl` launch using `--frames 3 --capture` completed successfully with source revision `fnv1a64-6312bf1e46d84c31`, scene `orchard`, simulation tick 3 and render-frame count 6. Those are distinct counters. The result and visually checked PNG are preserved at `.cache/mac-bootstrap/evidence/phone-direct-no-console-result.json` and `phone-direct-no-console.png`.

The initial result reports Core Audio initialization `ready`, `device_is_null:false`, `output_available:true`, last result 0, 25,088 sample frames and zero clipped frames or dropped commands. The human subsequently confirmed the music during normal play; that audible-output evidence is attributed to their feedback rather than inferred from mixer counters.

This check exposed a wrapper defect: inserting an extra `--` before native app flags caused a bounded request to start ordinary play. The corrected wrapper passes flags directly after the bundle identifier, with exact-command regressions for ordinary, frame and timeline launches; 49 tests pass on Mac and Windows. Its full phone capture run now passes all nine checkpoints listed above. Evidence and headless comparisons are in `.cache/mac-bootstrap/evidence/physical-adventure-captures-20260920-b`; the failed first run remains preserved.

A fresh ordinary player session was then launched and inspected: `running:true`, `paused:false`, `mode:play`, `background:false`, `surface_ready:true`, Metal and Core Audio active. The human responded: **"yes it's working! movement and interact work. Music was good and the text is readable"**. The app was left available for their play session; no full creative approval is inferred. This reference still contains desktop control hints and unused generic Jump/Fire buttons; its combat action is `attack`, and further touch-layout polish can follow human feedback.

The signed artifact identity, nine capture comparisons, normal-player handoff and attributed human feedback are recorded in [September 20 phone evidence](releases/ios-phone-smoke-evidence-20260920.json). The get-game-running milestone is complete. No distribution was regenerated here, and physical persistence/lifecycle, release configuration and broader device/game coverage remain outstanding.

## Remaining qualification gates

- Verify background recovery on the physical phone and extend interruption, focus and orientation/safe-area checks. The completed UIKit simulator scenario establishes only its recorded coverage.
- Repeat native command delivery, save/load, same-identity updates and corrupt-save preservation on the physical phone.
- Extend physical touch combinations, silent-switch/interruption and frame/memory checks beyond the confirmed movement, interaction, music and text feedback.
- Verify release argument restrictions and complete the current exported-app/package integrity checks before presenting a release artifact.
- Continue creative iteration from human feedback after this playable handoff. Initial positive feedback does not establish full gameplay or release acceptance.

## September 19: preserved Windows source-preview evidence

At this earlier milestone, the available workstation was Windows (build 26200), with an AMD Ryzen 7 5800X. Neither `xcodebuild` nor `xcrun` was available locally. The human said a Mac would be available later and asked to finish work possible without it. **The following table is the historical September 19 record:** it claimed no Apple native build, simulator launch, signed installation or iPhone gameplay result. Later Mac results above do not retroactively change those checks.

| Check | Status |
| --- | --- |
| Shared native tests | All 12 CTest targets pass, including the actual iOS launch parser and committed-inbox implementation. |
| Windows graphics/live regression | Full suite passes: all starters, assets, scenes/saves, capture parity, callback lifecycle, early cleanup, 652-entity inspection, forced file contention, receipt idempotence, HUD and player isolation. |
| Content export | All seven starters pass Windows/Android/iOS cook checks: 21 cases, with source-graph privacy, revision/state parity and overwrite protection. |
| iOS staging | All seven starters pass actual native cook → staging → integrity verification on Windows. |
| iOS Python tool contracts | 32 tests pass: paths, hashes, tampering, identity, signing choices, timing modes, ASCII IDs, Unicode output, release restrictions and committed uploads. |
| Studio | 36 core/adapter tests, real Electron target/settings tests, and two actual Broker → Python → native staging runs pass. |
| Android native compilation after shared-loop changes | ARM64 debug player rebuild passes. No additional phone run was performed for this iOS task. |
| Metal shader translation | Both model shaders translate successfully using pinned host shaderc with the iOS/Metal profile. This does not test Apple's compiler or Metal execution. |
| macOS host shader compiler | Not run; Mac required |
| iOS simulator native build and Metal rendering | Not run; Mac/Xcode required |
| iPhone signing, installation and launch | Not run; Mac/Xcode/signing/device required |
| Physical touch, safe area, audio, lifecycle and save/update checks | Not run |
| Human iPhone playtesting | Awaiting an installed and technically verified build |

The intended baseline was iOS 17+, Metal, arm64 devices, and arm64/x86_64 simulators. These were implementation targets, not a statement of tested hardware coverage. See the [plan](22-ios-plan.md) for acceptance gates and the [agent guide](23-ios-agent-guide.md) for the current workflow.

Machine-readable results, evidence hashes and the tested Windows executable hash are recorded in `docs/releases/ios-source-preview-evidence.json`. Raw task results are under `.cache/ios-implementation-20260919`, `.cache/ios-native-evidence` and `studio/.cache/ios-studio-integration-report.json`. Host tests exposed and now cover abandoned-inbox starvation, mismatched Unicode digit validation and Windows JSON encoding; those fixes do not establish physical-device transfer reliability.

## September 19 preview handoff and current boundaries

September 19 preview artifacts are kept under `dist/ios-preview/`, separate from the preserved 0.4.1 release. The runtime-only Windows preview includes iOS staging tools/resources; the optional Studio preview adds its iOS target panel. The source handoff includes the native engine, dependency pins, documentation and all seven verified content stages in `ios-examples/`. It contains no compiled iPhone app or Apple SDK and **predates the actual Mac integration fixes**. Use current source and documentation for a new Mac build; do not assume an old extracted package has updated itself.

The Windows preview packages retain the previously qualified, prebuilt Android players. The source-level Android rebuild above is a separate check of the shared-loop changes. Historical phone tests are not presented as new tests of that rebuild.

Initial iOS source limits: one game app, iOS 17+, 16,384 staged files and 256 MiB of immutable content, 36,000 fixed capture ticks, 128 requested capture frames, and 16 KiB native control requests. Native imports, UI, physics and save restrictions remain those in the runtime guide. Physical-device uploads currently use existing root/inbox directories; arbitrary nested directory creation through `devicectl` awaits testing. Export builds `.app` directories; IPA/TestFlight/App Store distribution is not implemented by these initial tools.

Historical [Windows and Studio evidence](19-release-evidence.md) and [Android qualification](20-android-qualification.md) remain unchanged. Their results do not qualify newly modified runtime code or any iOS target.
