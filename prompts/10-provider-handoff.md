# Continue with another agent or provider

Use this when moving between Studio providers or between Studio and an external agent. Complete or cancel the previous worker before starting the next one on the same project.

```text
Task: continue an existing Langmake game iteration.

Engine: [ENGINE_PACKAGE_DIRECTORY]
Game: [EXISTING_PROJECT_DIRECTORY]
Current handoff notes: [NOTES_FILE_OR_PASTED_SUMMARY]

Read the engine's agent instructions, this project's brief, accepted human
decisions, outstanding tasks and recent technical evidence. Inspect the
current files and revision. Do not assume access to the previous provider's
private memory or to a separate consumer application's conversations.
Run context --project GAME and read HANDOFF.json if present. Check its source
revision, brief/notes references, scenarios, operation records and outstanding
human feedback against the current files. Configure this game's targets
explicitly on a new machine; never inherit another game's SSH, phone or team.

Preserve these human decisions: [DECISIONS]
The next authorized change is: [CHANGE]
The human's latest playtest feedback is: [ACTUAL_FEEDBACK_OR_NOT_YET_PLAYED]

Explain any material contradiction between the notes and current files.
Keep guesses separate from accepted decisions. Implement the authorized
change, preserve a recovery checkpoint, run relevant native technical checks
and record an updated handoff. Do not interrupt an active human play session.
Use operation show/resume for an eligible interrupted deployment. Preserve
original errors and follow the recorded local signing helper if applicable;
do not request passwords or private keys. Refresh HANDOFF.json after the work.

Give me the playable result, exact launch command, controls, changes, checks
and focused feedback questions. I remain responsible for creative approval.
```
