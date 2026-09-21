# Make the next Langmake game easier to create

Engine-developer handoff · September 20, 2026 · Based on VOIDRUNNER II across Windows, Android and iPhone

## Intended outcome

A human should be able to give a new external agent a creative brief, target platforms and permission to implement. The agent should discover the engine contract, configured build machines and deployment workflow from the workspace. The human should not need to repeat instructions about touch input, safe areas, app identities, signing, icons, save migrations, evidence collection or directory structure for every game.

This document proposes engine, tooling, template and documentation changes. It does **not** authorize their implementation, describe proposed APIs as existing features, or request an engine-owned AI connection. External agents remain responsible for authoring; humans remain the primary gameplay playtesters. Studio should be optional for every workflow described here.

The highest-value next milestone is a complete, repeatable mobile iteration: create a game, author styled controls, validate its layout, export an icon-bearing app, install it on a configured target, preserve progress, and hand it to the human—with no game-specific shell glue.

## What actually happened

| Observation | Consequence | Classification |
| --- | --- | --- |
| Android and iOS support changed while this task was active; old packages and newer source tools coexisted. | The agent had to rediscover which executable/player could perform each operation. | Discovery/versioning gap; stale assumptions also contributed. |
| The working Mac SSH configuration was stored in a task-local cache. Default keys failed until the engine agent supplied its path. | Several unnecessary connection attempts and user exchanges. | Setup handoff gap, not broken SSH. |
| Native mobile buttons supported holding, but custom RML buttons exposed click/change events. | A nicer Fire button became a toggle; the user wanted hold-to-fire. | Runtime input limitation plus an agent product decision that should not have been silent. |
| A desktop-shaped playfield, large side panel and fixed button row consumed the phone landscape screen. | The first Android iteration felt cramped and awkward. | Game layout failure; platform-aware previews and better defaults could catch it earlier. |
| Android adaptations became a copied project graph. | Shared mechanics, platform UI and save migration changes could drift between copies. | Authoring workflow gap. |
| Easy and Normal still felt too hard after technical checks passed. | Further human-directed tuning was necessary. | Creative iteration, not a defect that automated tests can settle. |
| Sparse font coverage rendered some decorative punctuation as question marks. | Multiple packaging iterations for tiny UI fixes. | Authoring error; validation could detect it. |
| iOS compilation succeeded, but SSH signing could not access the Keychain private key. | A local signing helper and human approval were needed. | Expected Apple interaction boundary; recovery tooling was insufficient. |
| The first generated shell helper had CRLF line endings. | macOS rejected `/bin/sh^M` before signing could run. | Agent scripting error; a tested built-in helper would eliminate it. |
| The installed iPhone app had no game icon. | A game-specific script generated an asset catalog, merged plist metadata, incremented the build version and re-signed a copied app. | Export completeness gap; useful workaround, poor default workflow. |
| A native receipt poll treated missing-file output as JSON. Large simulation JSON also flooded command output. | Avoidable harness failure and noisy investigation. | Transport ergonomics plus agent error. |
| Every content revision required save-compatibility assessment, including presentation edits. | Per-revision migration branches accumulated in game code. | Necessary safety with excessive routine overhead. |

The engine already provides valuable foundations: text-authored assets, validation, offline Luau checking, simulation, revisioned content, fixed-tick captures with sidecars, declared saves and native device tooling. Build on these instead of replacing them with browser wrappers or autonomous UI playthroughs.

## Priority and ownership

P0 means recurring blockers worth addressing before the next cross-platform game. P1 improves everyday authoring. P2 is useful after the workflow is reliable.

| Priority | Deliverable | Primary owner | Done when |
| --- | --- | --- | --- |
| P0 | Discoverable workspace and target configuration | CLI/tools/docs | A fresh agent finds the correct runtime, SSH config, Mac root and explicit phone without chat history. |
| P0 | Styled controls with native action semantics | Runtime/UI | Custom Fire supports real hold/release with simultaneous movement and no stuck input. |
| P0 | Resumable mobile export, signing and deployment | Platform tools | Signing approval resumes the same verified build; no custom shell helper or full rebuild is needed. |
| P0 | App identity and icon export | Schema/exporters | A new mobile game installs with its own icon and preserves identity on update. |
| P1 | One source graph with target-specific presentation | Loader/cooker/templates | Mobile layout changes do not require copying gameplay and assets. |
| P1 | Responsive UI and viewport diagnostics | UI/renderer/CLI | Agents can inspect the actual playable viewport, touch bounds and safe-area layout before deploying. |
| P1 | Verification runner and save-compatibility workflow | CLI/runtime | Routine scenarios, receipts and migration checks require declarative inputs rather than bespoke scripts. |
| P1 | Portable onboarding and automatic handoff record | Docs/CLI/templates | Another agent can continue from a short checked-in record without reading the conversation. |
| P2 | Reusable HUD, pixel UI and balance examples | Optional template library | Examples accelerate work without imposing VOIDRUNNER's genre or visual style. |

