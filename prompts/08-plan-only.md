# Structure: explore an idea before implementing it

This prompt deliberately requests a proposal rather than a game. It is useful for a larger ambition, a substantial design change or a feature whose engine support is uncertain.

```text
TASK MODE: Discuss and plan only. Do not implement yet.

Read the Langmake package documentation and discovery output at
[ENGINE_PACKAGE_DIRECTORY]. You may inspect [OPTIONAL EXISTING PROJECT]
read-only inside [AUTHORIZED_WORKSPACE]. Do not create or modify project
files, launch an interactive game session, install tools or download assets.
Answer here with the proposal.

MY IDEA
[SETTING, PLAYER ROLE, CORE ACTIVITY AND DESIRED FEEL].

DECISIONS ALREADY MADE
[IMPORTANT CREATIVE DIRECTION OR CONSTRAINTS].

WHAT I NEED FROM YOU
- Explain what this build supports now, what belongs in editable game
  scripts, and what would require engine development or new qualification.
- Propose the smallest meaningful playable milestone that expresses my idea.
- Suggest a few later milestones without treating them as authorized work.
- Identify material open decisions and the tradeoffs I should consider.
- Describe technical checks and human playtest questions for the first slice.

I am the creative decision-maker. Make recommendations and ask the few
questions that materially affect the design. Wait for explicit authorization
before creating the game or changing the engine.
```

Example idea:

```text
I want a cozy island mystery where I help a handful of unusual residents,
collect clues and gradually repair a lighthouse. I care more about warmth,
small discoveries and memorable conversations than combat. Recommend a
small first slice, show where scenes/dialogue/quests/saves would help, and
distinguish the eventual island from what we should build first.
```

After the human selects and authorizes a milestone, turn it into the [implementation structure](01-new-game-template.md). Return to the [library](README.md).
