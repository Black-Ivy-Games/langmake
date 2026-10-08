# Android tooling

Langmake 0.5.0's [per-game workflow](../../docs/26-game-workflow.md) can call these tools for discovery, deployment, receipts and handoff. Direct commands remain supported. `build --profile NAME` passes the selected presentation profile to cook. `--application-id`, `--label` and `--orientation` may come from portable `application` metadata; explicit CLI values win. Additional overrides are `--version`, `--build-number` and `--icon`. App versions are separate from the save schema. An icon is an opaque square non-interlaced 8-bit RGB/RGBA or 1/2/4/8-bit indexed PNG, 192..4096px; 1024px can be shared with iOS/Windows. Export generates launcher/adaptive resources and verifies identity/version/icon resources in the actual APK. New metadata opts into an icon warning in debug or error in release; legacy games remain compatible. Keep important artwork central because Android applies device-dependent icon masks.

Agents creating or porting a game should start with the [Android agent workflow and reading list](../../docs/21-android-agent-guide.md). This file is the command/transport reference used by that workflow.

Run from the Langmake repository or extracted package root. Python and PowerShell are host tools. These scripts keep their downloads, SDK, JDK, Gradle, temporary files, Android user home, signing test key and emulator inside that root. A human-selected `--project` and explicit output/APK path may be elsewhere: a game does not have to live inside the engine installation. Existing packages are not overwritten. The optional Studio UI calls these same commands.

```powershell
python scripts/android/android.py doctor
python scripts/android/android.py bootstrap
```

