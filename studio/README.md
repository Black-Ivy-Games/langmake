# Langmake Studio 0.5.0

Studio is an optional Windows desktop authoring application. Projects remain ordinary Langmake files, and exported games run independently of Studio and model accounts. External agents and the runtime CLI remain fully supported.

The [0.5.0 game workflow](../docs/26-game-workflow.md) also works directly through external agents and the CLI: per-game targets, application icons, shared profiles, held controls and resumable deployment do not require Studio. Local target settings are explicitly selected for each game; no host or phone is inherited from another project.

## Open the packaged application

Run **Langmake Studio.exe**. The complete distribution includes a separate `runtime/` containing Langmake, templates, fonts, shaders and its documentation. Keep the folder together. This alpha is an unsigned portable application, not a system installer.

1. Open **Providers & runtime**. The packaged runtime is discovered automatically; you can choose another `langmake.exe`.
2. Add a provider profile and a model ID. Configure its endpoint/API key or choose an existing supported CLI executable.
3. **New** creates a project from one of eight templates; **Open folder** imports an existing project with `project.json`.
4. Describe an authorized iteration in chat. The worker uses bounded project-file and Langmake tools, streams progress, and starts from a source checkpoint.
5. Review files, changes and technical checks, then **Play** the game yourself. Agent runs and manual source changes are blocked during that play session. **Stop** releases the project for editing.
6. Record **human playtest feedback**, then send the next instruction. Passing automated tests does not establish gameplay acceptance.

Multiple projects and conversations persist. The project and conversation ellipsis controls rename items or hide/archive them; opening a removed project folder restores its retained history. Archived conversations can be shown and restored from the sidebar. Change providers between turns; Studio carries the human brief, accepted decisions, tasks, handoff notes and recent conversation into the next model. Provider-internal hidden state is not transferred. Project memory is editable in the Memory tab. It remains Studio authoring data, not a required game runtime file.

## Provider connections

| Profile | Connection |
| --- | --- |
| OpenAI-compatible API | HTTPS endpoint ending in `/v1`, API key and an explicit model ID. Uses streaming Chat Completions with function tools. |
| Local endpoint | A local server exposing the same chat/tool contract, such as `http://127.0.0.1:1234/v1`. Keys are optional. Studio does not download or host models. |
| Anthropic API | Direct Messages API at `https://api.anthropic.com/v1`, API key and explicit model ID. No proprietary SDK is bundled. |
| Codex app-server | Choose a user-supplied `codex.exe` supporting the current app-server protocol and dynamic tools. **Connect Codex** uses the official account login flow, or the profile's API key. |
| Claude Code | Choose a user-supplied `claude.exe`. Studio uses its documented streaming CLI and a private local MCP bridge with only Studio tools. An API key can be supplied in the profile. |

API usage and account eligibility follow the provider's current terms. A ChatGPT or Claude application subscription is not represented as an API credential. Studio never scrapes another application's authentication files. Codex account data uses a dedicated `accounts/<profile-id>` directory inside Studio data. External CLI authentication/storage is managed by that CLI within its dedicated directory. No Codex or Claude executable is bundled in the current Studio package.

Studio API keys are encrypted through Electron `safeStorage`, using the Windows operating-system account's protection. They are never returned to renderer code, written into project files, or stored as plaintext by Studio. Provider requests send the conversation and the project content the agent reads. Only configured model endpoints are contacted by the API adapters; loopback HTTP is permitted, remote API endpoints require HTTPS. Choosing a CLI means trusting that separately installed executable.

The selected model must support function/tool calls. Reading captures requires a vision-capable model; a text-only model may reject image input. Model names and account availability are user/provider configuration, not hardcoded subscription promises.

## Project tools and protection

The shared tool contract includes file listing, UTF-8 reads, revision-checked writes, PNG/JPEG viewing, installed documentation, native engine commands, durable project memory and focused human questions. Native commands cover discovery, validation, Luau checking, inspection, simulation, profiling, asset creation/export and timeline capture. Engine failures return diagnostics as tool errors. No arbitrary shell tool is exposed.

