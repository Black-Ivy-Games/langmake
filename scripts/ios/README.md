# iOS/iPadOS command reference

Langmake 0.5.0 adds [per-game workflow discovery and deployment](../../docs/26-game-workflow.md), while this native tool remains independently callable. `stage --profile NAME` selects presentation. `--bundle-id`, `--label` and `--orientation` are optional when the cooked project's `application` metadata supplies them; explicit values win. `--version`, `--build-number` and `--icon` override application fields. An icon override must already belong to the cooked graph. Opaque 1024..4096px non-interlaced 8-bit RGB/RGBA or 1/2/4/8-bit indexed PNG icons generate a verified AppIcon catalog. Metadata-opted-in release projects require an icon; old projects retain legacy behavior.

Builds retain persistent operations and compilation caches independently of content. `python3 scripts/ios/ios.py operation --operation ID` shows compact status (`--full` includes evidence). A signing-blocked build returns `ok:false`, `status:"blocked"`, `operation_id`, `operation_path`, `blocking`, and—when Xcode reached a verified signable app—`resumable:true` with a generated `sign-local.sh`. Run that LF-safe helper locally on the Mac for required Keychain interaction, or `python3 scripts/ios/ios.py resume --operation ID`. Resume verifies the original candidate, signs a copy with Xcode's selected identity/entitlements, and verifies the final artifact without recompiling. Failed attempts remain recorded. Missing SDK/license/profile/toolchain prerequisites have separate next steps. None of these commands selects a Mac, signing team or phone implicitly.

Start with the [iOS agent guide](../../docs/23-ios-agent-guide.md), [implementation plan](../../docs/22-ios-plan.md) and [qualification record](../../docs/24-ios-qualification.md). The historical baseline records native Apple builds and arm64 simulator checks for 2D/3D games, plus nine signed adventure capture/state checks on an iPhone 17 Pro Max and initial human feedback on movement, interaction, music and text. Physical persistence/lifecycle and final release qualification were unfinished at that milestone. Read [current workflow qualification](../../docs/27-workflow-qualification.md) for newer results, keeping simulator, iPhone, iPad and human evidence distinct. Existing Windows and Android releases retain their separate evidence.

`ios.py` is independent of Studio. It requires Python 3.9 or newer, emits one JSON result on stdout and sends command diagnostics to stderr. A nonzero exit status indicates failure. Every selected project, staged directory, output and tool-controlled cache must remain under this Langmake workspace. Do not select another computer or directory until the human authorizes it.

