# Star Courier: a single-screen 2D brief

This is an **authoring brief, not an existing or tested game**. It separates a concrete game loop, small content counts, presentation choices and observable checks. One arena, five pickups and two moving hazards keep the iteration manageable while exercising native sprites, audio, HUD and restart behavior.

[Prompt index](README.md) · [Reusable new-game template](01-new-game-template.md)

Copy the prompt below and replace both directory placeholders.

```text
Implement the following playable game iteration using Langmake. This authorizes creating the game and its project-local assets, scripts and technical checks.

Engine package: [ENGINE_PACKAGE_DIRECTORY]
New game directory: [NEW_PROJECT_DIRECTORY]

Read AGENTS.md and prompts/00-workflow.md in the engine package first. Follow that shared workflow. Locate the executable and inspect capabilities, schema, script-api and the sprite/music asset schemas. Create the project with new --template rockets-2d; replace the starter's content with this game. Keep work inside the new project. Preserve an existing destination and clarify before overwriting it. Propose engine changes separately if a required capability is missing.

I am the creative decision-maker and primary gameplay playtester. Make routine implementation choices within this brief, finish the authorized iteration and hand it to me for feedback. Use native technical verification as described in the shared workflow.

Game: Star Courier
Make a top-down, single-screen 2D salvage game in a small rectangular space station. A courier starts at a clearly marked landing pad. Collect exactly five energy cells, avoid exactly two patrol drones, then return to the pad to win. Show collected cells out of five and health out of three in a screen-space HUD.

The courier starts with three health. Drone contact removes one health and grants one second of invulnerability with a visible flash. Each drone follows a different simple, readable patrol lane. Zero health shows a loss panel; winning shows a delivery-complete panel. After either outcome, R restarts with all five cells, both drones, full health and cleared timers. Do not add combat, rooms, procedural maps, upgrades or online features.

Controls: WASD or arrow keys move; R restarts after win/loss; close the window to exit player mode. Keep the camera fixed with the whole arena visible. Bound movement to the arena. Every cell must be reachable without deliberate damage.

Art and sound: create original native pixel sprites with a dark navy station, cyan courier, amber cells and coral drones. Use a small consistent palette and a two-frame movement animation. Make an original, quiet 15–25-second looping chiptune plus pickup, damage and success sounds from native scores. No downloaded assets or external creative software are needed.

Technical acceptance:
- Validation and offline script checking pass.
- Stable IDs and declared state expose cell count, health, invulnerability deadline and playing/won/lost status.
- A native input scenario proves five distinct pickups and return-to-pad completion. Remaining on one cell cannot count it twice.
- A focused damage scenario proves a hit, protection against immediate repeated damage, eventual loss and R restoring a fresh attempt. Verify restart after victory too.
- Fixed-tick PNG/state captures show the initial arena, a damaged state and victory. Check HUD values against declared state.
- Render the theme and a short mixed scenario. Report clipping/dropped audio commands and fix any caused by normal gameplay.

Handoff: give me the exact play --project command, controls, goal, implemented changes, technical results and known issues. Gameplay acceptance awaits my playtest. Ask: Does movement feel comfortable? Are drone routes and damage feedback clear? Is the route through the five cells satisfying?
```