## 1. Make setup discoverable and machine-readable

Provide one stable entry point that reports the workspace, selected executable, engine/tool versions, actual capabilities, relevant documentation paths, project identity, installed targets and prerequisite status. Existing `capabilities`, `schema`, `script-api` and platform doctors should feed it; avoid a second inconsistent capability database.

Separate three kinds of information:

- **Game settings:** project ID, bundle/application IDs, display name, icon source, orientation, target presentation and save contract. Portable with the game.
- **Machine configuration:** SDK locations, authorized Mac host alias, SSH configuration reference, workspace root, signing-team reference and device aliases. Local and excluded from source distributions by default.
- **Operation evidence:** resolved physical device, input revision, artifact hash, current step, result paths and blocking reason. Generated per operation.

Do not store private keys, passwords, Apple tokens or license acceptance in game manifests. A configured machine is discoverable context, not blanket authorization to deploy. Preserve explicit user authorization for the host/directory/device; ask only for a genuinely missing choice or required human interaction. Never infer a physical phone from a simulator's paired status.

In the verified workflow, the successful handoff was an existing workspace-local SSH **configuration reference**, not a new credential. Host/account names, addresses and remote workspace paths belong in ignored per-game target configuration, not in public documentation or engine defaults.

Acceptance: a new agent with no conversation history discovers a valid configured target, distinguishes missing SSH configuration from rejected credentials, and does not search unrelated keys or ask the human to recreate working access. Source and portable installations expose the same discovery contract.

## 2. Keep input semantics independent of appearance

Give authored UI elements a supported binding to the same named actions used by keyboard, gamepad and native mobile buttons. Define held value, press edge, release edge and cancellation. Merely emitting a click on release is insufficient for weapons, dragging, charging, steering or sustained interaction.

Prefer a unified action-binding path over asking every game to emulate input through Luau-local flags. Rendering should allow custom images or native text/vector/pixel artwork, opacity, anchors, hit regions and pressed/cooldown states without changing the action's semantics.

Specify pointer ownership: a movement finger must remain a movement finger when another finger presses Fire. Define drag-out/re-entry, pointer cancellation, app backgrounding, UI disappearance, focus loss and save/load behavior. Held touches should not be resurrected from a save. Decorative descendants should route to the intended parent control without consuming unrelated input.

Support a comfortable invisible hit region larger than the visual icon, separate from opacity. Keep transparency independent of hit testing. Do not add an automatic “make transparent behind enemies” system before simple authored opacity and visibility states work well.

Acceptance: on physical Android and iPhone hardware, hold a custom Fire button while moving, release it, drag off it, open pause, and background the app. Fire must stop at the specified boundary every time. Keyboard/gamepad bindings and headless action timelines should produce equivalent gameplay transitions. A new agent should never have to change hold-to-fire into a toggle just to restyle a button.

## 3. Make export a complete, resumable product operation

Expose a persistent operation record covering prerequisite checks, staging, transfer, native compilation, signing, artifact verification, installation, launch and observation. Each phase needs an explicit result—not one ambiguous `ok` for the entire process.

Useful machine-readable blocking reasons include missing SDK, unauthorized device, ambiguous target, Apple agreement required, Keychain interaction required, signing identity unavailable, and installed/running revision mismatch. Report the exact next step and which work is already complete.

After a Keychain prompt, resume signing the existing artifact and finish its report. Do not require a new full compile or hand-authored export script. Generate any local helper with tested LF bytes, safe paths and the configured signing identity. Never bypass Keychain protections or solicit passwords in chat. Distinguish detection from diagnosis: `errSecInternalComponent` alone does not prove every failure has the same cause.

Cache native builds by engine/toolchain/configuration/ABI inputs and content staging by content identity. A UI label or app icon change should reuse valid native compilation. Every final artifact must still verify its actual bundled content, signature and revision. Preserve original build evidence rather than marking a failed signing attempt as successful after the fact.

Before a potentially disruptive update, preserve the authorized app's save/recovery state using a supported receipt-based operation. Reinstall under the same identity and key; never uninstall to bypass conflicts. Make the selected human/test app and whether launch will restart it explicit.

Acceptance: deliberately block signing after successful compilation, approve locally, then resume to a verified installed app without rebuilding unchanged native code. Repeat after an icon-only change. Confirm that progress survives and no unrelated app/device is touched.

## 4. Treat icons and app identity as first-class game metadata

Support a game-owned source icon plus platform-specific overrides when needed. Validate supported format, size, color/alpha requirements and source existence before an expensive build. Keep original artwork in the game project.

