# The Sleeping Lighthouse: a small 3D adventure brief

This is an **authoring brief, not an existing or tested game**. It bounds an adventure to two compact areas, one quest and one enemy. Persistent story progress, readable animated interaction and a human handoff are concrete goals; a production-scale RPG is outside this iteration.

[Prompt index](README.md) · [Reusable new-game template](01-new-game-template.md)

Copy the prompt below and replace both directory placeholders.

```text
Implement the following playable game iteration using Langmake. This authorizes creating the project, original project-local content and technical verification for this brief.

Engine package: [ENGINE_PACKAGE_DIRECTORY]
New game directory: [NEW_PROJECT_DIRECTORY]

Read the package's AGENTS.md and prompts/00-workflow.md first and follow that workflow. Locate the executable; inspect capabilities, schema, script-api and model/music asset schemas. Start with new --template adventure, then replace its story, layout and art with this game. Keep work inside the new project. Preserve an existing destination and clarify before overwriting it. Identify missing capability before proposing engine changes.

I own creative decisions and will do gameplay playtesting. Complete this iteration autonomously, use native technical checks, then give me a playable result and focused questions. Automated completion is not my approval of the experience.

Game: The Sleeping Lighthouse
Make a modest 3D adventure about repairing a forgotten coastal beacon. Use exactly two compact areas: a shore workshop and a lighthouse chamber. At the workshop, Keeper Neri offers a tuning tool and explains the repair. The player can accept immediately or ask a short question about the lighthouse; both branches allow the same quest. Grant the tool once and equip it visibly in the HUD.

The player finds one amber lens, then quiets one malfunctioning maintenance robot with three clearly signaled tuning strikes. The lighthouse doorway opens when the tool is equipped, the lens is held and the robot is disabled. Once opened, it stays available in both directions. Inside, interacting with the beacon consumes the lens, completes the quest and plays a short two-shot camera sequence with a moving light element. Allow return travel to the workshop afterward; Neri acknowledges the repair. Keep the story brief and original.

Controls: WASD moves; E talks/uses; 1/2 choose dialogue; Space strikes; F6 saves; F9 loads an existing save; close the window to exit player mode. Show objective, health, equipment and lens count in anchored UI. Use a following camera with obstruction handling. The player has three health; the robot gives a visible warning before a short-range attack, with a generous attack cooldown and one-second damage invulnerability. At zero health, recover at the current area's entrance with full health while retaining quest progress. Do not require a prior save to recover.

Art and sound: make original low-poly solid-part models with joint-authored idle, walk and strike clips. Use cool slate architecture, warm amber machinery and a readable tool silhouette. Create a quiet synthesized coastal theme and short tool/item/beacon sounds. Use supported simple materials. Do not add realistic water, an open world, extra enemies, a full inventory screen or external asset dependencies.

Technical acceptance:
- Validation and offline script checking pass for both areas and registered modules.
- Declared state exposes dialogue flags, once-only tool reward, equipment, lens count, robot health, quest stage and current scene.
- A native scenario demonstrates the optional dialogue route, tool reward, lens pickup, three marker-driven hits, gated entry and beacon completion. Repeated interactions cannot duplicate rewards or consume a second lens.
- A focused damage scenario verifies warning/cooldown behavior, invulnerability and recovery after zero health.
- Fresh-process save/load preserves progression and both areas. Returning to the workshop keeps the disabled robot and collected lens from reappearing.
- Saving during the cinematic resumes its declared progress and restores the gameplay camera on completion.
- Fixed-tick captures show readable HUD, articulated characters and both areas. Render representative audio and report clipping/drops.

Handoff: give the exact play --project command, controls, objective, changes, technical evidence and known limitations. Human gameplay acceptance is pending. Ask: Is the camera comfortable? Are hit timing and the robot warning understandable? Is the next repair step always clear?
```
