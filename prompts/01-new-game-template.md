# Structure: a first playable game

This separates creative intent, observable behavior, scope and handoff. Replace placeholders and delete optional fields. You do not need to know engine internals or fill every section; the agent can help clarify a material gap. The filled examples are [2D](02-star-courier.md), [3D adventure](03-sleeping-lighthouse.md) and [group strategy](04-tidepool-couriers.md).

```text
TASK MODE: Implement the first playable iteration described below.

CONTEXT
Use Langmake in [ENGINE_PACKAGE_DIRECTORY]. Create a fresh project at
[NEW_PROJECT_DIRECTORY], inside [AUTHORIZED_WORKSPACE]. Read its AGENTS.md,
agent guide and prompts/00-workflow.md; follow that workflow. Discover this
build's capabilities and choose a suitable installed starter.
Run context on the resulting project and read GAME-WORKFLOW.md (source:
docs/26-game-workflow.md). Establish this game's explicit targets once;
do not reuse another game's machine or signing configuration implicitly.

EXPERIENCE
Working title: [NAME]
Player role and setting: [WHO YOU ARE AND WHERE]
Desired feeling: [FOR EXAMPLE: CURIOUS, COZY, TENSE OR PLAYFUL]
The central activity: [WHAT THE PLAYER REPEATEDLY DOES]

FIRST PLAYABLE SCOPE
Use [2D OR 3D] and target [A PLATFORM THIS BUILD QUALIFIES].
This iteration contains [ONE SMALL LEVEL / TWO COMPACT AREAS / OTHER BOUND].
A typical attempt should take roughly [TARGET DURATION, SUBJECT TO PLAYTEST].
Future direction, not part of this implementation: [OPTIONAL AMBITION].

PLAYER RULES
- Controls: [MOVEMENT AND PRIMARY ACTIONS].
- The player progresses by [ACTION AND OBSERVABLE RESULT].
- Success means [CLEAR CONDITION].
- Failure or recovery works like [CONDITION AND RESPONSE, OR NO FAILURE].
- A fresh attempt should reset [RELEVANT GAME STATE].
- Persistence: [SESSION ONLY, OR WHAT SAVE/LOAD MUST PRESERVE].

PRESENTATION
Art direction and readable visual cues: [PALETTE, SHAPES, SCALE, MOOD].
Assets to create with native text tools: [SPRITES/MODELS/ANIMATION/MUSIC].
Sound direction: [SYNTHESIZED MUSIC MOOD AND DISTINCT EVENT CUES].
HUD must show [ONLY INFORMATION THE PLAYER NEEDS].
Accessibility/readability priorities: [OPTIONAL TEXT SIZE, MOTION, CUES].

DECISIONS
Keep these creative choices: [HUMAN DECISIONS ALREADY MADE].
Choose sensible defaults for [ROUTINE IMPLEMENTATION DETAILS].
Ask me before changing [MATERIAL CREATIVE CHOICE, IF ANY].
If a required feature is unsupported, explain the gap and propose a bounded
alternative or separate engine task instead of silently changing the brief.

TECHNICAL ACCEPTANCE
Validate and check scripts. Create native scenarios demonstrating
[SUCCESS], [IMPORTANT FAILURE/RECOVERY] and [STATE THAT MUST NOT DUPLICATE].
Use captures and sidecars to inspect [IMPORTANT VISUAL/HUD/ANIMATION STATE].
If this game saves progress, verify the declared state in a fresh process.
Register named scenarios so later agents can rerun the same checks. For phone
UI, inspect profile-specific layout/glyph diagnostics and native captures.
Record actual results, any unperformed checks and known issues.

HANDOFF
Finish the authorized iteration and give me its exact play command,
controls, objective and a concise change/verification report. I am the
creative decision-maker and primary gameplay playtester. Ask for focused
feedback on [TWO OR THREE EXPERIENCE QUESTIONS] before expanding the design.
Generate HANDOFF.json with references to the brief, notes and actual evidence.
```

Use the [feedback structure](05-feedback-iteration.md) for the next iteration. Compatibility and maintenance notes are in the [library index](README.md).