iPad uses the same `--destination device --arch arm64` build/install path. The Xcode app targets both iPhone and iPad; device discovery accepts physical `iOS` and `iPadOS` records and rejects simulators on that path. Select the iPad's explicit returned ID, pair/register it within the authorized scope, and stage the game's `--profile tablet` when that profile exists. Profiles are chosen at cook time, not automatically by hardware. Read [tablet presentation and qualification limits](../../docs/23-ios-agent-guide.md#ipad-selection-and-tablet-presentation); universal metadata and host tests do not establish an iPad gameplay result.

Wireless deployment uses the same commands and explicit device ID. First pair/trust the iPhone or iPad in Xcode (use a cable when required by its OS), enable its network connection, and keep it on the same reachable network as the authorized Mac. Keep it awake/unlocked for installation and launch, and refresh `devices --destination device` before acting. `devicectl` selects USB or network transport automatically; there is no Langmake `--wireless` option. The Mac still builds and signs. The current ninth-generation iPad run completed installation and six native capture/state comparisons over reported `localNetwork`; other network configurations remain unqualified. Read [wireless setup and evidence limits](../../docs/23-ios-agent-guide.md#wireless-iphoneipad-deployment) and [Apple's pairing guide](https://developer.apple.com/documentation/xcode/pairing-your-devices-with-your-mac).

## What each computer does

| Operation | Windows source workspace | Mac source workspace |
| --- | --- | --- |
| Author JSON/Luau, validate, simulate | Existing host CLI | Native headless host CLI after building it |
| Stage registered content and Metal runtime resources | Yes | Yes, with host CLI |
| Compile the iOS player | Requires Mac | Full Xcode, CMake and Git required |
| Sign and install on an iPhone or iPad | Requires Mac | Human-configured Apple team and device trust required |
| Simulator and private app inspection | Requires Mac | Xcode `simctl` / `devicectl` |

This native tool performs no Windows-to-Mac remote execution, Apple SDK download, account login, Xcode license acceptance or global tool installation. The optional workflow facade can call it through an explicitly configured per-game SSH target. The source handoff archive contains engine sources and dependency pins; CMake retrieves the pinned free/open-source dependencies inside `.deps/source` on the Mac. Apple SDKs and signing tools remain prerequisites supplied by Xcode under Apple's terms. See [Apple's command-line tool reference](https://developer.apple.com/documentation/xcode/xcode-command-line-tool-reference).

## Stage on Windows

Run from the repository root after building the current host CLI:

```powershell
python scripts/ios/ios.py doctor
python scripts/ios/ios.py stage --project examples/shared-2d --bundle-id org.example.myiosgame --label "My iOS Game" --configuration debug --orientation landscape --output dist/ios/myiosgame-staged
python scripts/ios/ios.py verify-stage --staged dist/ios/myiosgame-staged
```

`doctor` deliberately returns `ok:false, can_stage:true, can_build:false` on Windows, explaining the Mac prerequisite. That does not prevent `stage`. Choose a **new** output directory for each stage; previous content is never overwritten. Use `--host-build DIR` to select a directory containing `langmake.exe` (Windows) or `langmake` (Mac).

Staging calls native `cook --target ios`, which validates all registered scenes and collects the source graph. It packages precisely those sources, Metal cube shaders, the bundled Lato Latin font and the license inventory. Text model/material shaders are compiled into the iOS executable. The staged layout is:

```text
manifest.json
project/project.json
project/<registered game sources>
runtime/shaders/{vs_cubes,fs_cubes}.bin
runtime/fonts/LatoLatin-Regular.ttf
runtime/licenses/...
```

The manifest records each file's byte size and SHA-256, a content checksum, original and packaged revisions, stable storage ID, bundle ID, orientation and debug/release mode. Limits are 16,384 files and 256 MiB of immutable content. Traversal, symlinks, case collisions, unexpected files, changed cook sources and budget overflow fail validation. Host staging and native startup both verify bytes before use. Project `.langmake` caches and desktop saves do not ship.

Retain `project_id` across updates. Its SHA-256 produces the 24-character `storage_id`; legacy projects fall back to the title. Retain the bundle ID and signing identity as well to preserve the installed app's progress. Renaming a legacy title without assigning stable identity creates a different save namespace. Changed content may require game-authored save migration.

## Build on the authorized Mac

Copy/extract Langmake source and the staged folder **inside the authorized Mac workspace**. Keep the staged folder unchanged; run `verify-stage` after transfer. Windows absolute paths in an earlier report are evidence only; pass the new Mac-relative staged path explicitly.

```sh
python3 scripts/ios/ios.py doctor
python3 scripts/ios/ios.py verify-stage --staged dist/ios/myiosgame-staged
python3 scripts/ios/ios.py build --staged dist/ios/myiosgame-staged --destination simulator --arch arm64 --output dist/ios/myiosgame-simulator
```

Select `arm64` for an Apple Silicon simulator and `x86_64` for an Intel simulator. Physical phones use `--destination device --arch arm64`. The deployment floor is iOS 17.0. The selected Xcode must include the device/simulator SDKs and command tools. CMake uses the Xcode generator, pinned dependencies and a Mac-native `shaderc`; the tool builds that shader compiler under `.cache/ios/host-tools` unless `--shaderc PATH` is supplied. Build parallelism defaults to four jobs and can be changed with `--jobs N` (1â€“64).

After installing or updating Xcode, open it and complete its initial setup. `doctor` checks `xcodebuild -checkFirstLaunchStatus` and blocks builds while setup is incomplete, even when the SDKs are already visible. It reports Apple's diagnostics and setup instructions; it never accepts licenses or installs Xcode components.

The host shader compiler explicitly targets the running Mac's macOS version, so installing a newer Xcode SDK does not raise the host tool's minimum OS requirement. The separate iPhone and simulator builds retain their iOS 17.0 deployment floor.

To stage future game changes directly on the Mac, build the headless host CLI separately. It supports validation/cooking/simulation; it is not a macOS graphical player:

```sh
cmake -S . -B .cache/ios/host -G Xcode -DBUILD_TESTING=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET="$(sw_vers -productVersion)"
cmake --build .cache/ios/host --config Release --target langmake --parallel 4
python3 scripts/ios/ios.py stage --project examples/shared-2d --host-build .cache/ios/host/Release --bundle-id org.example.myiosgame --label "My iOS Game" --configuration debug --output dist/ios/myiosgame-next
```

For an iPhone, configure your account/team and certificate in Xcode first, then provide the actual team ID:

```sh
python3 scripts/ios/ios.py build --staged dist/ios/myiosgame-staged --destination device --arch arm64 --team-id YOURTEAMID --output dist/ios/myiosgame-device
```

`YOURTEAMID` is a placeholder for the ten-character team identifier. This generic build is appropriate when the profile is already configured. Default signing uses the existing `Apple Development` identity with automatic signing. To select existing manual signing assets, use `--signing-identity "Apple Development: Your Name (...)" --provisioning-profile "Your Profile"`. The tool does not create credentials. The native debug flag follows the staged configuration independently of signing. See [Apple's registered-device distribution workflow](https://developer.apple.com/documentation/xcode/distributing-your-app-to-registered-devices).

For first-phone provisioning, select its explicit CoreDevice `id` from `devices`, then include that device in the build. With the prepared adventure stage from the source handoff:

```sh
python3 scripts/ios/ios.py devices --destination device
python3 scripts/ios/ios.py build --staged ios-examples/adventure --destination device --arch arm64 --team-id YOURTEAMID --device DEVICE_ID --allow-provisioning-updates --register-device --output dist/ios/adventure-phone
```

`build --device` is optional for a generic already-provisioned build. When supplied, it checks fresh physical-device classification before building, resolves the hardware UDID internally, and selects it through an Xcode scheme; `xcodebuild -target` would ignore the destination. `--allow-provisioning-updates` permits Xcode to update profiles using its configured Apple account. `--register-device` explicitly allows registering only the selected phone with the chosen team and requires both `--device` and `--allow-provisioning-updates`; include it when that registration is within the authorized task. An already registered phone can use `--device` without `--register-device`. Simulator builds reject these device and signing options. Neither flag authorizes publishing a game.

Xcode's first-launch license/setup and the Apple account/Program License Agreement (PLA) for provisioning are separate prerequisites. The human accepts any required agreement and uses Xcode's **Manage Certificates** for the selected account/team to create or select an **Apple Development** certificate. The first `codesign` use may also need a local Keychain approval. A remote `User interaction not allowed` error during private-key access requires resolving that interactive Mac prompt, then retrying signing. Retain approvals already provided; do not silently create another identity or bypass Keychain protections. Passwords and private keys stay on the Mac and are never sent in chat or stored in project files. Unsigned arm64 compilation verifies native code generation only and does not produce an installable phone app.

Build output is a standalone `LangmakePlayer.app` plus `build-report.json`, which records target, architecture, signing status, staged revisions, storage ID, toolchain and compiler hashes. Its executable has a distinct name from the bundled `langmake/` content directory. A successful build is still `device_qualified:false`. The tools verify copied content and the device code signature. **This tooling builds installable `.app` directories, not App Store/TestFlight IPA export or upload.** Application icons are supported; distribution signing, archive/export options and submission qualification remain separate work.

## Select and deploy to an iPhone

Connect the iPhone to the authorized Mac, approve trust and enable Developer Mode when requested. The human performs those device approvals. [Apple's Developer Mode instructions](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device) describe the process.

```sh
python3 scripts/ios/ios.py devices --destination device
python3 scripts/ios/ios.py install --destination device --device DEVICE_ID --app dist/ios/myiosgame-device/LangmakePlayer.app
python3 scripts/ios/ios.py launch --destination device --device DEVICE_ID --bundle-id org.example.myiosgame
```

Replace `DEVICE_ID` with the explicit identifier returned by `devices`. Device names, `all` and `booted` aliases are rejected. Installation checks the app's actual identifier, platform, content hashes and signature before deploying; it records debug/release status for later inspection. Reinstallation uses the same application identity and preserves its container; do not uninstall as an update shortcut. App data belongs to `Library/Application Support/Langmake/STORAGE_ID` inside the installed application container.

Raw `devicectl` listings in Xcode 27 can include simulators and mark them as paired. `devices --destination device` normalizes both legacy and modern Apple JSON layouts and returns verified physical iOS/iPadOS candidates in `devices`; simulated, unknown-reality and unsupported-platform entries appear in `excluded_devices` with `exclusion_reason`. Interpret the physical view's fields separately:

| Field | Meaning |
| --- | --- |
| `reality` | `physical`, `simulated` or `unknown`; pairing and the displayed phone model do not establish hardware reality. |
| `pairing_state` | Apple's pairing status, independent of whether the target is real hardware or currently reachable. |
| `reachable` | `true` for a connected/available transport, `false` for disconnected/unavailable, or `null` when the source does not establish either. |
| `available` | In the physical view, true only for a supported physical target that is paired and reachable. This does not establish signing readiness or game qualification. |
| `developer_mode` | Reported separately; a connected phone still needs the required development authorization. |

Every physical install, launch, stop and file operation refreshes classification and rejects simulated, unknown-reality or unsupported-platform targets before acting. A build with `--device` performs the same guard before configuring its Xcode destination. A disconnected physical candidate can still be listed; check its transport/pairing diagnostics before deployment. For simulator work use `--destination simulator` and an explicit device from the isolated workspace set. Never use a raw paired name as proof that the human's phone is connected.

`launch` terminates an existing instance of that selected app so new launch arguments apply. Do not install, relaunch, stop or inject controls during the human's play session. Its `pid` is returned when Apple's response includes it. `stop --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --pid PID` sends SIGTERM to that explicit PID using the installed `devicectl device process signal` contract. Never guess or reuse an old process ID. If this Xcode lacks the required command options, the tool stops with a diagnostic rather than guessing another command.

## Use an isolated simulator

Simulator devices live in `.cache/ios/simulators`, separate from the human's usual Xcode simulator set. The tool never erases or shuts down unrelated simulators. Installed iOS runtimes remain Xcode-managed prerequisites.

```sh
python3 scripts/ios/ios.py simulator-list
python3 scripts/ios/ios.py simulator-create --name "Langmake verification" --device-type DEVICE_TYPE_ID --runtime IOS_RUNTIME_ID
python3 scripts/ios/ios.py devices --destination simulator
python3 scripts/ios/ios.py simulator-boot --device SIMULATOR_ID
python3 scripts/ios/ios.py install --destination simulator --device SIMULATOR_ID --app dist/ios/myiosgame-simulator/LangmakePlayer.app
python3 scripts/ios/ios.py launch --destination simulator --device SIMULATOR_ID --bundle-id org.example.myiosgame
```

Use the exact runtime/device-type identifiers from `simulator-list`. No runtime is downloaded automatically. To capture the displayed simulator screen, use `screenshot --destination simulator --device SIMULATOR_ID --output evidence/ios/display.png`; this is **not an exact-tick** capture. Physical-device screenshots use Langmake's native capture/sidecar path below. There is no guessed `devicectl screenshot` command.

## Native inspection and commands

Use a debug app installed with this tool, preferably a distinct verification bundle ID. Release builds reject native launch arguments and these tools reject their private inspection/control transport. Debug apps ordinarily launch in player mode; `--development` enables developer controls. App-private file transport uses Apple's paired-device tools and opens no network server.

```sh
python3 scripts/ios/ios.py launch --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --development
python3 scripts/ios/ios.py inspect --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --storage-id STORAGE_ID
python3 scripts/ios/ios.py control --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --storage-id STORAGE_ID --file evidence/ios/snapshot-request.json
python3 scripts/ios/ios.py pull --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --storage-id STORAGE_ID --path results/12345.json --output evidence/ios/snapshot-receipt.json
```

Read `live.session`, `live.running`, `live.published_at_ms` and the loaded `source_revision` before issuing controls. The native loaded revision is the build report's **`packaged_revision`**, which may differ from the original `source_revision` when cooking normalizes the manifest. Validate the loaded revision and observation age explicitly. A stale snapshot does not prove an app is still running. Example request:

```json
{
  "id": "12345",
  "session": "REPLACE_WITH_CURRENT_LIVE_SESSION",
  "action": "snapshot",
  "query": {"ids": ["player"], "fields": ["position", "velocity"], "offset": 0, "limit": 32}
}
```

IDs are numeric strings of 1â€“96 digits; select a fresh unique value and preserve it if diagnosing delivery. Requests are at most 16 KiB. `control` uploads into `inbox/`, then uploads a SHA-256 commit marker. Native code promotes only a complete hash-matching request into `commands/` atomically. Direct command writes through `push` are rejected. Upload success returns `executed:null`: poll `results/ID.json` and check its `ok` and request ID before treating a command as completed. Ordinary mutation receipts also report session/tick/revision. Capture receipts report the image path and source revision; verify their session, tick and camera in the matching `.png.json` sidecar. A missing receipt is not permission to issue another step/reset with a new ID. Receipts/evidence follow the native runtime retention rules. Routine transport JSON is not accumulated per poll; the latest host inspection cache replaces its previous snapshot.

`pull` reports confirmed absence with a nonzero exit and JSON containing `ok:false`, `code:"file_not_found"`, the requested `path`, and `evidence:"successful_parent_directory_listing"` on a physical device. A generic CoreDevice error 7000 alone does not establish absence: the tool verifies a successful, unfiltered parent listing for the same device and app container. Connection, authorization, unknown listing formats and files present despite copy failure remain errors. Simulator absence uses `evidence:"simulator_container_lookup"`. Treat confirmed absence as optional only when that particular read permits it; keep polling a pending receipt without resending its mutation. Downloads replace the requested local output only after a bounded regular file has been copied successfully, preserving previous output on failure.

Native inbox processing examines at most 64 markers per frame with a rotating cursor; incomplete uploads cannot indefinitely block later ready commands. Abandoned numeric request/marker files expire after ten minutes. The inbox limit is 4,096 files; overflow reports a transport diagnostic while gameplay continues. Retain requested evidence separately from this temporary upload area.

Player saves and auto-recovery are separate from source checkpoints. Automatic iOS recovery uses `saves/_ios_resume.json`; native declared state and supported runtime systems are restored, not arbitrary Luau locals or the physics solver's internal memory. For a manual named slot, send `{"action":"save","slot":"progress",...}` and after relaunch explicitly send `load` for that slot. Use `--fresh` only for an intentionally fresh technical launch. It does not constitute deleting a human's saved game.

## Exact-tick evidence

Both destinations support the same native launch/copy workflow. Substitute `--destination simulator` and its explicit ID for simulator tests.

```sh
python3 scripts/ios/ios.py push --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --storage-id STORAGE_ID --file evidence/ios/input.json --path input.json
python3 scripts/ios/ios.py launch --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --steps 120 --input input.json --ticks 0,60,120 --output-dir evidence/run1
python3 scripts/ios/ios.py pull --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --storage-id STORAGE_ID --path launch-result.json --output evidence/ios/launch-result.json
python3 scripts/ios/ios.py pull --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --storage-id STORAGE_ID --path evidence/run1/tick-60.png --output evidence/ios/tick-60.png
python3 scripts/ios/ios.py pull --destination device --device DEVICE_ID --bundle-id org.example.myiosgame --storage-id STORAGE_ID --path evidence/run1/tick-60.png.json --output evidence/ios/tick-60.png.json
```

First launch the debug app once to initialize its private directories. Inputs are copied before the technical launch; paths are relative to private storage. Physical-device `push` initially accepts a filename at the storage root, such as `input.json`; nested upload directory creation is not assumed. Native capture output directories and committed `control` inbox uploads are managed by the app. There is no `--project`, `--runtime` or arbitrary `--storage` override in an installed app. Use a unique output directory per scenario and correlate evidence with the expected packaged revision, scene, tick, camera and timeline. Native initialization writes a running marker to `launch-result.json`; poll until the final result is present, rather than mistaking an old file or the running marker for completion. Check `ok` and capture results. Tick zero is initialized state before the first update; tick N follows N fixed updates.

Fixed-tick captures require `--steps` (0â€“36,000), `--input`, sorted unique `--ticks` (at most 128) and `--output-dir` together. Frame capture uses `--frames` (1â€“36,000) with optional `--capture relative.png`. The modes cannot be mixed. `--no-audio` is available only in a debug build. Check `xcrun devicectl device process launch --help` on the actual Mac. Native flags follow the bundle identifier directly; **do not insert an extra `--` before those flags**. Physical testing found that the earlier wrapper's separator caused a bounded request to launch ordinary play instead. The corrected wrapper now completes frame and timeline captures on the phone. Continue checking the final native tick, captures, session and revision rather than treating Apple's launch response alone as proof of completed verification.

## Technical verification and human handoff

`python scripts/ios/test_ios.py` runs host contract tests without Xcode, a network connection or any device mutation. The September 20 suite has 49 passing tests on Mac and Windows, covering source/manifest tampering, content limits, paths, stable save identity, signing/device-registration validation, scheme destinations, timing modes, UTF-8 JSON, committed uploads and legacy/modern device classification with physical-action guards. Separate native simulator evidence covers Metal execution and UIKit background recovery; the host tests alone do not qualify those systems or physical iPhone behavior.

Before claiming iOS support ready for a game, record the Mac/Xcode/SDK versions and physical device/model/OS; verify 2D and 3D rendering, UI/safe areas, simultaneous touch controls, audio/interruption, background/resume, fresh-process saves, reinstall updates, native capture parity, release-argument rejection and performance. Preserve the app/build reports and native evidence. Then hand the playable build to the human with controls, known limitations and focused feedback prompts. Technical success does not replace human gameplay feedback.

The audio path requests playback only. Pinned miniaudio selects `AVAudioSessionCategoryPlayback` and activates that session; it does not select Ambient or request microphone capture. Native `audio_mixer` observations expose `device_backend`, `device_is_null`, `device_initialization_status`, `device_ready`, `output_available`, and the last device operation/result/error. A null fallback can advance mixer counters while producing no sound, so it reports `audio_available:false` and `output_available:false`. Successful device initialization does not prove audible sound: verify the silent switch, system volume and interruptions on the actual device. Physical silent-switch behavior remains unqualified.