Paths remain relative to the selected project. Parent traversal, absolute paths, alternate streams, symbolic links/junctions and `.git`, `.langmake`, `.studio`, `node_modules` are excluded from agent file tools. Native commands may write their own derived `.langmake` data. A selected project folder is an explicit capability; it is not a machine-wide drive grant. Keep secret material out of game projects sent to remote models.

One worker writes a project at a time. Cancel interrupts inference and processes, drains outstanding local tools, then releases the writer. Files already committed before cancellation remain inspectable. Revision checks detect external edits during write preparation; they are not a filesystem-wide lock against unrelated editors. Do not edit the same file concurrently in external tools.

Each run takes a Studio source checkpoint. History shows added/removed/modified files and before/after text; restore asks for confirmation and takes a recovery checkpoint. Checkpoints include up to 256 MiB of project files, excluding protected/generated directories, and retain the newest 20 within 1 GiB per project. Retention may remove older checkpoints; export or separately back up important milestones. Player save slots remain separate.

Studio stores configuration, encrypted keys, chats, periodic run journals, memory and checkpoints in `.data/` beside its executable (or in `studio/.data/` during source development). `LANGMAKE_STUDIO_DATA` can select another writable Studio-data directory before launch. This directory is private authoring state; do not distribute it with games. Interrupted runs are marked on restart. Conversation history persists; model handoffs use recent 24 user/assistant messages plus explicit project memory rather than an unlimited context window. Runs have an 80-round API tool budget and a 30-minute upper bound.

The player launches in a separate native window. Studio protects sessions it owns; external CLI sessions should be coordinated separately. The optional UI layout tool uses isolated native captures; no continuously embedded game viewport, autonomous desktop gameplay, local-model downloader, network multiplayer or shared multi-user editing is claimed.

## Android panel

Start with the [Android agent guide](../docs/21-android-agent-guide.md) and [device qualification](../docs/20-android-qualification.md). In a portable distribution these are `runtime/ANDROID-AGENT-GUIDE.md` and `runtime/ANDROID-QUALIFICATION.md`. Studio workers can list and read both through the `documentation` tool and are instructed to do so for Android targets. The guide gives the reading order, authoring and verification steps, and an explicit handoff for the Targets panel. Android build/deploy actions are panel operations, not worker tools; an external directory agent can use the native Python CLI independently.

Choose `scripts/android/android.py` from the runtime distribution and a Python executable in Settings. Targets offers Doctor, Devices, Build APK, Install, Launch and Stop. Save a stable, unique application ID and display label for each game; Studio proposes a per-project ID. Select ARM64 for a phone or x86_64 for an emulator, and landscape, portrait or automatic orientation. Select a device serial returned by Devices. Build progress is streamed from the native tools. Build artifacts go under the project's generated `.langmake/studio/android/` directory, with a unique APK filename per iteration. Native tooling owns SDK/NDK/JDK discovery and build diagnostics. SDK license acceptance and toolchain bootstrap remain explicit human operations through the Android setup instructions; Studio does not auto-accept licenses. Android builds and actual-device rendering require the corresponding toolchain and hardware; a green desktop UI test does not establish Android qualification.

## iOS panel

Start with the [iOS agent guide](../docs/23-ios-agent-guide.md) and [iOS qualification record](../docs/24-ios-qualification.md). Portable names are `runtime/IOS-AGENT-GUIDE.md` and `runtime/IOS-QUALIFICATION.md`; workers can discover them with the `documentation` tool. Follow their current qualification limits. Windows tests of this panel do not prove that a game runs on an iPhone.

Choose Python and `scripts/ios/ios.py` in Settings. Save a stable bundle ID, display label, orientation and development or release configuration. **Stage project** cooks the referenced game content into a fresh folder beneath `.langmake/studio/ios/`. The output path is shown in Targets. Staging is available on Windows. Move that staged folder to an authorized Mac and follow the guide's standalone commands; Studio does not guess a remote Mac, copy Apple credentials, or configure remote execution.