`bootstrap` installs the pinned open-source JDK and Gradle, then reports the Google Android SDK license requirement. Read [the checked-in SDK terms](android-sdk-license.txt) and [Google's terms page](https://developer.android.com/studio/terms). Only after the human accepts those terms, run:

```powershell
python scripts/android/android.py bootstrap --accept-sdk-licenses
```

This flag records acceptance and installs the pinned SDK/NDK archives. An agent must not infer acceptance from a request to inspect an Android project or from a failed build. In a package with `android-player/` and no native source tree, bootstrap and doctor automatically omit the NDK and native compiler requirements. `--player-only` requests that mode explicitly when using a prebuilt player from a source checkout. Source builds also need the existing CMake, Ninja and Git commands on PATH; nothing installs those globally.

The lock file pins archive URLs and hashes: JDK/Gradle use upstream SHA-256; Google SDK archives use the SHA-1 supplied by Google's repository metadata, with downloaded SHA-256 recorded in each local install receipt. Archive entries cannot escape the workspace. Download or hash errors stop installation. Tools are Windows x64; device builds support ARM64 and x86_64 Android API 26 or newer with GLES 3. Device compatibility must be qualified separately.

## Build one game

Choose a unique application ID for each game. Reuse the same ID to update that game and retain its app-private saves. Different IDs install side by side. The host engine must support `cook`; a repository build supplies CMake/native sources, while a portable package supplies `android-player/<abi>/{debug,release}/libmain.so`, shared `android-player/runtime` resources and the SDL Java shell. Each variant has an adjacent `player.json`; legacy single-variant `<abi>/libmain.so` packages remain supported when their metadata matches the selected configuration.

```powershell
python scripts/android/android.py build --project games/my-game --application-id com.example.mygame --label "My Game" --abi arm64-v8a
```

Choose `--orientation landscape` (default), `--orientation portrait`, or `--orientation sensor`. The first two allow both directions within that orientation; `sensor` allows the game to rotate between portrait and landscape. This setting is enforced after SDL window creation and is recorded in the build report. Design and inspect the HUD and touch controls at every supported aspect ratio; an export setting alone does not establish usable layouts.

The result is JSON with the APK path, hash, application ID, original `source_revision`, `packaged_revision`, storage ID and generated Gradle project. Android runtime observations report the packaged revision, which can differ when the cook step normalizes a manifest filename. The default is a **debug APK**, signed with a workspace-local development key. Builds validate APK signature, native ELF load-segment alignment and 16 KiB ZIP alignment. A build alone is not proof that a device can run the game. `--abi x86_64` builds for the emulator. `--host-build`, `--shaderc`, `--native-build`, and `--player-dir` override workspace-contained inputs. `--skip-native` reuses a debug native library after checking its CMake configuration; source release builds cannot use it. A package missing the native player/resources fails early, before recommending SDK installation.

The game cook manifest lists exactly which registered source files ship. The APK install manifest adds SHA-256 hashes, and the app verifies extracted bytes before starting. Content lives under a revision directory; after successful verification, the app retains the current revision and one previous revision. Saves, observations and evidence live separately under `files/state/<storage_id>` and are never removed by content cleanup. No project `.langmake` caches or desktop saves ship. This first mobile implementation retains the native recipe compilation needed by the current player; it does not yet provide a fully precompiled asset format.

Build reports distinguish `native_validation`, the input native library, from `packaged_native_validation`, the actual library extracted from the APK after Gradle removes debug symbols. Both contain ABI, 16 KiB ELF checks and SHA-256. Their hashes may differ legitimately; compare installed-package evidence to the packaged hash.

## Install and inspect

```powershell
python scripts/android/android.py devices
python scripts/android/android.py install --serial DEVICE_ID --application-id com.example.mygame --apk dist/android/BUILD.apk
python scripts/android/android.py launch --serial DEVICE_ID --application-id com.example.mygame
python scripts/android/android.py logs --serial DEVICE_ID --application-id com.example.mygame --output evidence/android/log.txt
python scripts/android/android.py screenshot --serial DEVICE_ID --output evidence/android/screen.png
python scripts/android/android.py stop --serial DEVICE_ID --application-id com.example.mygame
```

Every device operation requires an explicit serial. Installation checks the APK's actual application ID before replacing that app. `launch` restarts the selected app so its intent arguments apply; do not launch, stop, install or inject controls while the human is playtesting unless requested. ADB uses isolated server port 5038 and workspace-local keys. The human authorizes USB debugging on the phone. No operation installs a device driver or changes Windows virtualization features.

Debug APKs launch in ordinary player mode by default. For a technical session with control commands, explicitly launch with `--debug-args --development`. Debug builds expose app-private native inspection through Android's `run-as`, without opening a network server:

```powershell
python scripts/android/android.py launch --serial DEVICE_ID --application-id com.example.mygame --debug-args --development
python scripts/android/android.py inspect --serial DEVICE_ID --application-id com.example.mygame --storage-id STORAGE_ID
python scripts/android/android.py control --serial DEVICE_ID --application-id com.example.mygame --storage-id STORAGE_ID --file requests/pause.json
python scripts/android/android.py pull --serial DEVICE_ID --application-id com.example.mygame --storage-id STORAGE_ID --path results/REQUEST_ID.json --output evidence/android/receipt.json
```

Control files use the engine's existing command envelope and a numeric request ID string of 1–96 digits. Release apps are non-debuggable, so Android denies this `run-as` access path. A device screenshot is a screenshot of the displayed app, not an exact-tick engine capture. For deterministic graphical checks, copy a timeline into the app's private storage and pass bounded debug launch arguments; paths in those arguments are relative to the storage directory:

```powershell
python scripts/android/android.py push --serial DEVICE_ID --application-id com.example.mygame --storage-id STORAGE_ID --file games/my-game/input.json --path input.json
python scripts/android/android.py launch --serial DEVICE_ID --application-id com.example.mygame --debug-args --steps 120 --input input.json --ticks 0,60,120 --output-dir evidence/run1
python scripts/android/android.py pull --serial DEVICE_ID --application-id com.example.mygame --storage-id STORAGE_ID --path evidence/run1/tick-60.png --output evidence/android/tick-60.png
```

Exact filenames and sidecars follow the native capture command. Debug flags are allowlisted in Java and independently gated in the native build. The ordinary app resumes its compatible saved session; `--debug-args --fresh` requests a fresh technical test.

Automatic recovery is stored at `files/state/<storage_id>/saves/_android_resume.json` when the interactive game backgrounds or exits. Recovery preserves declared engine state and supported saved runtime systems; arbitrary Luau locals/globals and the exact audio playback phase are not restored. Keep persistent gameplay progress in declared engine state and use explicit save migrations when content changes.

An incompatible or corrupt automatic save is preserved at `files/state/<storage_id>/saves/_android_resume.rejected.json`, with its diagnostic in `_android_resume.rejected.json.reason.json` in the same directory. Only one rejected slot is retained, replacing the previous rejected record. If preservation fails, automatic saves remain blocked for that session and native observations report the diagnostic in `rejected_recovery`; manual saves remain available. A successful quarantine allows a fresh automatic save without overwriting the rejected bytes.

## Native command and capture recipes

Use a debug APK in a session reserved for technical checks. The following PowerShell snippets use the setup variables from the [agent guide](../../docs/21-android-agent-guide.md): `$lmAndroid`, `$lmBuild` (the parsed build report), `$lmSerial`, `$lmProject`, `$lmEvidence` and `$lmRun`. Use the report for the actual installed test application. For independent device scenarios, build/install with a separate verification application ID; relaunching an app stops its previous session.

### Read a live snapshot and receive a command result

```powershell
$lmLiveText = python $lmAndroid inspect --serial $lmSerial --application-id $lmBuild.application_id --storage-id $lmBuild.storage_id
if ($LASTEXITCODE -ne 0) { throw ($lmLiveText -join "`n") }
$lmLive = (($lmLiveText -join "`n") | ConvertFrom-Json).live
$lmAgeMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - $lmLive.published_at_ms
if (-not $lmLive.running -or $lmAgeMs -lt 0 -or $lmAgeMs -gt 5000) {
    throw "No recent running observation; inspect the app/logs and clock agreement."
}
if ($lmLive.source_revision -ne $lmBuild.packaged_revision) { throw "Unexpected installed content revision." }
$lmRequestId = [DateTime]::UtcNow.Ticks.ToString()
$lmRequestPath = Join-Path $lmEvidence "$lmRequestId.request.json"
$lmRequest = @{ id=$lmRequestId; session=$lmLive.session; action="snapshot";
    query=@{ ids=@("player"); fields=@("position","velocity"); offset=0; limit=32 } }
[System.IO.File]::WriteAllText($lmRequestPath, ($lmRequest | ConvertTo-Json -Depth 8), [System.Text.UTF8Encoding]::new($false))
python $lmAndroid control --serial $lmSerial --application-id $lmBuild.application_id --storage-id $lmBuild.storage_id --file $lmRequestPath
if ($LASTEXITCODE -ne 0) { throw "Request upload failed; inspect the transport diagnostic." }
$lmReceiptPath = Join-Path $lmEvidence "$lmRequestId.receipt.json"
$lmDeadline = [DateTime]::UtcNow.AddSeconds(15)
$lmReceipt = $null
while ([DateTime]::UtcNow -lt $lmDeadline) {
    $lmPullText = python $lmAndroid pull --serial $lmSerial --application-id $lmBuild.application_id --storage-id $lmBuild.storage_id --path "results/$lmRequestId.json" --output $lmReceiptPath
    if ($LASTEXITCODE -eq 0) {
        $lmReceipt = Get-Content -LiteralPath $lmReceiptPath -Raw | ConvertFrom-Json
        break
    }
    Start-Sleep -Milliseconds 250
}
if ($null -eq $lmReceipt) { throw "Receipt not received: inspect outcome before attempting another mutation." }
if (-not $lmReceipt.ok) { throw ($lmReceipt | ConvertTo-Json -Depth 8) }
$lmReceipt
```

Replace the example `player` query with IDs found in the game's own inspection. `inspect` returns the full `live` object; it does not support desktop `--ids`, `--fields` or `--check-source` flags. Filter locally or request a filtered native snapshot as above. `published_at_ms`, `publication_sequence`, `session`, `running`, `diagnostic` and the packaged revision distinguish fresh observations from stale files; the example's five-second threshold is a conservative probe threshold, not a renderer guarantee.

The uploaded JSON requires `id` (a string of 1..96 ASCII digits), `session` and `action`. Use a new request ID for each logical request. Optional fields include `expires_at_ms`, `include_state`, `query`, `slot`, `steps` and `input`. `step` permits 0..600 steps; its `input` is one input object held for those steps, not a timeline file. To save/load, use `action:"save"` or `action:"load"` and `slot:"progress"`, then verify the receipt and restored state. Player mode accepts snapshot/capture/save/load; pause/reset/step/resume require an explicit development launch. Preserve any existing human session before making that launch.

For a manual-save persistence check, carry out these steps in order:

1. Send a new request with `action:"save"` and `slot:"progress"`; wait for its successful receipt.
2. Pull `--path saves/progress.json` to a separate host save-evidence file and retain its hash.
3. Stop/relaunch the verification app (use `--debug-args --fresh` if deliberately starting without automatic recovery). Do not uninstall or clear app data.
4. Read the newly running live snapshot and use its **new session** in a fresh request with `action:"load"`, `slot:"progress"`, and `include_state:true`.
5. Receive that load receipt, assert restored declared progression and correct restore-hook behavior, then capture/inspect the resulting game.

A manual `progress` save is not the automatic `_android_resume.json` slot. Normal relaunch restores only the compatible automatic slot. Test background/resume and process-death recovery separately after an actual background save finishes; a force-stop immediately after queuing a save does not prove persistence.

A successful `control` result means the file was uploaded. The native receipt establishes execution success. If it is missing, inspect the state/session and logs; do not blindly issue another step/reset/load. Receipt pulls do not delete remote results; bounded retention limits them to ten minutes, 1,024 files or 16 MiB. Requested image/sidecar evidence is retained separately. Write command JSON as UTF-8 without a BOM, as shown, for both Windows PowerShell and PowerShell 7.

### Capture a reproducible timeline on the device

Use a unique run directory, and push the host timeline into app-private storage:

```powershell
$lmDeviceRun = "evidence/run-$lmRun"
python $lmAndroid push --serial $lmSerial --application-id $lmBuild.application_id --storage-id $lmBuild.storage_id --file "$lmProject/verification-input.json" --path "$lmDeviceRun/input.json"
# Before launch, optionally pull the previous launch-result.json and record state.session.
python $lmAndroid launch --serial $lmSerial --application-id $lmBuild.application_id --debug-args --steps 120 --input "$lmDeviceRun/input.json" --ticks 0,30,120 --output-dir $lmDeviceRun
python $lmAndroid pull --serial $lmSerial --application-id $lmBuild.application_id --storage-id $lmBuild.storage_id --path launch-result.json --output "$lmEvidence/android-launch-result.json"
# Repeat the pull with a bounded wait until this run completes; criteria below.
foreach ($lmTick in @(0,30,120)) {
    python $lmAndroid pull --serial $lmSerial --application-id $lmBuild.application_id --storage-id $lmBuild.storage_id --path "$lmDeviceRun/tick-$lmTick.png" --output "$lmEvidence/android-tick-$lmTick.png"
    python $lmAndroid pull --serial $lmSerial --application-id $lmBuild.application_id --storage-id $lmBuild.storage_id --path "$lmDeviceRun/tick-$lmTick.png.json" --output "$lmEvidence/android-tick-$lmTick.png.json"
}
```

Do not run the final download loop until completion is established. Poll at a modest interval (for example, 500 ms with a 120-second limit for a small 120-tick scene), checking exit codes and parsing the newly pulled file each time. `running:true` means initialization succeeded and work continues. A successful final result has `ok:true`, `state`, `captures`, and the requested final `state.tick`; its `running` field may be absent. Require capture paths containing this unique `$lmDeviceRun` and a new `state.session` if a prior session was recorded. A reused old `launch-result.json` is not evidence that the new run completed. Treat a new `ok:false`/`diagnostic` as failure; on timeout collect app logs and report the incomplete run. Inspect the sidecars' tick, session, camera, revisions and gameplay state, and compare relevant fields with the host scenario using appropriate floating-point tolerances.

Android timing mode requires `--steps`, `--input`, `--ticks` and `--output-dir` together. Steps are 0..36000; at most 128 capture ticks are accepted, unique/increasing and within that range. Paths after `--debug-args` are storage-relative, never Windows paths. Do not add desktop-only `--no-overlay`, `--width` or `--height`. Legacy Android `--frames` cannot be combined with timeline mode. Timeline runs ignore interactive input and do not read/write automatic recovery saves; they exit after producing their result. Top-level `inspect` reads the ordinary interactive live file and is not the completion signal for these isolated native sessions. Separate application IDs are needed to keep an ongoing human app session independent.

## Release artifacts

Source builds can use `--configuration release` to disable development launch arguments at compile time and disable Android debugging. The shared observation/save mailbox remains in app-private storage, but Android denies `run-as` access to the release app. An unsigned release APK is clearly labeled and cannot be installed until signed. Prebuilt packages can export release only when the selected library's adjacent `player.json` explicitly declares `development:false`, the matching `abi`, and the library's `sha256`. An unlabeled prebuilt library is treated as a development build.

```powershell
python scripts/android/android.py build --project games/my-game --application-id com.example.mygame --configuration release --keystore signing/mygame.jks --key-alias mygame --store-password-env MYGAME_STORE_PASSWORD --key-password-env MYGAME_KEY_PASSWORD --bundle
```

Supply passwords through the named environment variables; never put password values in arguments or game files. The keystore is an explicitly selected input. The command produces signed APK and optional AAB artifacts; it does not enroll a store account or publish anything. Protect a real release key and retain it for future app updates. Development keys are only for tests.

## Optional emulator

```powershell
python scripts/android/android.py bootstrap --accept-sdk-licenses --emulator
python scripts/android/android.py emulator-check
python scripts/android/android.py emulator-start
python scripts/android/android.py devices
```

The optional pinned AOSP API 36 image uses `emulator-5556` and needs no Google account. It starts without a window; use `--visible` when the human wants to interact with it. `--accel off` is an explicit slow fallback when acceleration is unavailable. This tooling never installs a hypervisor. Emulator evidence must be reported separately from physical-phone evidence. APK builds use `--abi x86_64` for this emulator.

Run host tooling checks with `python scripts/android/test_android.py`. These checks exercise packaging and path/authorization contracts; they do not claim Android rendering, lifecycle, touch or audio qualification.

Binary pull transport encodes bytes as base64 on the device and validates/decodes them before atomic host publication. This preserves remote exit status while avoiding Windows ADB newline conversion, which can corrupt PNG/WAV/save evidence even with `shell -T`. Invalid encoding or a failed read leaves earlier output intact.
