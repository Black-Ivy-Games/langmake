# Langmake 0.4.1: Android release qualification

September 19, 2026. **The standalone Android player and game exporter are implemented and qualified on the reference phone and emulator below.** Runtime-only and optional Studio packages include ARM64 and x86_64 debug/release players. Games run without Studio, an LLM, or an AI account. This is an alpha ready for human playtesting, with device coverage bounded by the evidence below.

The earlier [0.4.0 record](19-release-evidence.md) and previous distributions are preserved. Its pending Android statements describe that earlier release.

## Artifacts and first test

- Runtime: `dist/Langmake-0.4.1-windows-x64.zip`.
- Optional desktop UI with runtime: `dist/Langmake-Studio-0.4.1-windows-x64.zip`.
- Phone demo: `dist/android/langmake-star-patrol-0.4.1-arm64.apk`, with adjacent build report and `.aab`.
- Inventories: `package-manifest.json`, `studio-package-manifest.json`, and `android-player/manifest.json`. The release-level `dist/Langmake-0.4.1-release.json` records archive hashes and verification results.

**Langmake Star Patrol is installed on the reference phone.** Open it from the phone's app list. Move with the left stick and shoot with **Fire**. The inherited **Jump** button has no function in this old starter. This technical demonstration uses the unchanged `rockets-2d` project. It was stopped after technical checks; opening it starts normal player mode.

The demo uses release configuration signed with the workspace's **development certificate**. It is suitable for this test, not a production signing identity or a store submission. Published games need the developer's own release key. The exporter supports signed APK and AAB output; it does not enroll accounts or publish to stores.

From this checkout:

```powershell
& ".\dist\Langmake-Studio-0.4.1\Langmake Studio.exe"
.\build\langmake.exe new --template rockets-2d --output games/my-android-game
python scripts/android/android.py build --project games/my-android-game --application-id com.example.mygame --abi arm64-v8a --orientation landscape
python scripts/android/android.py devices
python scripts/android/android.py install --serial DEVICE_ID --application-id com.example.mygame --apk APK_PATH_FROM_BUILD_RESULT
python scripts/android/android.py launch --serial DEVICE_ID --application-id com.example.mygame
```

Replace placeholders with actual values. In an extracted package, use its `langmake.exe` and the same Android commands from that package root. Prebuilt native players remove the engine compiler requirement. Python and local SDK/JDK/Gradle are still required; see [Android authoring](18-android.md) and [tool commands](../scripts/android/README.md). This workspace already has the accepted toolchain. Other workspaces must handle SDK terms explicitly.

Studio's **Targets** panel uses the same exporter with ABI and orientation selection. External directory agents remain fully supported. Each export writes a new APK and preserves previous outputs.

## Reference environment

| Environment | Recorded configuration |
| --- | --- |
| Host | Windows 11 Pro 10.0.26200; Ryzen 7 5800X, 8 cores / 16 logical processors; approximately 64 GiB RAM |
| Phone | Samsung SM-A146U; Android 15 / API 35; MediaTek MT6833; ARM64; 1080 × 2408 display; density 450; 4 KiB OS pages |
| Emulator | AOSP Android 16 / API 36; x86_64; 1080 × 1920; density 420; GLES 3 through Google SwiftShader; WHPX |
| Toolchain | JDK 17.0.18+8; Gradle 8.11.1; AGP 8.10.1; NDK 28.2.13676358; platform API 36; build-tools 36.0.0; platform-tools 37.0.1 |
| Build baseline | Android API 26+, OpenGL ES 3, arm64-v8a or x86_64; C++20 runtime with static subsystem dependencies |

Downloads, acceptance receipts, caches and evidence stay in the workspace. Device serials remain in local records. Device commands use explicit serials and isolated ADB server port 5038. Unrelated applications and their data were preserved.

## Executed checks

Reports below are under `.cache/android-qualification/` unless stated otherwise. A compact machine-readable record is `docs/releases/0.4.1-evidence.json`, packaged as `api/release-evidence.json`.

