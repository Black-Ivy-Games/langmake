# MATTERFALL — ready-to-use Langmake game brief

You are authorized to build a new, original game using the existing Langmake engine. Working title: **MATTERFALL: From Atoms to Life**. Create it in its own fresh directory, `games/matterfall`, inside `.`. If that directory already contains work, inspect it and resolve the conflict instead of overwriting it. Do not modify VOIDRUNNER or another game. Do not change the engine.

Deliver a polished, playable 2D physics merge demo for Windows PC and mobile, with shared gameplay and platform-appropriate presentation. The player drops matter into a container, combines matching objects, discovers increasingly complex forms, expands the container, and ultimately creates a tardigrade. The human is the game designer and primary playtester. Complete the authorized implementation autonomously; ask only about material ambiguities, missing target authorization, or genuine engine blockers.

## Read and discover before implementation

Read the workspace `AGENTS.md`, `README.md`, and these documents, following their current contracts rather than remembered behavior from older engine versions:

1. `docs/08-agent-api.md` — human-directed workflow and native agent API.
2. `docs/26-game-workflow.md` — context discovery, target configuration, shared profiles, custom held actions, icons, verification, resumable operations and handoff.
3. `docs/13-runtime-guide.md` — entities, physics, collisions, prefabs, input/UI, viewports, declared state and saves.
4. `docs/10-text-assets.md` — native text-authored sprite art, animation, music and sound effects.
5. `examples/workflow-2d/README.md` — a working example of the newer workflow. Use its infrastructure, not its genre or branding.
6. For Android, read `docs/21-android-agent-guide.md`, `docs/18-android.md`, `docs/20-android-qualification.md`, and `scripts/android/README.md`.
7. For iPhone/iPad, read `docs/23-ios-agent-guide.md`, `docs/24-ios-qualification.md`, and `scripts/ios/README.md`. iPad needs its own explicitly selected presentation profile and verification.
8. Use `prompts/README.md` and `prompts/10-provider-handoff.md` as workflow references when useful. This creative brief takes precedence over example game content.

Confirm the actual executable and implemented features through `capabilities`, `schema`, `script-api` and the relevant `asset schema` queries. For a genuinely new directory, start with:

```powershell
build/langmake.exe new --template workflow-2d --output games/matterfall
build/langmake.exe context --project games/matterfall
build/langmake.exe target list --project games/matterfall
```

After creation, start subsequent work with `context --project games/matterfall`. Inspect stable IDs and author through supported JSON/Luau/assets. Do not assume a feature described only in a roadmap exists. If essential behavior cannot be implemented safely through the supported game API, explain the specific gap; do not patch the engine or silently substitute different controls.

## The core interaction — exact behavior

The player chooses **where horizontally** an object drops. Every object enters from the same horizontal release line near the top of the container.

- A mouse click or touch inside the playfield selects its X coordinate. Convert that screen coordinate into the container's world X coordinate.
- Spawn at `(selected_world_x, fixed_release_y)` in the 2D gameplay plane. Ignore the click/touch's Y coordinate when choosing spawn height. Clicking near the floor must still release the object from the top.
- Use the same plane and fixed depth for all dropped objects. Gravity pulls them downward; they bounce gently, collide and pile up in a bounded container.
- Clamp X using the incoming object's collider radius so it cannot spawn through a wall. If the release location is obstructed, show clear feedback; never teleport the object lower into the pile.
- One deliberate click/tap creates one drop. Holding must not cause automatic repeated drops, and synthesized mouse events must not double-trigger a touch.
- Show the next object prominently before it is released. Keep that preview synchronized with the actual queued object. An invalid input must not consume it.
- On PC, hovering shows a ghost object at the release line and a subtle vertical guide. On touch, a tap drops at its X; if implementing drag-to-aim, use one committed drop on release and clearly communicate that behavior. Do not require a separate Fire button.
- Menu/HUD interactions never also drop an object. Ignore secondary touches for dropping while a primary gesture owns the interaction. Clear pending input on pause, cancellation and backgrounding.
- Add a short, clearly indicated drop cooldown to prevent accidental rapid dumping, without making aiming feel sluggish.

