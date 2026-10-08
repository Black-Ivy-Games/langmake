# A brief for your next game-creation agent

When music or sound matters, use [the music and sound workflow](30-music-and-sound-guide.md) and [the soundtrack prompt example](../prompts/12-music-and-sound.md). The human chooses the musical direction through audible iterations; technical checks are separate.

For an Android brief, follow the [Android agent workflow and documentation index](21-android-agent-guide.md) and adapt [the Android prompt](../prompts/09-android-game.md).

For an iPhone brief, start with the [iOS workflow and qualification requirements](23-ios-agent-guide.md) and adapt [the iOS prompt](../prompts/11-ios-game.md). Read current Apple qualification and explicitly configure this game's authorized Mac workspace, signing team and device before requesting deployment.

Begin with `langmake context --project GAME` and [the game workflow](26-game-workflow.md). Use per-game targets, shared presentation profiles, native icons, named scenarios, resumable exports and generated handoff records. Never copy another game's local SSH/device/team configuration.

For more specific structures and filled examples, use the [prompt library](../prompts/README.md): small 2D games, 3D adventures, group strategy, feedback iterations, bug repairs, assets and planning before implementation.

Give the agent access to the extracted Langmake package and the directory where you want the new game. Windows authoring and play need no engine download, compiler, engine-owned model connection or external creative application. Android exports use the included native player and require the workspace-local Android SDK, JDK and Gradle described in the [Android guide](18-android.md); they do not require compiling the engine. Paste this brief, followed by your game idea and intended first playable milestone:

> Use Langmake 0.5.0 to help me create my game. I am the creative decision-maker and primary gameplay playtester; you implement the game and perform technical verification. Read the package's AGENTS.md, START-HERE.txt and AGENT-API.md before editing. Read RUNTIME-GUIDE.md and TEXT-ASSETS.md for the systems you use. Inspect capabilities, schema, script-api and asset schemas instead of assuming an API exists.
>
> Start with `langmake.exe new` in a fresh project directory inside the workspace I authorized. Choose workflow-2d for shared mobile/desktop authoring, or the closest of adventure, strategy, shared-2d, rockets-2d, rockets-3d, 2d or 3d. Keep the bundled examples intact. Use editable Luau modules and JSON definitions, registered source files, native sprite/model/music recipes, screen-space UI and bounded runtime entities. Reuse my existing decisions; ask about creative direction only where my brief leaves a material choice unresolved.
>
> Finish the authorized playable iteration, then run validation, offline script checking and bounded native simulation scenarios. Use exact-tick captures and their sidecars when visual evidence helps. Check save/load in fresh processes if the game uses persistence. Use isolated technical sessions and preserve any session I am playing. Do not replace my playtesting with a computer-use playthrough or infer creative approval from passing checks.
>
> Hand over the exact `play --project` command, controls, goal, changes, checks and known issues. Tell me what is ready for human playtesting and ask two or three focused questions about the experience. Use my feedback for the next iteration. Do not add an engine-owned LLM connection or claim unsupported platform exports.

## Quick launch

From the extracted package folder in PowerShell:

```powershell
.\langmake.exe new --template adventure --output my-game
.\langmake.exe validate --project my-game
.\langmake.exe check --project my-game
.\langmake.exe play --project my-game
```

Or double-click `run-adventure.cmd`, `run-strategy.cmd` or `run-shared-2d.cmd` to try the reference projects. Their own READMEs describe controls. Close the window to exit player mode.

For a repository checkout, use the actual current build executable; the canonical executable is `build/langmake.exe` after `scripts/build.ps1`. The portable package is under `dist/Langmake-0.5.0/`. All generated game files, saves and evidence should stay in the directory you authorized. The same brief works with external agents and optional Studio. For Android, read the current release evidence and Android guide first: the package supplies prebuilt debug/release players, while SDK setup, explicit device selection, touch/UI layout and game-specific verification remain part of authoring.

## Scope for the first iteration

Describe the player's role, the central action, art/music direction and one short objective. Grow scenes, story, systems and content through subsequent human feedback. Windows/D3D11 and Android/GLES3 are supported within the actual device coverage in the [Android qualification record](20-android-qualification.md). The initial limits include one active scene, 4,096 live entities, 256 navigation agents (128 exercised together), primitive colliders, modest model materials and Latin font coverage. See the runtime guide for detailed budgets and unsupported imports; the release evidence records actual tests and hardware.
