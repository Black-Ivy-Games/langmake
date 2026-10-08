# Structure: reproduce and repair a defect

State expected and observed behavior separately. Screenshots, saves, revisions and exact steps help if you have them; the agent should investigate missing technical detail instead of asking you to diagnose the engine.

```text
TASK MODE: Investigate and fix this defect in my game.

Engine package: [ENGINE_PACKAGE_DIRECTORY]
Existing project: [EXISTING_PROJECT_DIRECTORY]
Authorized workspace: [DIRECTORY]
Read AGENTS.md, the agent guide and prompts/00-workflow.md.

EXPECTED
[WHAT SHOULD HAPPEN].

OBSERVED
[WHAT ACTUALLY HAPPENS, AND HOW OFTEN].

REPRODUCTION
1. [START CONDITION, INCLUDING A SAVE IF RELEVANT].
2. [ACTION].
3. [ACTION THAT EXPOSES THE PROBLEM].

OPTIONAL EVIDENCE
[SAVE/CAPTURE/LOG PATHS, SOURCE REVISION, OR RECENT CHANGES].
My play session is [CLOSED / ACTIVE]. Keep it undisturbed.

SCOPE
Find the cause and repair the game files needed for this behavior. Preserve
unrelated creative choices. Reproduce the defect with native tools where
possible and show the same scenario working after the fix. Check related
failure/recovery cases and save compatibility if affected.

If the cause is an engine defect that cannot be fixed within the authorized
game scope, provide a small reproduction and a proposed engine change.
Do not hide it with an unexplained gameplay redesign or modify the engine
without authorization.

Report the cause, the fix, technical evidence, remaining uncertainty and
the exact launch/reproduction steps for my human confirmation.
```

Example report content:

```text
Expected: a delivered letter grants one stamp, even after loading a save.
Observed: talking to the same recipient after loading sometimes adds a
second stamp. Start after delivering the first letter, save, close the
game, reopen, load and talk to that recipient again. Fix duplicate rewards
while preserving earned stamps and the remaining delivery objectives.
```

For script interruptions, retain the full structured diagnostic, actual runtime path and source revision. New source builds expose separate CPU/elapsed timing, bounded stack locations and optional binding measurements; see [script execution](../docs/39-script-execution-guide.md). Do not infer a busy loop from elapsed time alone or resume a partially failed world as a completed update. A custom frontend must handle the engine exception itself. Historical binaries may lack the new fields.

Return to the [library](README.md). For a subjective change in feel, use [feedback and iteration](05-feedback-iteration.md).
