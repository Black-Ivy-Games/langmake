# Structure: turn playtest feedback into an iteration

Separate what the human experienced from the change they want. This gives the agent a concrete target while preserving the game already made. Include the game revision or a capture if available, but do not make technical diagnostics a prerequisite for giving feedback.

```text
TASK MODE: Implement this next iteration of my existing game.

Use Langmake in [ENGINE_PACKAGE_DIRECTORY] and the project at
[EXISTING_PROJECT_DIRECTORY]. Read AGENTS.md, the agent guide and
prompts/00-workflow.md. Continue this project; do not start a replacement.
Workspace authorized for changes: [DIRECTORY].

MY PLAYTEST FEEDBACK
- What happened: [OBSERVATION, INCLUDING CONTEXT].
- How it felt or affected play: [HUMAN EXPERIENCE].
- The result I want: [DESIRED BEHAVIOR OR FEEL].
- Another issue, if any: [OBSERVATION AND DESIRED RESULT].

IMPLEMENT NOW
1. [HIGHEST-PRIORITY CHANGE].
2. [SECOND CHANGE, IF ANY].

KEEP THESE DECISIONS
[MECHANICS, ART DIRECTION OR CONTENT THAT SHOULD REMAIN CONSISTENT].
Ideas for later, not authorized in this iteration: [OPTIONAL].
My current play session is [CLOSED / ACTIVE AT THIS PROJECT]. Preserve an
active session and coordinate the edit window if source changes would reset it.

CHECK AND HAND OFF
Inspect the existing state and save compatibility before making changes.
Use native technical checks for the affected behavior and any likely
regressions. Show before/after evidence where it clarifies the change.
Give me the exact play command, controls, what changed and known issues.
Explain what I should try to judge whether this addresses my feedback.
I will decide whether the change feels right after playing it.
```

For example, replace the middle sections with:

```text
In Star Courier, I frequently slide past cells after releasing movement.
I want the ship to stop sooner while retaining a slight feeling of drift.
The blue drones also blend into the station. Give hazards a more distinct
silhouette and a warm accent, with a brief visible cue when I take damage.

Implement those two changes. Keep the five-cell objective, patrol routes,
health rules and music. Choose initial movement values and verify that
releasing input stops the ship sooner without changing its maximum speed.
Capture the revised hazard and damage cue. Ask me to compare steering
comfort and hazard readability; do not treat numerical checks as approval.
```

Return to the [library](README.md) or [new-game structure](01-new-game-template.md).
