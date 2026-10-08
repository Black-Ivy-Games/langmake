# Optional Langmake Studio

Studio is a separate authoring application for human developers to manage game projects and AI conversations. The runtime remains usable through files and native commands by external Codex, Claude and other coding agents. Games do not contain a model connection, account credential or chat dependency.

See [Studio module instructions](../studio/README.md) for the implemented launch, provider setup, tools, testing and packaging contract. See the preserved [0.4 implementation checklist](16-studio-android-plan.md) and current [0.4.1 Android qualification](20-android-qualification.md) for executed release checks and boundaries.

## Two supported authoring routes

1. Point an external coding agent at the engine package and an authorized game directory. Read the engine's AGENTS and agent/runtime guides, then use JSON/Luau, `validate`, `check`, `simulate`, `capture` and `play`.
2. Open Studio, select the same game directory, configure a supported provider profile and start a project chat. Studio supplies project tools and native engine commands to the agent.

Both routes edit ordinary projects with `project.json` as the entry point. There is no Studio-only project format, cloud project ownership or mandatory account to run games. Portable runtime distributions include the command-line interface and documentation without Electron or provider libraries.

Do not let an external agent and a Studio worker edit the same game simultaneously. Studio coordinates its own writers; independent external editors cannot be silently controlled by that coordination. Finish or cancel one worker before handing the project to the other. Preserve a checkpoint and record outstanding work.

## Human-directed workflow

The human supplies the creative direction. Agents implement the authorized iteration and perform technical checks. Use the [prompt library](../prompts/README.md) to structure a first request, bug report, asset request or feedback iteration.

When the game is ready to try, finish the agent's writing and open Play. Studio protects its active human play session against its own agent edits, resets and reloads. After playing, record what happened and the changes you want. Passing tests or generated screenshots do not establish human creative approval.

## Conversations and provider changes

Projects can have multiple chats. Shared project memory should record the human's brief, accepted decisions, outstanding tasks and a concise handoff. Keep agent assumptions separate from decisions the human actually made.

Switch providers at a completed or cancelled turn. The next worker receives the selected chat's relevant history and shared project memory, then reads the same files. Provider-private hidden reasoning and separate consumer-app chats are not transferred. Important decisions must therefore be explicit project knowledge rather than assumptions about a model's memory.

Authentication and billing are provider-specific. A subscription is usable only through a supported provider integration. API-key profiles use that API's billing. Local-model profiles depend on the selected model's tool and image capabilities. Provider credentials belong to Studio's protected account storage, outside game sources and distribution content. The module's documentation distinguishes real provider verification from protocol fixtures.

## Distribution and updates

Studio is independently buildable and packageable. A distribution including Studio contains a normal runtime package; a runtime-only distribution remains usable by external agents. Android exports use the same public build/deployment tools in either route.

Do not hand-edit copied distribution documentation. Update canonical source guides and prompts, regenerate the packages, and verify their manifests. Keep the prior release available until the new artifact's tests and limitations have been recorded.

## September 22, 2026 usability update

See [Studio usability and upgrade](37-studio-usability.md) for the implemented studio2 changes, local-server setup and qualification boundaries. The editor entry point is now labeled **UI editor**. Provider model discovery is an explicit human GET request, and the runtime remains provider-independent.
