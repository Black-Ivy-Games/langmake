# Tidepool Couriers: a small group-strategy brief

This is an **authoring brief, not an existing or tested game**. It names the unit count, navigation setting, commands and cargo rules. One ground map and 24 simple workers give room to evaluate group behavior without implying a complete RTS or a general performance guarantee.

[Prompt index](README.md) · [Reusable new-game template](01-new-game-template.md)

Copy the prompt below and replace both directory placeholders.

```text
Implement this playable Langmake game iteration. This authorizes creating the game project, its native assets and project-local technical checks.

Engine package: [ENGINE_PACKAGE_DIRECTORY]
New game directory: [NEW_PROJECT_DIRECTORY]

Read the package's AGENTS.md and prompts/00-workflow.md first. Follow that shared workflow. Locate the executable; inspect capabilities, schema, script-api and model/music asset schemas. Create the project with new --template strategy, replacing its content with this bounded game. Keep work inside the new project, preserve an existing destination and clarify before overwriting it. Propose engine changes separately if needed.

I remain the creative decision-maker and primary gameplay playtester. Implement this iteration with routine engineering choices, verify it using native commands and hand it to me for gameplay feedback.

Game: Tidepool Couriers
Build one compact, top-down-view 3D ground map containing a workshop, a glass-fragment pickup field and two large rock obstacles with spacious routes around them. A commander directs exactly 24 small articulated courier robots. Collect exactly twelve fragments and deliver them to the workshop to win. Each worker carries at most one fragment. Reserve pickups explicitly so two workers cannot claim the same fragment.

The player selects all workers or either half. Gather orders send the selected group to the pickup field; return orders send it to the workshop. Cargo is credited only when its assigned return task arrives. Empty workers may return without increasing the score. Follow continuously tracks the commander; recall moves the selected group to the commander's current location once. Make formation spacing easy to adjust in the editable orders module. Report unreachable tasks clearly rather than awarding their cargo.

Controls: WASD moves the commander; 1 selects all; 2 the first half; 3 the second half; C gathers; V returns cargo; B follows; J recalls; F6 saves; F9 loads an existing save; close the window to exit player mode. Use a fixed elevated camera showing the whole map. Show selected count, remaining fragments, carried cargo and delivered count in anchored UI. Give selection and cargo clear visual distinctions.

Art and sound: use original native solid-part robots with idle/walk animation, rocks built from supported solids and a restrained teal/sand/copper palette. Compose a quiet electronic loop and pickup/delivery sounds. Keep assets text-authored. This iteration has no worker-spawning controls, combat, construction, jumping/flying routes, dynamic terrain, streaming or multiplayer.

Technical acceptance:
- Validation and offline script checking pass.
- Native scenarios demonstrate selection changing which workers receive orders, and selected workers following/recalling through native ground navigation.
- Declared state exposes selection, per-worker task, fragment reservation/cargo and delivered total. Switching orders or revisiting destinations cannot duplicate fragments.
- A bounded scenario routes workers around both rocks and delivers exactly twelve fragments, with no negative or duplicate inventory.
- Fresh-process save/load preserves selection, assignments, reservations/cargo and delivery progress.
- Fixed-tick captures show the initial layout, moving group and completed delivery. Report measured simulation timing for this scene and audio clipping/drops; do not claim a universal frame-rate guarantee.

Handoff: give the exact play --project command, controls, objective, implemented behavior, technical results and known issues. Mark human gameplay feedback pending. Ask: Can I tell which workers are selected and carrying cargo? Are obstacle routes easy to follow? Do gather, return, follow and recall behave as I expect?
```