| Check | Result and evidence |
| --- | --- |
| Native builds | Both ABIs in debug/release; actual packaged libraries verified for ABI, SHA-256 and 16 KiB ELF alignment; APK signing/alignment checked. `player/manifest.json` and APK build reports |
| Phone 2D/3D | 12 checks each: launch, button, stick/release, action pages, authored UI touch, manual saves, background freeze, audio/surface resume, rotation/safe area, process-death recovery, incompatible/corrupt save preservation. `phone-probe2d-final/`, `phone-probe3d-final/` |
| APK update | 3 checks: same-APK reinstall preserves declared progress and unchanged manual save; restore succeeds. No uninstall, data clear or content migration. `phone-update-final/` |
| Emulator 2D/3D | Same 12 checks each. `emulator-probe2d-final/`, `emulator-probe3d-final/` |
| Script watchdog | Runaway script pauses at its 50 ms CPU budget while inspection/capture remains responsive: 2 checks. Android thread CPU time avoids false errors from host descheduling. `emulator-budget-final/` |
| Replay parity | 30 phone captures, 15 each in 2D/3D, at tick 0, action boundaries and tick 240. Sidecars match Windows simulation over 22 fields including entities/animation, camera, UI and event streams, with absolute float tolerance 1e-4 and relative 1e-5. `phone-probe2d-timeline-final/`, `phone-probe3d-timeline-final/` |
| Existing games/models | All seven original starters run unchanged on the phone; an eighth fixture displays textured, skinned glTF/GLB poses. 60 technical checks including adventure scene transitions/restoration. `starter-phone-final/device-index.json` |
| Release player | ARM64 Star Patrol installs and runs; displayed pixels and audio initialization checked; Android run-as access rejected. `phone-release-final/` |
| Portable Android export | Extracted ZIP hashes; two debug exports preserve earlier outputs; signed release APK/AAB; x86_64 release installation/display and run-as rejection on emulator. No NDK, CMake, Ninja, Git or engine sources available. Verified SDK/JDK/Gradle files and immutable Maven artifacts were copied into the isolated extraction; no fresh download is claimed. `portable-export/report.json` |
| Windows | Native, integration, original-game playthrough, assets and graphics/live suites passed, including forced contention with 652 entities. `windows-regressions.log`; canonical build also passes 11 native tests |
| Runtime package | All seven starters create/validate/check/simulate/render with empty PATH; native art/audio, font HUD, tick captures and ZIP extraction/hashes passed. `package-smoke.log` |
| Studio | 21 core tests; real Electron UI with an explicitly fake local provider exercises tools, editing/checkpoints, Play/Stop protection, feedback/chats and ABI/orientation persistence. Packaged UI passed. `studio/.cache/android-core-final.log`, `studio/.cache/android-packaged-ui-final.log` |

Probe fixtures also passed 26 host checks before device use. Earlier failed runs are preserved for diagnosis; final reports above are authoritative. Initial portable-test failures were harness environment/resume errors before any export. Studio transport/UI tests do not claim authenticated cloud-model generation.

## Performance observations

The original strategy reference ran for approximately 182 seconds with 128 workers, alternating group commands and recall. Thirty-four samples observed up to 128 navigating agents; the active crowd count falls as tasks finish. Rolling frame p95 ranged **14.25–15.37 ms**. Sampled simulation time averaged **7.06 ms**, with a **17.42 ms** maximum: not every tick is guaranteed below 16.67 ms. PSS memory ranged **176.6–188.9 MiB**, starting at **183.5 MiB** and ending at **179.0 MiB**, without sustained growth. Render CPU samples ranged 2.27–6.03 ms. GPU timings were unavailable, not zero-cost rendering.

This qualifies a modest reference workload on this phone, not arbitrary games or long thermal/battery behavior. Raw measurements: `starter-phone-final/strategy/strategy-samples.json` and adjacent dumpsys records.

## Human feedback and limits

The human confirmed the initial 3D display/controls worked, heard the synthesized music, and reported it was quiet. The probe had low score/instrument gain. Star Patrol's unchanged music measures peak 0.4263, RMS 0.0534 and zero clipped frames offline. System volume was not changed. The demo's perceived level and game feel still need human feedback.

- Saves restore declared engine/game state and reconstructed systems, not arbitrary Luau locals, a VM, solver internals or exact audio playback phase. Old shooters retain some progress in script locals; authors should move persistent progression into declared state.
- Rejected recovery bytes are retained at `saves/_android_resume.rejected.json`, with `saves/_android_resume.rejected.json.reason.json`. One rejected slot is retained. Failed preservation blocks automatic overwrites and exposes a diagnostic; manual saves remain available.
- Debug APKs default to player mode; technical launch arguments must be explicit. Release disables those arguments at compile time and is not Android-debuggable. The shared runtime retains its app-private observation/save mailbox; it opens no network service.
- Default controls expose existing actions, six buttons per page. Old desktop labels/unused actions remain unchanged. Customize `input.touch` and HUD layout for a useful phone game. UI layout is density-independent and display-safe; snapshots report physical hit bounds.
- API 26 is the build baseline; execution was measured on API 35/36. Actual 16 KiB-page hardware, other GPUs, external Bluetooth controllers, simultaneous physical multi-touch, calls/audio-focus interruptions and long thermal behavior were not exhaustively qualified.
- Cooking packages a verified source graph and compiles native asset recipes at startup. Large games need startup/memory tests. Existing bounded entity/model/navigation, simple-material and synthesized-audio limits still apply.
- Windows remains the authoring host. iOS, XR, consoles, streaming worlds, multiplayer and store submission are outside this release.

For another game, use [the Android prompt example](../prompts/09-android-game.md) with actual package/project paths. Request a small shared Windows/Android iteration, native evidence and a phone handoff with controls. The human decides the next creative iteration after playing.