Two **identical-tier objects that physically touch** merge into exactly one object of the next tier. Proximity without contact is not sufficient. Different tiers collide but do not merge.

Resolve merges deterministically: an object can participate in at most one merge in a tick. Consume each parent once; create one child at a sensible midpoint, with bounded inherited motion and brief collision-safe placement. Allow satisfying chain reactions without duplicate rewards, overlapping spawn explosions or objects falling through the floor. Handle three-way and four-way simultaneous contacts explicitly.

Use the engine's supported physics/contact APIs. Prototype circular collision and stacking first: do not assume unsupported polygon physics, browser APIs or a renderer rotation implies a rotated collider. Keep collider geometry honest relative to the art. Only use game-script simulation if a supported native approach is unsuitable and the result remains stable and testable.

## Three discovery chapters

This is **science-inspired fantasy merging**, not a simulation of nuclear fusion, chemistry, biological evolution or actual relative scale. Say so briefly in the codex/help. Use correct object names and element ordering while keeping the main game delightful rather than turning it into a lecture.

### 1. The atomic chapter

Begin with hydrogen. Two hydrogen objects merge into helium; two helium into lithium; continue through **all 118 currently recognized elements in atomic-number order, ending at oganesson**. Treat “largest atom” here as the final/highest-atomic-number element in this sequence, not a claim about measured atomic radius.

Use recognizable symbols, names and atomic numbers in previews and the discovery codex. Create a readable family-based visual system with distinct palettes, nucleus patterns, highlights and orbit motifs. Adjacent active tiers must be distinguishable without relying only on color. Electron-orbit motifs may be stylized; do not label decorative arrangements as accurate configurations.

### 2. The molecular chapter

After completing the atomic sequence, two final-tier atomic objects merge into the first molecular-tier object and unlock the molecular chapter. Include a meaningful curated ladder of roughly 12–16 molecules, including **water and dopamine**, plus visually varied simple and more complex examples.

Define and document the exact order in data before implementing content. Order it for gameplay and visual complexity; do not claim it is a real reaction chain or strictly increasing molecular size unless checked. Every merge still requires two identical objects. Give water, dopamine and other molecules recognizable, differentiated pixel cluster silhouettes, rather than recolored atomic balls.

### 3. The microscopic chapter

The final molecular pair unlocks the microscopic chapter. Include roughly 8–12 varied forms or structures featuring **viruses, bacteria and chromosomes**, ending in a **tardigrade**. This is a discovery collection: chromosomes are structures, viruses are not simply “tiny animals,” and the ordering is fictional rather than an evolutionary family tree.

Use dramatically different silhouettes and restrained animations: capsid/spike motifs, bacterial membranes, chromosome shapes, cellular interiors and finally an unmistakable eight-legged tardigrade. All remain readable at gameplay size. Creating the first tardigrade completes the demo with a memorable celebration, final discovery card and options to inspect the collection or start again. Never merge it into an undefined tier.

## Make the entire demo reachable

A strict hydrogen-only merge tree would require exponentially many drops. **Do not ship that progression.** Keep every element in the ladder, but make unlocked tiers feed into the drop queue quickly enough for a human to reach the end of the demo.

- Use a configurable frontier-based supply system: as discoveries advance, newly discovered and nearby lower tiers become available to drop. Early hydrogen-only play should teach the rule, then the supply advances with the player.
- Supply may include the highest discovered tier, but must never skip to an undiscovered tier. The player must create each next discovery through a valid pair merge.
- Weight the queue toward useful frontier tiers while retaining enough lower tiers to create planning and satisfying cascades. Prevent an endless supply of obsolete low-tier clutter.
- Use small milestone checkpoints and clearly explained board-management relief, such as an earned recycle charge for an obsolete piece. No silent deletion of objects or random victory gifts.
- Persist discoveries and chapter milestones. A loss should offer a useful checkpoint retry rather than forcing the player through the entire periodic table again.
- Target a first complete demo journey of approximately **30–45 minutes**, configurable in tuning data. This is a design target, not something to claim verified from simulation. Validate actual pacing through human playtests.
- Keep scoring, queue weights, sizes, bounce, cooldowns, danger timing, chapter thresholds and assist settings in data rather than scattered magic numbers.

