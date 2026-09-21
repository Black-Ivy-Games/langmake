# Prompt examples for humans and AI agents

Use these structures to turn a game idea into a clear, playable iteration. They are inspiration, not a required language or a limit on your creativity. A short ordinary-language request is enough when the surrounding conversation already supplies the context. Agents may help a human refine a brief without taking over their creative decisions.

These are **authoring briefs**, not additional bundled games or evidence that the games described have been built. The executable's discovery commands and runtime guide remain the authority on support.

## Choose a starting point

| Situation | Starting document |
| --- | --- |
| Understand the shared human/agent workflow | [Working agreement](00-workflow.md) |
| Set up one game's targets, profiles, icons and repeatable checks | [Game workflow](../docs/26-game-workflow.md) |
| Describe your own first playable game | [Fill-in structure](01-new-game-template.md) |
| See a small, concrete 2D prompt | [Star Courier](02-star-courier.md) |
| See a bounded 3D adventure prompt | [The Sleeping Lighthouse](03-sleeping-lighthouse.md) |
| See a small group-strategy prompt | [Tidepool Couriers](04-tidepool-couriers.md) |
| Turn human playtest feedback into changes | [Feedback and iteration](05-feedback-iteration.md) |
| Ask for a focused defect investigation | [Bug report and repair](06-bug-report.md) |
| Commission native text-authored art or music | [Asset creation](07-assets.md) |
| Discuss a larger idea before authorizing implementation | [Plan only](08-plan-only.md) |
| Follow Android instructions and reading order | [Android workflow](../docs/21-android-agent-guide.md) |
| Author one shared Windows/Android game | [Android game](09-android-game.md) |
| Prepare an iPhone/iPad port with explicit Apple prerequisites | [iOS game](11-ios-game.md) and [qualification](../docs/24-ios-qualification.md) |
| Continue with another provider or external agent | [Provider handoff](10-provider-handoff.md) |

Copy the text block from one document. Replace bracketed placeholders, remove optional sections you do not need, and send the resulting prompt to your agent. Do not paste the whole library as one task. Examples provide structure; your own direction takes precedence over their choices of genre, tone and mechanics.

`[ENGINE_PACKAGE_DIRECTORY]` means the folder containing `langmake.exe`, `AGENTS.md` and these `prompts/` files. Use the actual extracted location when sharing the package; the 0.5.0 package destination is `dist/Langmake-0.5.0`. `[NEW_PROJECT_DIRECTORY]` must be a new directory inside the workspace you authorize. For source-checkout work, specify the checkout and its actual build executable instead, and use the documentation paths below. Resolve material missing paths before writing; never create a directory literally named after a placeholder. Studio can supply the selected project and engine paths; the same briefs work in external coding-agent applications.

## The useful structure

A good brief connects **the intended experience**, **what the player does**, **the limits of this iteration**, and **how it will be checked**. Describe outcomes before implementation details. Give an explicit task mode: discuss, plan, implement, or repair. Identify which decisions the human has already made and which routine details the agent may choose.

Keep technical acceptance separate from creative acceptance. “All five cells can be collected and a new attempt resets progress” is technically testable. “The ship feels pleasant to steer” needs human playtesting. Include both, with an exact launch command and controls in the handoff.

Use current capabilities for the first milestone. A larger ambition can be recorded as future direction without authorizing all of it at once. If a requested capability is absent, the agent should explain the gap and offer an achievable scope or a separate engine proposal; it should not claim a roadmap feature exists.

## Compatibility and maintenance

**Last reviewed:** September 21, 2026, against Langmake 0.5.0 discovery, workflow tools, runtime documentation and bundled starter layouts. The prompt texts have been reviewed for supported scope; these proposed games have not been executed as tests. Engine release evidence lives separately.

September 21, 2026 shaped-2D review: reviewed new-game, 2D, feedback, defect, asset and platform examples for optional bounded `physics2d` fixtures and rotation. Existing prompt scopes remain valid; the runtime guide documents authoring. This iteration has PC evidence only. Portable copies are generated from canonical documentation during packaging.

