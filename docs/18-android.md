# Android export in Langmake 0.4.1

**Agents: start with [Android game creation instructions](21-android-agent-guide.md).** That guide defines the reading order and complete author/test/build/install/handoff sequence. This page is the platform and touch-authoring reference.

**Langmake builds standalone Android APKs and AABs and runs games through its native GLES 3 player.** ARM64 and x86_64 debug/release builds, signing, portable exports, physical-phone and emulator checks are complete. See the [0.4.1 qualification record](20-android-qualification.md) for tested artifacts, performance and coverage limits.

The build baseline is Android 8.0/API 26 or newer, OpenGL ES 3, ARM64 phones and x86_64 emulators. The 2D/3D qualification suites ran on a Samsung SM-A146U running Android 15/API 35, where the human confirmed controls and audible music, and an Android 16/API 36 x86_64 emulator. These observations do not establish compatibility with every supported Android version or device, nor acceptance of game feel.

Start with the [Android tool guide](../scripts/android/README.md). It covers the pinned workspace-local toolchain, SDK terms, source builds, portable exports, release signing, optional emulator, and device commands. No global installation is required by these scripts. The 0.4.1 runtime and Studio distributions include prebuilt Android players for both ABIs and configurations. Installing the SDK cannot repair a package missing its native player.

```powershell
python scripts/android/android.py doctor
python scripts/android/android.py devices
```

After the SDK terms have been accepted and the local toolchain is installed, the build command for this checkout is:

```powershell
python scripts/android/android.py build --project games/my-game --application-id com.example.mygame --abi arm64-v8a --orientation landscape
```

An extracted package containing `android-player/` uses its prebuilt native libraries automatically; no source-build override is needed. A package without those libraries cannot export Android merely by installing the SDK. Debug and release players have separate, checked metadata.

Use a unique application ID for each game and preserve it for updates. Choose `--orientation landscape` (default), `portrait`, or `sensor`; the last supports rotation between portrait and landscape. The choice is applied by both Android and the SDL host, so a resizable window cannot silently override it. Check each supported layout, including touch controls and device cutouts.

The default debug APK opens in normal player mode; technical controls require an explicit development launch. Builds report both the original `source_revision` and `packaged_revision`; compare Android runtime observations with the latter. Game content and save data are stored separately. The tool guide provides explicit-serial installation, launch, logs, screenshots, native inspection, deterministic capture, and signed release APK/AAB commands.

## Author touch controls through the existing input API

Merge this fragment into a project, preserving its other actions. It describes a movement stick and two ordered action buttons; it does not implement the gameplay responses to those actions.

```json
{
  "input": {
    "actions": {
      "interact": {"keys": ["E"], "gamepad": "north"},
      "attack": {"keys": ["F"], "gamepad": "rightshoulder"}
    },
    "touch": {
      "enabled": true,
      "stick": {
        "enabled": true,
        "x_action": "move_x",
        "y_action": "move_z"
      },
      "buttons": [
        {"action": "interact", "label": "Talk / Use"},
        {"action": "attack", "label": "Attack"}
      ]
    }
  }
}
```

Scripts read `engine.input("move_x")`, `engine.input("move_z")` and `engine.input_pressed("interact")` as usual. Stick axes are distinct; upward motion produces a negative vertical-axis value. Registered actions are numeric, so test `> 0` or use `input_pressed`, because Lua treats zero as true. Touch buttons may also use legacy `jump` and `fire`. Omitted touch configuration supplies the default stick and action buttons; explicit `buttons` controls their order, with six shown per page. Use `"enabled": false` to hide the movement/action overlay while keeping game UI available.

The human remains the primary gameplay playtester. Agents prepare technical evidence and a playable handoff with controls and focused feedback prompts, following the [human-directed workflow](08-agent-api.md). Phone touch comfort, readability, heat, sustained performance, interruptions and audio behavior still need real device testing. Emulator observations must be labeled separately.

September 21, 2026 source update: mobile pointer coordinates now survive finger release until the next consumed fixed tick, matching queued semantic UI clicks. Authored tap-to-place games should verify rapid down/up taps as well as held gestures on the target device. This does not qualify untested devices or historical packaged players.