Build, Devices, Install, Launch and Stop require a local Mac with Xcode and are disabled on Windows. The Studio source contains the local Mac command adapter, but this Studio distribution remains a Windows application; a Mac Studio distribution has not been qualified. On the Mac, use the standalone Python tooling independently. Select `device` with `arm64` for an iPhone, or a simulator architecture matching the Mac (`arm64` for Apple silicon, `x86_64` for Intel). Device builds require an explicit 10-character signing Team ID. Allowing Xcode provisioning updates is a separate opt-in and may contact Apple using Xcode's configured account. Existing profiles can be used without this opt-in. No certificate, account or subscription is included with Langmake.

For a first iPhone build, run **Devices**, explicitly select the physical phone, enable **Allow Xcode provisioning updates**, and enable **Register selected iPhone with Apple** if it needs adding to your signing team. Registration is off by default and is saved per project; it authorizes Xcode to register that selected device during **Build app**. Studio passes the selected CoreDevice ID to the native CLI, which resolves the hardware UDID for Xcode. It never substitutes the first listed phone. Leaving the device selector empty and registration disabled retains generic builds using existing profiles. Simulator targets disable these signing opt-ins. The CLI rechecks the actual device before building; a saved Studio list is not proof that a phone is still connected.

The adapter accepts only devices returned by Devices for the selected destination, verifies build reports and artifact paths, and retains the process identity returned by Launch for Stop. An iOS play session locks agent runs and edits until stopped, as desktop and Android play do. Builds use unique project-local output directories; changing content or export settings requires staging and building again. The native Mac shader compiler is built automatically unless an explicit compatible `shaderc` is selected. Build/deploy operations are human Targets panel actions or external CLI agent actions, not provider worker tools. No iOS build service or provider SDK is added to the runtime or exported game.

## Source setup

Install dependencies only inside this module. Setup requires Node 22.12 or newer and npm; no global tools are installed. All npm/Electron downloads and temporary files are redirected into `studio/.cache/`.

```powershell
powershell -ExecutionPolicy Bypass -File studio/launch.ps1 -Install -NodePath C:/path/to/node.exe
# After dependencies exist, launch directly:
powershell -ExecutionPolicy Bypass -File studio/launch.ps1
```

If npm is not discoverable on PATH, also pass `-NpmCli C:/path/to/npm/bin/npm-cli.js`. `LANGMAKE_ENGINE` overrides automatic runtime discovery. Source discovery checks `build/langmake.exe` before the older `.cache/build-0.4.1/langmake.exe` and `.cache/build-0.4/langmake.exe` folders. On macOS, source Studio uses the native `langmake` filename and supports extensionless executable selection; set `LANGMAKE_ENGINE` to the Mac host CLI built by the iOS tooling. The iOS guide builds that CLI under `.cache/ios/host/Release/langmake`, which Studio also discovers. A host-only CLI can validate, cook and simulate games; it does not imply a qualified Mac desktop player.

```powershell
cd studio
node --test test/core.test.cjs test/ios.test.cjs test/package.test.cjs
node test/ui.cjs
node scripts/package.cjs --runtime ../dist/Langmake-0.5.0 --output dist
```

The package script emits `Langmake-Studio-0.5.0/`, includes only the selected runtime's verified `package-manifest.json` entries, retains Electron license files and writes dependency and per-file SHA-256 inventories. Extra games, local settings and other unlisted files in a used runtime folder are excluded. Missing files, invalid paths, private directories and size/hash or version mismatches stop packaging. An existing package directory is not overwritten. Packaging uses the pinned `package-lock.json` and Electron 44.4.3. Build tools and test packages are excluded from the distributed application.

## Verification status

The core test suite covers project containment, junction rejection, revision conflicts, cancellation, credential redaction, persistence, checkpoints, writer/play locks and image tools. `test/ui.cjs` exercises the real Electron window with an **explicitly fake** loopback model server, real native Langmake validation, file edits, checkpoint diff, Play/Stop protection, feedback and chat creation. It verifies integration, not model quality. The actual Codex executable's initialize/account-read/thread-start protocol has also been exercised without a signed-in account. Live provider generation requires configured credentials/endpoints and is not implied by those tests.