0.5.0 session review, September 20, 2026: reviewed all prompt structures for deployed-artifact relaunch and source/target provenance. Updated the shared agreement for `session launch`, ended scenario sessions and game-local single-object receipt input files. Existing Android, iPhone and provider-handoff briefs retain their explicit setup and human-playtesting requirements. This is documentation maintenance, not new device or creative qualification.

0.5.0 workflow review, September 20, 2026: updated the shared agreement, new-game, Android, iPhone and provider-handoff briefs for project context, explicit per-game targets, presentation profiles, held custom UI actions, portable application icons/versions, resumable operations, named verification scenarios and portable handoffs. Reviewed the remaining creative, feedback, defect, asset and plan-only examples; their bounded scope remains valid. `workflow-2d` is a separate executable starter, not proof that these prompt examples have been implemented. Historical platform measurements remain historical; consult the current release evidence for new behavior.

0.5.0 iPad workflow review, September 20, 2026: expanded the Apple brief and discovery links to include explicit iPad selection, tablet profiles, device registration and actual viewport/safe-area checks. The native universal-app configuration and host classification tests are separate from physical iPad evidence. Historical iPhone/simulator checks do not qualify iPad touch, windowed multitasking or this unbuilt prompt example; the current release record remains the authority for executed checks.

0.4.1 iOS simulator-progress review, September 20, 2026: reviewed the iPhone brief and discovery guidance against the actual Mac toolchain, renamed `LangmakePlayer.app` product and confirmed 2D/3D Metal simulator scenarios. The prompt explicitly distinguishes simulator evidence from signing, physical controls/sound, lifecycle checks and human iPhone feedback. This is a prompt maintenance review; the proposed example game has not been built or qualified. Engine measurements are recorded separately in [iOS qualification](../docs/24-ios-qualification.md).

0.4.1 iOS device-discovery review, September 20, 2026: updated the iPhone brief for Xcode's mixed physical/simulator listings, separate hardware reality/pairing/reachability, and physical-action guards. It also preserves earlier human agreement approvals and distinguishes unsigned compilation from signed installation. Simulator UIKit recovery evidence and a newly detected physical phone do not qualify this proposed game; this entry records prompt maintenance only.

0.4.1 first-phone provisioning review, September 20, 2026: the iPhone brief now makes device registration scope explicit, uses `build --device` for the verified Xcode scheme destination, and separates existing-profile builds from `--register-device --allow-provisioning-updates`. It distinguishes Xcode setup, Apple's account agreement, certificate selection and local Keychain approval without requesting passwords or private keys. This is documentation review, not proof of a signed or installed prompt example.

0.4.1 initial phone-run review, September 20, 2026: updated the iPhone brief for nine successful reference-app phone captures after the launch-argument fix and the human's positive feedback on movement, interaction, music and text. The brief requires checking actual native ticks, captures and revision after a bounded request. Reference evidence and initial feedback do not qualify this proposed game or establish full creative/release acceptance; this entry records prompt maintenance only.

0.4.1 Mac setup review, September 20, 2026: reviewed the iPhone and provider-handoff briefs against actual SSH workspace setup. Their scope remains valid. The iOS guide and doctor now explicitly distinguish completed Xcode first-launch setup, visible SDKs and an installed simulator runtime. This prerequisite check does not qualify an iOS build or device.

0.4.1 iOS source-work review, September 19, 2026: reviewed shared workflow, Android and provider handoff assumptions; added a bounded iPhone-port brief requiring an authorized Mac/Xcode workspace, explicit signing/device identity and actual Apple qualification. Existing Android/Windows briefs retain their scope. The new brief is not an executed iOS game or a claim of native build success.

0.4.1 Android agent-guide review, September 19, 2026: reviewed the shared workflow and Android/provider-handoff briefs. Added a required starting guide with ordered references, exact Android transport/timing differences and human/Studio handoff. Prompt examples remain inspiration, not an executed game or new device qualification.

0.4.1 Android review, September 19, 2026: reviewed the Android, asset, feedback and provider-handoff briefs. Added portable player selection, ABI/orientation and density-independent UI guidance. External agents remain supported, and device evidence and human feedback stay distinct. Studio exports use unique APK files per iteration. This is a documentation review; the sample prompt itself was not built as a game.

