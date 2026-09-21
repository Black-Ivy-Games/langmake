# iOS implementation and qualification plan

Started September 19, 2026. This is an authorized implementation plan, not evidence that an iPhone build has passed. The existing 0.4.1 Windows and Android distributions and their qualification records remain preserved.

## Goal and platform contract

Export an existing Langmake JSON/Luau game as a standalone iOS app using the same runtime and authored assets. Keep command-line workflows independent of optional Studio. External agents remain supported. No model connection or provider SDK belongs in the runtime or exported app.

Initial target: iOS 17 or later, arm64 iPhone devices, and arm64/x86_64 iOS simulators, with bgfx Metal rendering. Simulator execution does not qualify a physical iPhone. Windows can author and prepare content; native compilation, signing and installation require an explicitly configured Mac with full Xcode. Do not discover remote machines or upload projects without an authorized destination.

The build produces `LangmakePlayer.app`, whose executable is `LangmakePlayer`. Its immutable `langmake/project/`, `langmake/runtime/` (Metal shaders and font) and `langmake/manifest.json` remain separate from that executable. Saves, observations and development requests use a writable, project-scoped Application Support directory. Preserve stable project identity and bundle identifier when updating an installed game. Never put signing credentials in game content or generated manifests.

## Phases

1. **Shared runtime and native app:** add an iOS CMake target and UIKit-compatible SDL entry point; adapt the existing frame loop to callbacks while preserving desktop/Android behavior; connect Metal, safe areas, orientation, touch, audio interruptions and background recovery. Retain the existing gameplay/UI contract rather than silently changing games.
2. **Standalone Apple tools:** provide a prerequisite doctor, validated content staging, reproducible Xcode configuration, explicit signing and destination options, simulator/device install and launch, and development diagnostics. Keep generated projects, dependency sources and caches in the authorized workspace. Reject missing prerequisites and unsupported options with actionable diagnostics.
3. **Agent verification:** provide development-only native inspection, command receipts and deterministic capture with tick/state evidence. Keep production launch arguments closed to developer control. Exercise durable save recovery and updates without treating a successful transfer as successful execution.
4. **Optional Studio and documentation:** expose the same tools from target management, publish an ordered agent guide and human launch instructions, update capability discovery and prompt examples, and keep runtime-only use possible.
5. **Local regression and portable handoff:** verify cook graph isolation, identity/integrity checks, command validation, Studio behavior and Windows/Android compatibility. Prepare a portable source/build handoff for an authorized Mac. Preserve previous release packages and historical evidence.
6. **Apple qualification:** compile both simulator and device configurations on a Mac, inspect Metal rendering and captures, and install on a real iPhone. Test touch, UI focus/safe areas, audio, pause/resume, termination/recovery, saves and same-identity updates. Record host/Xcode/device versions and measured results. Fix integration defects before calling the iOS port ready.

## Acceptance gates

| Gate | Required evidence |
| --- | --- |
| Content | All seven starter source graphs validate, cook and preserve deterministic headless state; private files are excluded; invalid or changed content fails without overwriting existing output. |
| Runtime regression | Existing native/headless checks and Windows graphics/live checks pass after the shared-loop change; Android still compiles and retains its input/recovery contract. |
| Build tools | Path boundaries, integrity hashes, signing choices, destination selection, development/release separation and failure handling have automated tests. Actual Xcode commands also run on the target host. |
| Simulator | 2D and 3D apps launch; native observations and fixed-tick captures agree; resizing/orientation, focus, saves and recovery work. |
| iPhone | A signed standalone game is installed on a named device; rendering, controls and audio work; background/resume, process restart and updates preserve declared state. |
| Distribution | Exported content and runtime resources are complete; exact build/install/launch commands and signing prerequisites are documented. No Windows path is embedded as a runtime requirement. |
| Human handoff | Human receives the playable app, controls, changes and focused feedback prompts. Technical qualification and human gameplay approval are reported separately. |

## Boundaries

This work does not include App Store submission, developer enrollment, remote-host discovery, visionOS/XR, multiplayer, or game-specific redesign. Store eligibility and distribution signing remain separate from local device qualification. The engine does not download new gameplay code in an exported game; Luau behavior is bundled with the authored project.

Custom authored UI currently exposes click/change events; iOS porting does not implicitly authorize or include the separately discussed hold-to-fire UI feature. Existing built-in mobile actions retain held input semantics.

## Status and prerequisites

The native source adapter, independent tools, optional Studio integration and host-side checks are implemented. September 19 work completed the checks possible on Windows; that historical record remains preserved.

On September 20 the human authorized `<MAC_WORKSPACE>` on an Apple silicon Mac, macOS 26.6.2 and Xcode 27.0 (27A266a). The Mac prerequisite doctor passed, and the updated 49 host tool tests pass on Mac and Windows. Native `shaderc` builds with an explicit deployment target matching the running macOS. The 2D rockets app compiled, installed and ran with Metal in an arm64 iPhone 16 simulator using iOS 27.0. Seven capture sidecars matched Windows headless gameplay state at ticks 0, 79, 80, 81, 149, 150 and 480. A repeated audio-enabled simulator run completed after an initial Apple RemoteIO abort and a human-approved system audio prompt; physical speaker output still needs verification.

Apple qualification is progressing through phase 6. The 3D adventure app now builds through the official exporter and runs with Metal. Nine capture/state comparisons pass within the recorded floating-point tolerances; inspection confirms readable HUD, lit articulated characters and the expected inventory display. The 2D and adventure scenarios passed 24 and 43 native requests respectively, covering saves, mutation/restore, fresh-process load, same-bundle reinstall and corrupt-save rejection without changing declared state. Adventure verification includes progression, inventory/quests, saved areas, controller/animation, camera and random-generator state. Its audio-enabled run reports a ready Core Audio output backend with no clipping or dropped commands.

The adventure also passed actual UIKit background/foreground checks in the simulator: autosave matches the paused baseline, Core Audio suspends/resumes successfully, same-process state is retained, and process restart automatically restores progression/entities/stored scenes. Signing subsequently succeeded through the local Mac Terminal launcher, and the adventure is installed on the physical iPhone 17 Pro Max running iOS 26.6.2. Nine official-wrapper phone captures through tick 600 match headless state within the recorded tolerances. The app was then launched fresh in ordinary player mode, and the human confirmed movement, interaction, music and readable text. Physical persistence/lifecycle checks and full release qualification remain unfinished.

The first physical launch exposed an argument-forwarding defect: an extra `--` token before the app flags caused the bounded request to start ordinary play. The corrected wrapper passes native flags directly after the bundle identifier; frame and timeline captures now complete on the phone, and 49 host tests pass on Mac and Windows. The tools also reject simulated or unverified targets for physical-device operations. See [phone evidence and initial feedback](releases/ios-phone-smoke-evidence-20260920.json), [simulator evidence](releases/ios-simulator-evidence-20260920.json) and the [qualification record](24-ios-qualification.md). No distribution was regenerated for this phone-play milestone; previous packages remain unchanged. Initial positive feedback is not full creative approval or release qualification.

Primary references: [Apple command-line tools](https://developer.apple.com/documentation/xcode/xcode-command-line-tool-reference), [Developer Mode](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device), [SDL iOS support](https://wiki.libsdl.org/SDL3/README-ios), and [CMake Apple toolchains](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html). The pinned dependency source is authoritative for integration details.
