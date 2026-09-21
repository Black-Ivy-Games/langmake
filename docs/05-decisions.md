# Decisions and remaining choices

Status as of September 18, 2026. The creator selected an independent runtime, delegated language/library selection and explicitly authorized implementation. A runnable Windows alpha now exists with validation recorded in [the release document](07-first-release.md). Future platform and capability proposals remain identified below.

## Requirements already established by the creator

- Conversation and a local agent with directory access are the primary authoring workflow.
- The engine exposes APIs/files and has no engine-owned LLM integration. External agents may use local or online models interchangeably.
- Build our own runtime using free/open-source libraries; Godot/Bevy are not runtime dependencies.
- The human is the decision-making developer and primary gameplay playtester. AI agents implement the human's direction and run technical verification; meaningful playable iterations return to the human for feedback. A small preview supports this ongoing collaboration.
- Native agent testing remains supported. Computer-use playthroughs are supplementary when requested or needed for a focused input/UI defect; they do not replace human gameplay evaluation. Agents work autonomously within authorized scope without asking permission for every implementation detail.
- Full 2D and 3D capabilities are long-term goals.
- Desktop, mobile, VR, and console releases are desired.
- Reuse free/open-source third-party libraries instead of rebuilding every subsystem.
- Current authorization covers implementing, building, testing and packaging the Windows alpha in this directory.
- Do not operate outside this directory without permission.
- The main runtime direction is selected; discuss substantial new scope and future platform commitments together.

## Decisions with the largest effect

| ID | Decision | Status / selected direction | Consequence or remaining choice |
| --- | --- | --- | --- |
| D01 — settled | Existing runtime or independent runtime? | Creator selected an independent runtime assembled from libraries | We own the world model, operations and subsystem integration |
| D02 — settled | Who manages the model? | The external agent; engine APIs/files work independently of local or online models | No engine-owned model connection, hosting, inference, or conversation system |
| D03 — selected | First creator-facing interface? | External agent chat plus standalone preview and CLI/live inspection | Alpha controls are documented; richer picking, overlays and inspector layout remain future work |
| D04 — selected | First games and visual ambition? | Primitive 2D platformer and 3D exploration/collection example | Imported animated characters, richer lighting and the full courtyard brief are future expansions |
| D05 — selected | First platform and expansion order? | Windows alpha first; desktop, mobile/OpenXR, console pilot proposed afterward | Each later platform requires its own access, integration and device evidence |
| D06 | Console SDK exception to FOSS? | FOSS engine and public tools, separately supplied restricted SDK/platform integration | Strictly FOSS everything limits which platforms we can ship on |
| D07 — selected | License for original engine/tool code? | MIT; see the root LICENSE | Third-party code retains its own notices; future external assets require their own provenance |
| D08 — selected | Native language | C++20 | Portable feature subset; qualify actual target toolchains |
| D09 — selected | Gameplay language | Luau, interpreter baseline, with native bindings in the alpha source | Generated typed definitions and checker integration remain future work; standard Lua only a fallback for a demonstrated blocker |
| D10 — conditional | Graphics foundation | bgfx, with SDL3 for platform integration | Must pass Windows and Android OpenXR interoperability gate before significant renderer expansion |
| D11 — selected | Main subsystem libraries | SDL3, Flecs, Jolt, Box2D, miniaudio and JSON in the alpha; cgltf, ozz, Recast/Detour, RmlUi and OpenXR later | Adopt by milestone; integration is distinct from runtime qualification |

## Remaining qualification and design choices

Pinned library versions and build options, the initial JSON project format, explicit entity IDs and PNG capture are present in the alpha source. Record reference hardware, qualify screenshot timing and gameplay behavior, measure reload versus restart, and determine which files an agent needs per task. Rich multi-file scenes, prefabs and generated script type metadata remain future design work.

Use the [selected stack](06-selected-stack.md). Confirm the compiler and rendering/XR integration before building a large framework. Keep API schemas, script declarations and native component metadata consistent; do not require JIT for shipping.

## Decision history

The initial plan compared a Godot-backed authoring product with an independent runtime. After discussing the additional control over state, execution, inspection and API guarantees, the creator chose our own runtime. The earlier foundation comparisons remain as background rather than active alternatives.

The creator also clarified that local versus online inference must not matter: the engine is an API-like tool for external agents. C++20 and Luau were selected, followed by explicit implementation authorization. The runnable Windows alpha and pinned local dependencies now exist; original code is MIT licensed. Build, headless, scripted playthrough and preview evidence are recorded separately from future platform ambitions.