0.4 implementation review, September 18, 2026: added optional Studio/external-agent handoff and Android briefs. Existing Windows game, asset, feedback and bug structures retain their scope. Android briefs require target prerequisites and separate emulator/device evidence; they do not claim every phone has been qualified. See the current release evidence before choosing a platform. This documentation review does not constitute building the proposed games.

September 18, 2026 repair review (0.3.0): reviewed the shared workflow, feedback and defect briefs for the player-mode Escape fix. Their scope and commands remain valid. Escape now reaches the existing exit handler in `play` as well as `preview`, after focused UI cancellation; this restores the documented behavior. No new platform or gameplay qualification is implied, and no portable release was generated for this local game iteration.

| Authority | Source checkout | Portable package |
| --- | --- | --- |
| Agent workflow and commands | `docs/08-agent-api.md` | `AGENT-API.md` |
| Project setup and repeatable verification | `docs/26-game-workflow.md`, `docs/25-workflow-implementation.md` | `GAME-WORKFLOW.md`, `WORKFLOW-PROTOCOL.md` |
| Runtime systems and limits | `docs/13-runtime-guide.md` | `RUNTIME-GUIDE.md` |
| Native assets | `docs/10-text-assets.md` | `TEXT-ASSETS.md` |
| Executed release evidence | `docs/releases/0.5.0-evidence.json`, `docs/20-android-qualification.md`, `docs/24-ios-qualification.md` | `api/release-evidence.json`, `ANDROID-QUALIFICATION.md`, `IOS-QUALIFICATION.md` |
| Studio and provider handoff | `docs/17-studio.md` | `STUDIO.md` |
| Android agent workflow and reading order | `docs/21-android-agent-guide.md` | `ANDROID-AGENT-GUIDE.md` |
| Android prerequisites and authoring | `docs/18-android.md` | `ANDROID.md` |
| iOS workflow and qualification | `docs/23-ios-agent-guide.md`, `docs/24-ios-qualification.md` | `IOS-AGENT-GUIDE.md`, `IOS-QUALIFICATION.md` in a package that includes iOS tooling; absent from historical 0.4.1 packages |
| Machine-readable support | `build/langmake.exe capabilities`, `schema`, `script-api`, `asset schema --type TYPE` | The same commands on `langmake.exe` |

When engine behavior, commands, templates, assets, saves, platform qualification or the human/agent workflow changes:

1. Review the affected prompts and shared agreement in the same change. Rewrite misleading assumptions; retain valid examples without adding features merely because they exist.
2. Compare commands and support claims with executable discovery, the runtime guide and current starter files. If a new brief relies on an unqualified feature, label it as a proposal or choose supported scope.
3. Check the technical acceptance points still describe observable behavior. Do not rename a documentation review as an executed gameplay test.
4. Keep discovery links in the root README, AGENTS instructions, agent guide and game-creation brief. Package these canonical files from `prompts/`; do not maintain separate hand-edited copies in `dist/`.
5. Update this review date/version when that review is performed, regenerate the portable documentation/archive when distributing a release, and verify the packaged links and manifest. Keep historical test results and new documentation-only verification distinct.

Future agents working on the engine are instructed to do this in the source `AGENTS.md`. This is part of the change/release workflow, not a background service or a promise that old extracted copies update themselves.

September 20, 2026 receipt review (0.5.0): reviewed the shared workflow, mobile and defect prompts after mobile copy-acknowledgment recovery and verified missing-file handling. Existing examples remain valid: native receipts establish execution, and uncertain delivery must not cause duplicate mutations. The workflow now observes the original request ID after a lost copy acknowledgment and preserves the warning when a matching receipt resolves it. Optional iOS recovery files are skipped only after the native tool verifies absence; transport failures and malformed saves remain errors. This review adds no human gameplay acceptance.

September 21, 2026 touch-release review (0.5.0 source): reviewed the shared workflow and Android game brief for deferred touch pointer coordinates. Added fast tap-position verification to the Android brief. The fix preserves release coordinates for the consuming fixed tick and clears them on consumption/reset. This is prompt maintenance, not new qualification of historical distributions or the example game.
