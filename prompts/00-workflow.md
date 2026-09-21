# Shared working agreement

The examples reference this agreement so each brief can focus on the game. It defines how to carry out an authorized task; reading it or discussing an example does not itself authorize implementation. Use the human's current request and prior decisions to determine the scope.

## Roles and scope

The human is the creative decision-maker and primary gameplay playtester. The agent is a worker developer: implement authorized ideas, author assets, investigate defects and perform technical verification. Suggest options where helpful, but retain the human's priorities. Complete routine authorized work without asking for approval at every step.

Resolve meaningful missing context early. A proposed game, future wish or plan-only request is not permission to create it. An implementation brief authorizes its playable milestone, not every idea mentioned as future direction. Treat example settings and numbers as editable starting choices.

## Start from the actual installation

Read its `AGENTS.md` and agent guide. Discover commands and supported systems with `capabilities`, `schema`, `script-api` and the asset schemas you need. Use the runtime/asset guides for contracts; wider roadmap documents include future work. The engine is independent of the agent's model, provider and conversation UI. For an Android target, read the [Android agent workflow](../docs/21-android-agent-guide.md) and its ordered references before authoring.

For a new game, select a suitable installed starter and use `new` with a nonexistent destination. For an existing project, inspect and continue it. Keep game files, generated assets, saves and evidence inside the authorized workspace. Preserve bundled examples and existing human work. An ordinary game brief does not authorize modifications to the engine or global tool installations.

Run `context --project GAME` and read [the game workflow](../docs/26-game-workflow.md). Inspect `HANDOFF.json` when present. Configure targets explicitly for this game and machine; never inherit another game's SSH host, phone, signing team or application identity from memory. Keep portable application metadata, source and scenarios in the project; local target bindings and operation records belong under `.langmake/`. `workflow-2d` demonstrates the structure. Use presentation profiles for phone/tablet UI while retaining shared gameplay.

For an iPhone or iPad target, read the [iOS guide](../docs/23-ios-agent-guide.md) and [qualification record](../docs/24-ios-qualification.md). Establish the authorized Mac workspace, Xcode/signing prerequisites, explicit simulator or device, and selected phone/tablet profile. Source staging, native compilation, simulator evidence and each physical-device qualification are separate outcomes; report each accurately.

Use the native text asset pipeline where requested. External assets require a clear source, compatible reuse terms and recorded attribution; do not assume that an image or song found online is reusable. Keep asset dependencies in the registered source graph. Avoid unnecessary external creative applications for assets the native pipeline can make.

## Implement and verify

Keep the current iteration small enough to finish. Use registered modules, definitions and declared state for systems that need observation or persistence. A restart is game behavior: implement a fresh attempt in player mode rather than relying on preview-only reset shortcuts. If content changes affect saves, follow the documented compatibility/migration rules.

Run validation and offline script checking. Use bounded deterministic input scenarios to verify the behaviors changed, with native inspection and exact-tick captures where useful. Inspect relevant images and their state sidecars. For a game using persistence, check a fresh-process load and progression without duplicate rewards. Keep result logs, screenshots and save artifacts at distinct paths. Do not assume a copied starter includes its unregistered test timeline; author or explicitly copy a scenario and adapt it to the game.

Register reusable scenarios and run `verify` against the selected profile. Use `ui-check` for viewport bounds, interactive overlaps and glyph coverage, then inspect native captures. Custom held controls use `data-action` bound to a named action; check press, release, drag-out and lifecycle cancellation. Keep application versions separate from save compatibility. Presentation-only save compatibility is explicit and still requires unchanged gameplay fingerprints plus fresh-process restoration checks. Use recorded operation status and `operation resume` for a blocked deployment instead of rebuilding blindly.

For an already deployed mobile game, use `session launch` to reopen the matching artifact without a new build. A bounded native scenario ends its session; relaunch a debug verification target with `--development --fresh` before live stepping. `receipt --input` names a game-local file containing one input object, not inline JSON or a timeline. Store transient requests under `.langmake/`. Current source/profile, runtime and target fingerprints must match deployment evidence; authored edits require another deployment before live verification.

Prefer native checks. Computer-use gameplay is supplementary when requested or needed for a specific behavior native tools cannot verify; it is not the default replacement for human playtesting. Check scope-appropriate failure cases and recovery. Report a blocked feature or unperformed check honestly instead of silently substituting a different game.

Protect an active human play session. Coordinate source edits/restarts when necessary and use isolated technical sessions. Do not reset, reload, pause or inject input into the human's game without authorization.

## Hand over the iteration

Provide the exact player launch command, controls, objective, changes, technical results and known issues. Hand over a fresh playable state or clearly explain a supplied save. Separate measured facts from expectations. Ask two or three focused questions about the experience. Say “ready for human playtesting” when technical checks establish that status; only the human can supply creative approval.

Generate `handoff --project GAME` with project-relative brief and notes references. Preserve the actual technical evidence and unresolved human feedback there so a different agent can continue without private conversation history.

If the next creative decision depends on playtest feedback, wait for that decision while continuing independent work already authorized. Do not invent feedback or automatically expand the agreed milestone.

Return to the [prompt library](README.md) or use the [new-game structure](01-new-game-template.md).