Exporters should generate the required iOS asset catalog/plist integration, Android launcher resources and appropriate desktop packaging resources. Preserve each platform's identity and signing configuration. A missing icon should be an actionable warning or release-profile error, not an unexpected blank icon after deployment.

Track application build versions separately from gameplay save schema. Include icon/resource fingerprints in the final packaging report even when gameplay revision is unchanged. Asset generation must happen before signing; no agent should patch a signed bundle without a supported re-sign workflow.

Acceptance: export the same new game to Android and iOS, inspect both installed icons, replace the icon, then update in place without losing saves. Verify the installed package metadata—not just the source PNG. Keep store publishing separate from sideload deployment.

## 5. Share gameplay while adapting presentation

Provide supported target profiles/overrides for input layout, UI documents/styles, camera/viewport policy, orientation and package metadata. Establish deterministic merge rules and include the resolved configuration in cook/inspection output. Avoid unrestricted platform forks as the standard workflow.

Layout adaptation is not simply enlarging the background. VOIDRUNNER needed an actual wider playable arena, a recentered camera and reduced HUD occupancy. Some games should expand their arena; others must preserve a fixed playfield to maintain mechanics. Expose that choice clearly rather than silently stretching sprites, changing collision geometry or widening encounters.

Let the game query a stable viewport/safe-area contract. Supply phone/tablet/desktop preview profiles with density, aspect, cutouts and orientation. Report physical and logical UI bounds, touch bounds and the rendered world region. Warn about offscreen interactive elements, unintended overlap and configured controls with no corresponding action.

Add font coverage checks over authored UI strings and allow a declared expected character set for dynamic text. Provide supported pixel-safe styling examples with fallback glyphs. Do not imply RML/RCSS supports arbitrary browser CSS.

Acceptance: one source graph runs at desktop, wide landscape phone and portrait/tablet reference sizes; essential controls remain reachable, sprites keep intended proportions, and the resolved gameplay geometry follows the declared policy. Missing UI glyphs produce a useful diagnostic before packaging.

## 6. Package verification as reusable tooling

Retain the existing validation, Luau checks, input timelines, captures and sidecars. Add a runner that executes named project scenarios, collects bounded output and checks authored assertions against declared state. Provide concise summaries by default and file paths for full logs/state.

Unify native transport behavior while preserving platform-specific implementations. Deliver a command, wait for its receipt, validate session/revision/request ID, then fetch results with a bounded timeout. “Uploaded” must never mean “executed.” Handle absent files and malformed responses explicitly; do not automatically replay a mutation with a new ID because its receipt is delayed.

Support capture comparison against host state with declared numeric tolerances. Keep immutable run IDs, artifacts and sidecars together. Provide distinct statuses for host-tested, simulator-tested, physically rendered, physically input-tested and human-playtested. Audio counters establish processing, not audible output; timeline input establishes mechanics, not multitouch comfort.

Verification should use an isolated app identity or protected session when a human build is active. Hand off ordinary player mode only after relevant technical checks; do not continue resetting or injecting input into the human's session.

Acceptance: an agent runs a scenario and obtains a compact report with images, state assertions and failures. It needs no custom receipt-polling Python, no entire-state console dump, and cannot accidentally label simulator evidence as physical-device verification.

## 7. Reduce routine save-migration overhead without weakening compatibility

Separate game identity, artifact/content revision, state schema and explicitly declared save compatibility. UI colors, icon metadata and gameplay-state structure have different compatibility implications; a single changed source hash should not force a growing list of identical passthrough branches without useful analysis.

Provide a conservative compatibility report and a supported way to declare an assessed compatible change. Require migration for structural differences; never infer safety solely from unchanged `save_version`. Restored entities, camera, scene states and UI bindings also matter.

Create fixtures for repeated restore, old-save migration and update-in-place. Verify that initialization rewards are not replayed, increased capacities are applied once, and failed migrations preserve original bytes. Make manual slots versus automatic platform recovery visible in CLI output. Export/import across platforms should be explicit and compatibility-checked; do not assume Android saves automatically transfer to iPhone.

Acceptance: an assessed presentation-only update continues safely without a new ad hoc revision branch; incompatible state changes fail clearly. Loading the same migrated save twice cannot duplicate research, rewards or health bonuses.

## 8. Supply reusable starting points, not another giant prompt

Keep `AGENTS.md` short: architecture boundaries, authorization, human playtesting, discovery entry point and where to read deeper. Select documentation by installed capability and requested task. Platform guides remain authoritative details, but a new game agent should not need every platform's entire guide before creating a first prototype.

