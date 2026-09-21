# A shared Windows and Android game

Start with [the Android agent workflow](../docs/21-android-agent-guide.md), then check the Android export prerequisites. This brief does not grant SDK license acceptance, signing credentials, store publishing authority or permission to replace an unrelated app.

```text
Task: implement a small game that I will play on Windows and Android.

Engine: [ENGINE_PACKAGE_DIRECTORY]
New game directory: [NEW_PROJECT_DIRECTORY]
Android application ID: [MY_UNIQUE_APPLICATION_ID]
Test device, if connected: [DEVICE_SERIAL_OR_EMULATOR]

Make a short 2D game about guiding a repair drone through a damaged station.
Collect three batteries and reach the exit. One hazard type, a clear restart,
and a short original synthesized music loop are enough for this iteration.

I am the decision-making developer and primary gameplay playtester. You are
the worker developer. Implement the idea and verify its technical behavior,
then hand it to me to assess the controls, readability and difficulty.

First read ANDROID-AGENT-GUIDE.md in the engine package (source checkout:
docs/21-android-agent-guide.md), then its ordered documentation list. Follow
its complete authoring, verification, export and human-handoff workflow. Query the
actual engine capabilities, qualification record and target doctor. A portable
package needs the matching prebuilt player; its setup can omit the NDK.
Select ARM64 for a phone, x86_64 for the emulator, and the intended orientation.
Run context and follow GAME-WORKFLOW.md. Store this game's explicit target
configuration locally; do not infer a phone or machine setup from another game.
Use shared gameplay with desktop/phone presentation profiles, portable
application metadata and an original opaque PNG launcher icon. Keep app version
and build number separate from the declared save compatibility contract.
Use one shared project with named actions, keyboard controls and an authored touch layout. Keep buttons
readable, use dp for mobile fonts and button dimensions, honor the safe display
area and avoid mouse-only mechanics.
For a held action, use a custom data-action button tied to the same named action
as the keyboard. Verify hold/release, drag-out and background cancellation. For tap-to-place
controls, also verify fast press/release taps use the release position. For tap-to-place
controls, also verify fast press/release taps use the release position.

Create original text-authored sprites and music with native Langmake tools.
Keep the scope small. Preserve the source project and any existing player
saves. Use a stable project identity and the Android application ID above.

Run validation, script checks and a bounded deterministic scenario, plus
ui-check and visual inspection at the phone profile's viewport.
Register the scenario and use verify for repeatable state/save checks. Build an
APK using the native export workflow. If the authorized device is available,
install and launch this game's APK, check rendering and input, background/resume it,
and verify that progress can survive a process restart. Keep test evidence.
Use a separate application ID for automated device scenarios if a human
play session must be preserved. Android launch restarts the selected app.
Explain any missing SDK, credentials or device prerequisite accurately.
Use deployment operation reports to retain failure details and resume an
eligible blocked operation; do not discard completed work or repeat mutations.

Hand me exact Windows and Android launch instructions, controls, the APK and
its application ID, changes and known issues. Distinguish emulator evidence
from physical-phone testing. Ask me to play and give feedback before making
further creative decisions. Do not publish to an app store.
Generate the portable handoff with the brief, actual evidence and open feedback.
```

For an existing game, replace creation with adapting one playable area and its controls first. Shared files do not prove that the same difficulty, UI scale or performance works well on a phone; the human should try both targets.
