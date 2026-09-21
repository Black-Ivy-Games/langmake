# A shared game prepared for iPhone or iPad

Use this structure when requesting an iPhone or iPad port. **Historical coverage includes 2D/3D simulator evidence, nine signed iPhone capture/state checks and positive initial human feedback.** Consult the current qualification record for newer checks. The universal app configuration includes iPad, but an iPhone test does not establish iPad rendering, touch ergonomics or lifecycle behavior. An agent must complete this game's signing, installation and selected-device checks before claiming a tested game. The reference evidence does not qualify this proposed game; this example itself has not been built or tested.

```text
Task: adapt my existing Langmake game for the selected Apple device and prepare a playable test.

Engine source or iOS handoff directory: [ENGINE_DIRECTORY]
Existing game directory: [GAME_DIRECTORY]
Authorized Mac connection and workspace: [MAC_AND_WORKSPACE]
Apple bundle identifier: [MY_UNIQUE_BUNDLE_IDENTIFIER]
Signing team reference: [TEAM_ID_OR_EXISTING_XCODE_CONFIGURATION]
Device family: [IPHONE_OR_IPAD]
Presentation profile: [PHONE_OR_TABLET_PROFILE_NAME]
Test device identifier: [DEVICE_IDENTIFIER]
Register this device with that team if needed: [YES_OR_NO]
Orientation: [PORTRAIT_OR_LANDSCAPE]

If the phone is not yet available, the authorized milestone is a simulator
build and a precise handoff of the remaining signing and physical-device checks.
Do not silently substitute that milestone when I requested a tested device build.

I am the decision-making developer and primary gameplay playtester. Preserve
the game's design and existing Windows/Android controls. Adapt one short
playable area first, with readable device-appropriate UI and touch controls.

Read AGENTS.md, docs/23-ios-agent-guide.md and its ordered references. Inspect
the actual capabilities and iOS qualification record. Do not infer support
from roadmap text or treat a staged project as an installed game. Use the
standalone scripts/ios tools; Studio is optional.
Run context and read docs/26-game-workflow.md. Configure the Mac, device and
team for this game explicitly; do not inherit another game's local targets.
The native deploy facade can orchestrate those same standalone tools.
Keep all generated files and caches in the authorized workspaces. Do not discover other Macs, install
global tools, enroll an Apple account or publish the game.

Use the engine's current device listing to verify reality=physical for my device.
Treat pairing, reachability and Developer Mode as separate facts; Xcode may also
list paired simulators. A simulator record is not evidence that my device is connected.
For first-device provisioning use build --device with the returned ID; the tool
resolves the hardware UDID and chooses the Xcode scheme destination. Use
--register-device only if authorized above, together with --device and
--allow-provisioning-updates. Reuse an existing profile when already configured.

Use stable project and bundle identities so updates preserve progress.
Keep gameplay saves in declared engine state. Use named actions, touch controls
and dp UI dimensions with safe-area awareness.
Use the selected phone/tablet presentation profile, portable application metadata and an original
opaque 1024-pixel PNG application icon. App versions are independent of save
compatibility. Custom held controls use data-action with named game actions;
check release, drag-out and lifecycle cancellation, as well as the press.
Use ui-check and native captures for layout and glyph coverage. Avoid engine modifications
unless I separately authorize a missing engine capability.
For iPad use the same ios/device/arm64 target, explicitly select the iPad ID
and cook the tablet profile. Do not assume the app switches profiles by model,
reuse the prior iPhone ID, or treat nominal screen dimensions as safe bounds.
Verify actual viewport/density/safe-area observations. Treat windowed multitasking,
orientation changes and accessory input as separate qualification scopes.

Validate and check scripts, run a bounded deterministic scenario, then build,
sign, install and launch on the specified iPhone or iPad using the documented tools.
Keep native observations and matching capture sidecars. Verify rendering,
touch, audio, background/resume, restart and save-preserving app updates.
For each bounded launch, check the actual native tick, revision and capture
results; a successful device-launch response alone does not prove completion.
Separate simulator checks from physical-device evidence. Preserve any active
human play session. Report missing Mac/signing/device prerequisites precisely.
If signing needs the Apple account agreement, an Apple Development certificate
through Xcode's Manage Certificates, or a local codesign Keychain approval,
explain the actual error and retain approvals I already gave. The account
agreement is separate from Xcode's first-launch license. Passwords and private
keys stay on my Mac. Unsigned compilation does not complete phone installation.
Retain the recorded deployment operation and original error. If it supplies
a sign-local.sh helper, run that helper locally on the Mac to approve signing,
then resume the operation through the documented command. A verified candidate
can resume signing without a full compile; do not manufacture signing defaults.
Use the actual app path returned by the current tools (LangmakePlayer.app);
do not copy an older preview's executable or assume its package is current.

Give me the app artifact and bundle ID, exact launch instructions, controls,
changes, tested device/Xcode versions and known limits. Once technically
verified, ask me to try the game and assess controls, readability and sound.
Do not infer my gameplay approval from automated checks.
Generate HANDOFF.json with the current brief, notes and measured evidence.
```

For a new game, replace the existing-game instruction with a bounded concept and a new project directory. Supply signing references rather than passwords, certificates or private keys in a prompt.