The iOS adapter tests use explicitly fake process results to verify command routing, selected-device checks, signing opt-in, artifact containment, junction rejection, play locks and report validation. The real Windows Electron test verifies target settings and Mac prerequisite messages. Neither test suite is a substitute for Xcode, simulator or physical iPhone qualification.

`node test/ios-stage.cjs` performs a real Studio broker â†’ Python â†’ native `cook --target ios` integration check using the source runtime. It verifies two independently generated stages, content manifests and stable game identity. Set `LANGMAKE_ENGINE` or `LANGMAKE_IOS_PYTHON` for explicit executable locations. This still does not compile or run an Apple application.

## Music authoring

A runtime with audio feature revision 1 exposes recorded WAV/FLAC music/SFX, synchronized layers and new effects. Read [music and sound](../docs/30-music-and-sound-guide.md) and [audio qualification](../docs/31-audio-qualification.md). The worker's `engine` tool accepts `command:"music"` with `subcommand` status, instruments, schema, validate, pack-install, pack-verify, render, audition, transform or import. Its options use the native CLI names without `--`; every score, source, pack and output path remains relative to the active project. Writing/rendering respects the human play-session lock.

Portable authoring packages can include the sfizz host library. If `music status` reports it missing, the human installs/builds it once through `langmake music setup` outside an agent turn; Studio does not grant project workers permission to rebuild the engine installation. Pack installation is an optional 395 MiB download inside the selected project, for example `directory:".cache/music-packs/vsco-ce"`. External directory agents can instead reuse an authorized shared pack. Baked games need no sampler or source bank. Give the human the generated audition file path to listen in their player; Studio does not add an audio editor or claim musical approval from render metrics.

## UI layout editor

Click **UI editor** beside Play to adjust existing game elements with native-rendered drafts, property editing, anchors, drag/resize, undo/redo and source review. Apply saves one RML document after a checkpoint and revision checks. Export writes `ui-design.json`, a screenshot and per-element notes/pinned intent for agents. Read [the complete guide](../docs/34-ui-editor-guide.md) and [qualification](../docs/35-ui-editor-qualification.md). No provider connection is needed. The matching runtime and optional editor scripts are required; old packages do not have UI observation revision 1.

## Studio build studio2 · September 22, 2026

This build includes the **UI editor** button beside Play, even without a provider profile. It edits existing registered RML interfaces, not entire game scenes. Use Open draft to capture a preview; stop Play or an active worker first. Older 0.5.0 packages may lack this feature. Providers & runtime shows the Studio build identity; pair it with a runtime supporting UI observation revision 1.

For LM Studio on this computer, start the server and select **Local OpenAI-compatible endpoint**. Use **http://127.0.0.1:1234/v1** (adjust the port if configured), then **Test connection & fetch models**. Choose the exact Model ID from the suggestions and save. An optional API key is needed only if the server requires authentication. The network address LM Studio advertises may not be loopback: Studio continues to require HTTPS for non-loopback endpoints. Testing fetches only the model list; it does not run inference or qualify tool use. An existing saved key is used only for an unchanged provider type and endpoint; enter a key explicitly when testing a changed destination.

Tool requests/results are retained under collapsed **Agent activity**. Intermediate tool-round commentary is separate from the final answer. Empty API completions get at most one summary-only retry with tools removed; a second empty response, interrupted stream or output limit reports an error. Completed tool actions are not replayed by that retry. A human retry can still repeat prior work, so inspect activity and History first. Greetings and connection checks are prompted to answer directly; model compliance is not guaranteed.

New projects use the name entered in Studio, while template game titles remain authored project content. For an existing imported project, use the project **…** button to rename its sidebar entry; this does not rewrite the in-game title.

In the source workspace, close the old Studio and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\studio\open-packaged.ps1
```

This launches dist/studio2/Langmake-Studio-0.5.0 with its matching runtime, reusing dist/Langmake-Studio-0.5.0/.data when present. It retains projects, profiles, chats and checkpoints in place; it does not read/export credentials or copy private data into a release. Supply -DataDirectory for a different workspace data folder. Launching the new executable directly uses its own .data instead. LANGMAKE_ENGINE explicitly overrides the saved runtime path. Never run old and new releases against the same data simultaneously.
