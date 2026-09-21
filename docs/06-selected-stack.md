# Selected runtime language and library stack

Selected stack · Implementation status updated September 18, 2026

The creator selected an independent runtime and authorized implementation. **C++20 is the runtime language and Luau is the gameplay language.** The runnable Windows alpha integrates SDL3, bgfx/bx/bimg, Flecs, Jolt, Box2D, Luau, miniaudio, nlohmann/json, LodePNG and Manifold. Version 0.2 adds [native text-authored assets](10-text-assets.md), including an engine-owned synthesizer. Version 0.3 also integrates cgltf, ozz-animation, Recast/Detour, RmlUi and FreeType through engine adapters; consult [implemented contracts](13-runtime-guide.md) and [execution evidence](14-release-evidence.md). Other optional additions below remain planned. OpenXR interoperability remains unqualified and is scheduled for the platform milestone.

## Why C++20

C++ is the best fit for this particular dependency graph and platform ambition. SDL, bgfx, Jolt, Flecs, Luau, ozz, Recast and the UI/audio libraries expose native C/C++ interfaces. Direct integration minimizes additional bindings, permits control over resource lifetimes and threading, and leaves a practical route to vendor platform SDKs once access is available.

C++20 is the baseline for our code, not a demand that dependencies use the same standard. Use a portable subset and verify the supported compiler/standard-library features for each target. Do not require C++ modules or compiler-specific extensions. A language selection alone does not establish console compatibility.

The cost is memory-safety responsibility and native build complexity. Use explicit ownership, resource handles with lifetime checks, compiler diagnostics, static analysis and sanitizer builds where supported. Prefer readable systems and component definitions over elaborate template-generated frameworks. Native recompilation belongs to engine changes; ordinary game edits should stay in scripts and data.

