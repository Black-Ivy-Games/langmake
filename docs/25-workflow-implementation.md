# Repeatable game workflow implementation

Authorized September 20, 2026. This implements the lessons in the agent workflow proposal; this file records the plan, not qualification claims.

## Configuration boundary

Every game explicitly configures its own local build targets. A clone or a new game has no SSH host, signing team, device, or deployment authorization. Targets live in that game's ignored `.langmake/targets.local.json`, bound to the local machine and project directory. Reusing a person's existing SSH configuration is allowed only when explicitly selected for that game. Private keys, machine addresses and device IDs never become starter or engine defaults. Portable `project.json` metadata describes the game, not a person's hardware.

## Delivery phases

1. Establish portable application/profile/scenario/save contracts and per-game local target configuration. Provide context, target discovery and portable handoff commands using existing capability/doctor output.
2. Add styled named-action controls with pointer ownership and cancellation, plus viewport, hit-region and font diagnostics. Preserve existing click/change and native controls.
3. Add icons/application versions and resumable platform builds/signing with reusable compilation, explicit operation state and integrity checks. Keep standalone platform tools usable without Studio.
4. Add named scenario verification, bounded receipts and state assertions, target presentation profiles and assessed save compatibility. Preserve strict behavior for projects without the new opt-ins.
5. Add a small reusable workflow starter, cross-platform regression cases and readable agent/human instructions. Test the source workflow, regenerate separate distributions, and exercise fresh-agent authoring against the packaged contract.

## Verification gates

- Existing project and runtime tests remain passing; old projects need no edits.
- New tests cover per-game target isolation, copied-config rejection, no implicit SSH defaults, source/portable discovery and content exclusions.
- Held controls cover independent fingers, edges, drag/cancel, focus/background, hidden controls and restored state.
- Profile/source tracking, glyph diagnostics, layout bounds and repeated save restore are exercised with native tests and captures.
- Platform operation tests preserve successful compile output across signing failures, verify new icon resources and reject altered/stale artifacts.
- Native scenarios and receipt handling have bounded output/time, session/revision checks and no automatic mutation replay.
- Real device evidence and human feedback remain distinct from host/simulator tests. Existing human game sessions are protected.
- Packages have verified manifests and no local target settings, SSH references, private games or credentials.

Final status and measured limitations will be recorded in release evidence after integration. No optional library, provider SDK or desktop UI becomes a runtime requirement.

## Completed implementation

All five implementation phases are complete. Windows regression, native Android/iPhone/iPad scenarios, Android/iPad update preservation and fresh packaged authoring have executed. See [0.5.0 qualification](27-workflow-qualification.md) for exact results, distribution verification and outstanding human feedback. Each game still configures its own Mac, SSH reference, signing team and devices explicitly.