Extend starter projects with optional examples for persistent progress, pause/save/continue, action-bound mobile controls, sound buses, safe-area HUDs and an icon placeholder. Include a small verification scenario and a handoff manifest. Keep shooter-specific difficulty curves, campaigns and upgrade systems out of universal defaults.

Useful optional UI components include meters with numeric readouts, cooldown controls, pixel-panel/button skins and progression indicators. Progress semantics belong to the game: approach completion is not boss defeat, and a full research bar may still require a level gate. Examples should show honest labels and not silently claim completion.

For balance, offer tunable data and telemetry examples—damage, survival time, pickup cadence, completion and failure counts. The lesson is to make human-directed tuning quick, not to have the engine decide what “Easy” should feel like.

Generate a concise per-game handoff record: brief and accepted feedback, source/target profiles, identities, save contract, current artifact/revision, target setup references, verification results, pending human actions and known limitations. Separate local machine references from portable project information. A second agent should not reconstruct state from task-local scripts or conversational memory.

## Proposed interface shape—not existing commands

The following illustrates the desired workflow only. Do not copy these commands into current task instructions until implemented, tested and documented:

```text
langmake context --project games/new-game --json
langmake target list --json
langmake new --template shared-2d --output games/new-game
langmake verify --project games/new-game --scenario smoke --target phone-preview
langmake deploy --project games/new-game --target configured-iphone
langmake operation resume OPERATION_ID
langmake handoff --project games/new-game
```

A target facade can delegate to the existing standalone Python tools. Do not make Studio, a model provider or a network service a prerequisite. Preserve direct platform commands for diagnosis and automation.

## Delivery order and release acceptance

1. **Discovery and handoff:** make configured environments discoverable, version capability output, add concise operation reports. Validate with a fresh agent that lacks this conversation.
2. **Mobile input and packaging:** implement custom action-bound controls, first-class icons and resumable signing. Exercise hold/cancel/multitouch and update-in-place on both real phone platforms.
3. **Authoring and verification:** add target profiles, layout/font diagnostics, scenario tooling and compatibility fixtures. Prove one source graph survives several presentation and balance iterations.
4. **Templates and documentation:** update affected prompts and source/portable discovery links together; regenerate distribution copies from canonical sources. Record documentation review separately from actual platform qualification.

Measure improvement using a new, unrelated small game. Record setup questions, manual commands, custom helper scripts, time to first playable build, and time for a UI/icon-only update. The target is **zero custom signing/icon/receipt scripts**, **no repeated request for already configured connection details**, and **no loss of controls or progress during routine presentation changes**. Hardware approvals and creative questions remain legitimate human interactions.

After this work, an adequate human brief should look like:

> Create [game idea] in its own project folder. Target Windows and my configured landscape phones. Use the workspace's current Langmake workflow, preserve progress across updates, verify technically and give me a playable build. Do not change the engine. Ask me about creative decisions or genuinely missing prerequisites, not setup already recorded in the workspace.

Until then, use the current guides and actual command capabilities. This proposal must not become another list of unsupported promises pasted into game prompts.

## Evidence and implementation starting points

- Current contracts: [agent API](../08-agent-api.md), [runtime guide](../13-runtime-guide.md), [Android workflow](../21-android-agent-guide.md), [iOS workflow](../23-ios-agent-guide.md), [iOS qualification](../24-ios-qualification.md).
- Onboarding: [prompt index and maintenance checklist](../../prompts/README.md), [provider handoff](../../prompts/10-provider-handoff.md).
- Input implementation reviewed: [UI events](../../src/ui.cpp) and [native mobile controls](../../src/android_input.cpp). At review time, authored UI listeners covered click/change; native mobile controls tracked held fingers separately.
- Platform entry points: [Android tooling](../../scripts/android/README.md), [iOS tooling](../../scripts/ios/README.md). Reuse their supported transport and physical-device classification.
- Project history: [Android adaptation notes](../../games/voidrunner-2/ANDROID-PORT-PLAN.md), [iPhone deployment record](../../games/voidrunner-2/ios/DEPLOYMENT.md).
- Android evidence: `games/voidrunner-2/android/evidence/layout2/`, `balance-ui/`, and `device-verification-final.json` under that evidence directory.
- iPhone evidence: `games/voidrunner-2/ios/evidence/verification.json`, three native capture/sidecar pairs and `player-live.json`. Tick 180 declared gameplay state matched host simulation; this was not a physical multitouch test.
- Icon workaround to replace with supported export: [game-local packaging script](../../games/voidrunner-2/ios/icon/package-icon.py), LF-safe signing helper beside it, and `install-report.json`. Installation succeeded; home-screen appearance was left for human inspection.

Review these findings against the latest implementation before scheduling work. Features and qualification may have advanced since this task; preserve the evidence boundary rather than treating historical limitations as permanent.