Rust is attractive for its [safe ownership model](https://doc.rust-lang.org/book/ch04-00-understanding-ownership.html), but this chosen library collection would add FFI and binding maintenance. C# would introduce managed-runtime and export/AOT decisions without simplifying this native integration work. Neither is rejected as a language generally; C++ is selected for this engine's requirements.

## Why Luau for game behavior

[Luau](https://github.com/luau-lang/luau) is an embeddable Lua-derived language with gradual typing, a checker/linter, a compiler and a VM. Its main license is MIT. Use interpreter execution initially; native code generation is optional rather than a dependency. [Luau performance design](https://luau.org/performance/)

In the alpha, an external agent edits Luau gameplay and JSON scene data, then receives script and project-validation diagnostics. Generated API types and checker integration are still future work; runtime errors are not a substitute for that type-checking pipeline. Script/data edits avoid a native rebuild.

The alpha source supplies a small native gameplay binding set; the example scripts exercise it. Typed declarations, broader asset APIs and shared metadata generation remain to be built. Luau does not know about our entities, physics or assets automatically. Generate discovery schemas and script API definitions from the same engine-owned metadata where practical. Batch operations across the script/native boundary, keeping heavy subsystems in native code.

The alpha compiles Luau source when loading the project and includes the compiler and VM at one pinned revision. A future cooked shipping format can compile trusted source ahead of loading and omit the compiler from players. Do not accept arbitrary downloaded bytecode as a safe interchange format. Luau differs from standard Lua, so do not assume all Lua libraries/binding systems work unchanged. See its [embedding guidance](https://github.com/luau-lang/luau).

Use deliberate host bindings, bounded memory allocation and script interruption for runaway scripts. The [Luau sandbox](https://luau.org/sandbox/) is part of this design, not automatic protection around native bindings. Saveable gameplay state should use declared components/data; an arbitrary VM heap, closure, or coroutine stack is not our portable save-game format. Script reload may restart a behavior or scene when migration is unsupported.

## Main runtime libraries

Licenses describe principal project code. The alpha's exact revisions and notice inventory are in [dependencies.lock.json](../dependencies.lock.json) and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Entries from 3D asset import onward are future integrations. This list is not a platform or test certification.

| System | Selection and official source | Main license | What we gain / what we own |
| --- | --- | --- | --- |
| Platform | [SDL3](https://github.com/libsdl-org/SDL) | zlib | Windows, events, controllers and lifecycle. We expose input actions and target capabilities. |
| Graphics access | [bgfx](https://github.com/bkaradzic/bgfx) | BSD-2-Clause | Native graphics backends including Vulkan, Metal and Direct3D. We build the actual 2D/3D scene renderer. |
| World organization | [Flecs](https://github.com/SanderMertens/flecs) | MIT | Entity/component storage, queries, relationships and reflection. Our public schema and saved identity remain engine-owned. |
| 3D physics | [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | MIT | Collision, rigid bodies and character-related primitives. We integrate transforms, events, fixed steps and gameplay. |
| 2D physics | [Box2D](https://github.com/erincatto/box2d) | MIT | Dedicated 2D bodies, contacts, joints and queries. Keep a separate world from 3D physics. |
| Gameplay | [Luau](https://github.com/luau-lang/luau) | MIT | Script compiler, VM and analysis tools. We implement typed game APIs and reload behavior. |
| Audio | [miniaudio](https://github.com/mackron/miniaudio) | MIT-0 or public domain | Playback, mixing, resources and basic spatialization. We implement game buses, events and platform adaptation. |
| Solid asset creation | [Manifold](https://github.com/elalish/manifold) | Apache-2.0 | Native primitives and solid boolean operations. Langmake supplies bounded JSON recipes, parts, colors and rendering. Integrated in 0.2. |
| Pixel assets | LodePNG, from pinned bimg sources | zlib-style | PNG decoding/encoding. Langmake supplies pixel palettes, raster drawing and sprite animation. Integrated in 0.2. |
| 3D asset import | [cgltf](https://github.com/jkuhlmann/cgltf) | MIT | glTF/GLB parsing. Our importer loads referenced content, validates it, cooks resources and uploads them. |
| Skeletal animation | [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) | MIT | Sampling, blending and animation conversion. We build animation state machines, character synchronization and renderer skinning. |
| Navigation | [Recast/Detour](https://github.com/recastnavigation/recastnavigation) | zlib | Navmesh generation and path queries, with optional crowd support. NPC decisions and game behavior remain ours. |
| Game UI | [RmlUi](https://github.com/mikke89/RmlUi) | MIT | Text-authored layout, styling, elements and data binding. We supply renderer/input integration and inspectable UI state. |
| VR interface | [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) | Main headers/loader Apache-2.0 OR MIT; [per-file terms](https://github.com/KhronosGroup/OpenXR-SDK/blob/main/COPYING.adoc) | Standard XR API/loader. We own frame/session orchestration, tracked input, interaction and device qualification. |

These are coherent subsystem roles, not instructions to integrate all twelve at once. With bgfx, SDL owns platform facilities rather than a second SDL_GPU device. miniaudio owns the audio system rather than duplicating it through SDL. Flecs organizes gameplay components but does not take ownership of physics internals or GPU resources. Do not expose third-party pointers/handles as persistent scene identity.

## Rendering: the future XR qualification gate

**Select bgfx as the initial graphics foundation.** It offers a native C/C++ interface and multiple free graphics backends while allowing us to design independent 2D and 3D rendering systems. Its own description is a library around which to build an engine. It does not supply a complete materials/lighting/animation/content system. [bgfx overview](https://bkaradzic.github.io/bgfx/overview.html)

The alpha renderer uses Windows Direct3D 11 with colored primitives, orthographic/perspective views and PNG capture. Textured sprite batching, asset mesh submission, materials, lights and shadows are future renderer milestones. Particles, LOD, instancing and streaming follow measured game needs. Full-feature parity with established engines is not inherited from the graphics abstraction.

The alpha packages the pinned upstream bgfx example DXBC binaries `vs_cubes.bin` and `fs_cubes.bin`; their notices and hashes are recorded. A future authored shader pipeline should use bgfx's offline tools and supported source conventions with structured diagnostics. Start with engine-provided materials and explicit parameters. Arbitrary GLSL/HLSL is not automatically portable through that pipeline. [bgfx tools](https://bkaradzic.github.io/bgfx/tools.html)

The major open integration question is OpenXR. A bgfx maintainer recommends its external-texture creation path instead of assuming older internal-access/override calls work everywhere. That establishes a possible route, not a tested engine integration. [Maintainer discussion](https://github.com/bkaradzic/bgfx/discussions/3837), [API reference](https://bkaradzic.github.io/bgfx/bgfx.html)

Before building substantial rendering features, test:

1. Stereo rendering into runtime-owned swapchain images on a selected Windows PC VR setup and an Android Vulkan headset.
2. Correct graphics-device and queue setup, image ownership, synchronization, and acquire/wait/render/release sequencing.
3. Frame scheduling against predicted display time, session focus loss, shutdown/restart, and a desktop mirror.
4. Graphics/XR validation and sustained timing at the selected refresh rate. Multipass stereo is acceptable initially; multiview and foveation have separate performance gates.
5. Whether integration can remain small and maintainable, without a substantial permanent graphics-library fork.

bgfx's [threading/frame pipeline](https://bkaradzic.github.io/bgfx/internals.html) must be reconciled with OpenXR rather than equating a submitted frame with completed GPU work. Use Khronos' [Vulkan sample](https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/main/src/tests/hello_xr/graphicsplugin_vulkan.cpp) as a reference.

This gate belongs to the platform milestone after Windows 0.3. If it fails, reconsider the XR backend while retaining the qualified Windows renderer behind its adapter. SDL_GPU is a fallback candidate, but its OpenXR helpers in the development branch were marked for SDL 3.6.0 while the release listing checked during the September 17 research showed 3.4.16. Treat that research snapshot as emerging functionality, not a released guarantee. Recheck maturity before the XR milestone. [SDL XR header](https://github.com/libsdl-org/SDL/blob/main/include/SDL3/SDL_openxr.h), [SDL releases](https://github.com/libsdl-org/SDL/releases)

wgpu and Filament remain alternative graphics approaches described in the earlier research. We are not selecting multiple competing renderers. Game-facing rendering APIs must not expose bgfx-specific types, so replacing that adapter does not rewrite game scripts or scene files.

## Supporting libraries and tools

nlohmann/json and CMake are used now. The remaining table entries are future additions; the alpha does not currently integrate ImGui, Tracy, RmlUi, font shaping or a texture/mesh cooker.

| Need | Selection | License / adoption point |
| --- | --- | --- |
| Font rasterization | [FreeType](https://freetype.org/license.html) | Select FTL license; introduced with RmlUi |
| Complex-script shaping | [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | MIT-style; introduced with multilingual text support |
| Preview/debug widgets | [Dear ImGui](https://github.com/ocornut/imgui) | MIT; development builds, early inspection controls |
| Profiling | [Tracy](https://github.com/wolfpld/tracy) | BSD-3-Clause; early optional instrumentation |
| Mesh cooking | [meshoptimizer](https://github.com/zeux/meshoptimizer) | MIT; after basic imports work, with measured benefits |
| Texture cooking/transcoding | [Basis Universal](https://github.com/BinomialLLC/basis_universal) | Apache-2.0 core plus bundled notices; select required codec subset later |
| JSON tooling | [nlohmann/json](https://github.com/nlohmann/json) | MIT; authoring/import/control paths, not repeated frame-loop parsing |
| Build configuration | [CMake](https://cmake.org/licensing/) | BSD-3-Clause; top-level native build and target toolchains |

RmlUi is HTML/CSS-like, not a browser. Its default font path uses FreeType; HarfBuzz integration requires deliberate work. Shaping alone does not implement bidirectional ordering, line breaking, fallback, input methods or accessibility. These remain explicit UI acceptance criteria. [RmlUi build options](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/building_with_cmake.html), [HarfBuzz boundaries](https://harfbuzz.github.io/what-harfbuzz-doesnt-do.html)

Dear ImGui is for the small development inspector, not the production UI foundation; its own documentation identifies internationalization/accessibility limitations. Tracy assists performance diagnosis, while ordinary engine commands still return machine-readable timing summaries without requiring its GUI.

nlohmann/json is a parser/data representation, not a JSON Schema validator. Our registered property definitions must drive semantic checks and generated schemas; select any additional standards-complete schema validation package against the exact required draft during the tooling milestone. Keep JSON authoring separate from compact runtime storage and cooked binary resources.

CMake coordinates the native build, using pinned source checkouts in `.deps/` and the bgfx.cmake integration. bgfx/bx/bimg revisions are recorded together. Future asset tools must remain matched to their runtime readers. Existing CMake, Ninja, Git and MinGW tools are invoked without installing or modifying a toolchain outside this workspace.

## Optional later networking

[GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets), BSD-3-Clause, is the preferred transport candidate when multiplayer requirements are specified. It is not part of initial bootstrap. It does not supply world replication, prediction, rollback, matchmaking or hosted relay services as a complete product. Choose those policies around actual game requirements and qualify each target's available platform implementation.

## Free/open-source constraints that affect this selection

Use ozz's glTF/offline route; do not introduce its optional Autodesk FBX SDK dependency. Prefer glTF/GLB as our first supported 3D exchange format.

Do not adopt KTX-Software wholesale under a blanket Apache-2.0 label. Its [license inventory](https://github.com/KhronosGroup/KTX-Software/blob/main/LICENSE.md) identifies a non-open-source ETC decoder. Basis Universal is our selected texture candidate; any later KTX integration must explicitly exclude incompatible code and audit the pinned build. This is a real dependency-selection issue under the creator's FOSS requirement.

Keep license notices, versions/hashes, source URLs, build options and transitive dependencies in the dependency record. Asset/font licenses are separate. Vendor SDKs and drivers remain external platform prerequisites, as described in [Platforms](03-platforms.md). The public platform list of any selected library is not proof of current-console readiness.

## How we make the assembled engine suitable for agents

The target architecture uses an engine-owned component/property registry connecting source data, native state, script API definitions, validation, queries and diagnostic locations. The alpha supplies its initial JSON project/schema, native state and command results; a unified generated metadata and type pipeline remains future work. Third-party storage and handles stay internal. Use the current executable schema rather than assuming every planned property exists.

The command surface is conventional software: inspect, patch, validate, run, step, capture, measure, restore and package. A native headless executable should expose essential operations directly. TypeScript may provide thin orchestration and an optional MCP adapter, but there is no engine-hosted LLM, model-provider SDK, or conversation manager.

The libraries supply powerful individual systems. We still build the engine's project format, object identity, API, resource management, asset cooking/cache, renderer features, sprite/tilemap layer, animation state machines, saves, scenario testing, checkpoints and platform packaging. These are the responsibilities that determine whether the agent workflow succeeds.

## Integration order and current boundary

1. Native build, SDL window/input, bgfx sprite/mesh output, Flecs world and a small typed native command surface; optional Tracy/ImGui.
2. Luau bindings, scene loading/validation, fixed-step Box2D/Jolt examples, observation/capture and asset import; perform the early XR graphics gate before significant renderer expansion.
3. First 2D game: sprite/tile features, miniaudio, RmlUi/FreeType, recoverable edits and packaging.
4. First animated 3D game: cgltf pipeline, ozz skinning/blending integration, camera/lighting features; Recast/Detour when the game requires navigation.
5. Asset optimization, multilingual UI, mobile/XR qualification and subsequent platform work against measured requirements.

The authorized bootstrap has downloaded the current subset and recorded its revisions in [dependencies.lock.json](../dependencies.lock.json). The alpha release document records its observed acceptance evidence and limits. Adopt the remaining libraries at their milestones, with new license inventory and feature tests, instead of treating this full selection as already implemented.