Object sizes must remain playable across 118 atomic tiers. Use bounded size bands or a defined scale system; do not make each successor multiplicatively larger forever. Keep current neighboring tiers visually legible, and do not let the highest atomic tier fill the entire container before the chapter transition.

## Expanding container and camera

Completing the atomic chapter triggers a celebratory zoom-out and a **real increase in usable container size**. Do this again, more modestly, at the microscopic transition if it improves the demo.

Animate the transition smoothly. Temporarily suspend drop input and resolve or safely suspend in-flight merges. Expand actual wall positions, floor/ceiling policy, legal drop bounds and camera framing together. Preserve existing objects, state and progression; keep all pieces inside valid bounds. Resume with the next preview and screen-to-world mapping updated correctly.

This must not merely scale a background texture. Do not nonuniformly stretch sprites or collision shapes. State the intended geometry/size policy and test it. Mobile and PC should preserve the same gameplay rules and usable world geometry; presentation profiles adapt screen layout around them.

## Challenge, feedback and polish

Make the baseline forgiving and satisfying. Show a visible danger line and a grace-period meter when settled objects threaten to overflow. An object legitimately crossing the release line while falling should not cause instant defeat. Merges that clear the danger should recover the warning. Never fail without a readable reason.

Provide pause, resume, restart confirmation, volume controls, a brief first-play tutorial and a discovery codex. A Relaxed option may reduce overflow pressure, but do not dilute the main demo by building unnecessary mode systems before the core is polished.

Every merge should feel responsive: pixel compression/squash where supported, a short flash, small palette-matched particles, a readable discovery burst and a musical merge sound. Escalate the feeling of chain reactions without covering the pile or saturating the audio mixer. Offer reduced effects and avoid excessive flashing or screen shake.

Author original music and sound effects through Langmake's supported formats: calm, curious laboratory ambience that grows warmer and more organic across chapters; distinct drop, impact, merge, discovery, chapter-unlock, warning and victory cues. Respect sound/music settings in saves and after lifecycle resume.

## Art direction and UI

Create original, richly detailed pixel art inspired by the density, material detail and warmth of **Terraria and Stardew Valley**, without copying their assets, characters or interfaces. Pixel art should look intentionally illustrated, not like generic colored circles, emoji, blurred icons or bare debug shapes.

Use consistent pixel scale, deliberate limited palettes per material/family, crisp silhouettes, layered highlights, tiny animated details and attractive backgrounds that remain quieter than the objects. High detail must not compromise recognition, performance or collision readability.

The container is the visual focus. Avoid enormous sidebars that shrink gameplay on phones. Show:

- The current/next drop object, name and tier.
- Score and best discovery.
- A clear chapter progress bar with numeric progress.
- A compact preview of the next merge result or discoverable ladder.
- Overflow danger and any recycle/assist charges.
- Small, reachable pause and codex controls.

Use custom pixel-style frames and distinct buttons with pressed states, not a row of bland rectangles. Preserve control semantics when styling them. Keep UI from obscuring the pile; use translucency or repositioning where necessary. Use documented `dp`, safe-area and action-binding support, and validate glyph coverage.

Provide a **portrait phone profile** as the initial mobile design assumption: a tall drop container suits one-thumb play. Use a centered container with compact supporting UI on desktop. Author a separate tablet profile if targeting tablets; do not claim that a universal iOS build proves iPad layout. Support only orientations actually implemented and tested. Keep gameplay, definitions and assets shared across profiles.

Include an original, recognizable app icon from the first export: a luminous pixel atom transforming into a tiny living form. Declare it through supported `application` metadata; use official export/signing paths rather than patching app bundles.

## State, performance and project structure

Use stable project/application identity unique to this game. Keep active objects and IDs, tiers, positions, motion required for supported restoration, score, discoveries, queue/RNG, chapter/expansion state, cooldowns, danger state and settings in declared save-compatible state. Do not rely on unsaved module locals for progress.

Save at appropriate settled milestones and through supported lifecycle/manual-save mechanisms. Define what happens to a drop/merge interrupted by saving. Test restoration twice without duplicate children, score or unlocks. Use `save-check`, assessed compatibility and explicit migrations for actual schema changes.

Keep content definitions, gameplay, presentation and tuning clearly separated. Bound live entities and effects; reuse assets where appropriate. Avoid hundreds of active decorative electron entities per atom. Use sprite detail/animation for rich appearance. Verify dense-pile performance on the actual available targets instead of claiming universal 60 FPS from a small host test.

## Implementation, verification and deployment

First prove a small vertical slice with several atomic tiers: precise drop mapping, stable stacking, correct contact merging, synchronized next preview and a usable phone layout. Then implement the complete discovery ladders, reachable progression, expansion transitions and final presentation. Do not stop at the vertical slice or call a handful of elements the full demo.

Use named scenarios and current documented `validate`, `check`, `ui-check`, `verify`, fixed-tick capture and save-roundtrip workflows. Essential coverage includes:

1. Same click X at different screen Y values yields the same release point; wall-edge drops remain legal before and after zoom/resize.
2. UI taps, held pointers and duplicate touch/mouse events do not create unintended drops.
3. Two identical contacting objects create exactly one successor; different tiers never merge; crowded simultaneous contacts cannot reuse a parent.
4. All atomic successors and chapter boundaries are valid; the tardigrade ends the demo safely.
5. Queue advancement remains within discovered tiers and makes progression feasible.
6. Expanded geometry, camera framing and restored saves retain valid object positions and consistent input mapping.
7. Overflow warning/grace/recovery, cooldowns and pause behave as specified.
8. Save/load preserves the pile, queue, discoveries and settings without duplicate rewards.
9. UI checks and rendered captures at representative desktop, phone and tablet sizes where supported; font coverage and visible/hit bounds checked.
10. Dense-pile stress and bounded particle/audio activity.

Use isolated verification targets for automated mobile scenarios. Technical simulations do not prove real touch comfort or audible sound. Inspect native captures and sidecars, confirm actual installed revisions and distinguish host, simulator, physical rendering/input and human-feedback evidence.

Configure targets **for this game** using ignored `.langmake/targets.local.json` and documented target commands. Never inherit VOIDRUNNER's SSH config, Mac folder, signing team, phone serial or application IDs. Ask for target details/authorization when missing, while continuing host work. A remote Mac must use a dedicated `games/matterfall` subfolder within its authorized workspace. Keep credentials out of game files.

Use supported `deploy`, `operation show/resume`, bounded `receipt`, and `session launch` workflows. Let humans handle Apple/Keychain/license prompts locally. Do not create task-local signing or icon glue when the engine already supports it. Install and launch on explicitly authorized available targets; clearly report any platform whose build or physical verification remains blocked.

Finish with `brief.md`, concise tuning/playtest notes, a content catalog, verification evidence and generated `HANDOFF.json`. Provide the playable result, exact launch instructions, controls, platform status and known limits. Ask focused feedback about drop accuracy, readability, merge satisfaction and discovery pacing. Once handed over, do not interrupt the human's active play session.

**Success means a complete, attractive demo in which a player can intuitively drop at the chosen X coordinate, enjoy stable and rewarding merges, progress through atoms and molecules into microscopic forms, experience a genuinely expanding container, and finally discover a tardigrade—with the same game working well on PC and mobile.**
