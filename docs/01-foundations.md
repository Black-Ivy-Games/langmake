# Foundation choices and starting resources

Primary-source research checked September 17, 2026; implementation status updated September 18. Recommendations are engineering judgments; upstream support lists are not evidence that our product already works on a platform. Links to `stable`, `latest`, or repository branches can change. The alpha's exact source revisions are recorded in [dependencies.lock.json](../dependencies.lock.json).

**Decision update:** the creator selected and authorized implementation of an independent runtime. C++20 + Luau and the current library subset now form a runnable [Windows alpha](07-first-release.md), with native/headless tests, scripted playthroughs and observed rendering. That release document records the validation boundary. The comparisons below retain the reasoning behind the alternatives; they do not reopen the foundation decision.

## What the terms mean

**Runtime:** the program and systems that execute the game: graphics, physics, sound, controls, animation, and gameplay code. **Renderer:** the part responsible for turning scene data into images. **Authoring tools:** the programs that create, inspect, test, and package game content. **API:** a documented way for another program to request operations and get results.

An open-source runtime makes its source available under a license allowing reuse and modification. Godot is one example. We can reuse it without requiring creators or agents to use its graphical editor. We would still depend on its scene model and runtime behavior.

A custom runtime assembled from libraries is a different project: we borrow pieces such as graphics access and physics but design how they form a complete engine. That buys greater architectural control and adds substantial work. “New authoring product” and “new runtime” should be chosen deliberately rather than conflated.

There is also a useful correction to the starting premise: established engines already have automation surfaces. [Unity supports batch mode and executing editor methods](https://docs.unity3d.com/6000.0/Documentation/Manual/EditorCommandLineArguments.html), and [Unreal supports headless Python commandlets](https://dev.epicgames.com/documentation/unreal-engine/scripting-the-unreal-editor-using-python). An agent need not click through every operation. Our opportunity is to make the entire authoring and verification workflow coherent, discoverable, and reliable for programs from the beginning.

## Foundation alternatives considered

| Foundation | What we inherit | Fit and remaining work | Assessment |
| --- | --- | --- | --- |
| Godot | C++ runtime, dedicated 2D/3D systems, scenes, animation, physics, audio, UI, import/export, OpenXR | New authoring API, stable project conventions, compact diagnostics, observation/testing, minimal preview UI; adapter must respect native semantics | Considered for speed to a usable product; not selected |
| Bevy | Rust runtime, ECS, 2D/3D rendering, plugins, assets, scene serialization, remote inspection | More integration and compatibility management; physics/navigation/scripting choices; mobile/XR qualification | Strongest code-first alternative |
| O3DE | C++ engine, Atom renderer, substantial 3D systems, scripting and automation | Larger integration surface; evaluate dedicated 2D needs; some target platforms experimental | Useful reference, less suited to this first scope |
| Custom C++ + libraries | Selected graphics/platform/physics/audio building blocks | We own world model, animation, materials, tools, assets, UI, packaging and much more | Selected by the creator for architectural ownership |

Godot's [feature catalog](https://docs.godotengine.org/en/stable/about/list_of_features.html) and [FAQ](https://docs.godotengine.org/en/stable/about/faq.html) document its systems, languages, MIT licensing, and target platforms. Different renderers have different feature ceilings. Native extensions use [GDExtension](https://docs.godotengine.org/en/4.5/tutorials/scripting/gdextension/what_is_gdextension.html); verify compatibility with the selected engine release and each target. This older versioned page establishes the mechanism, not our future version choice.

Bevy's [official feature overview](https://bevy.org/) and [repository](https://github.com/bevyengine/bevy) document Rust, MIT/Apache-2.0 licensing, and 2D/3D capabilities. Its README also warns of missing features and frequent breaking releases. Particularly relevant is the [Bevy Remote Protocol](https://docs.rs/bevy/latest/bevy/remote/index.html): JSON-RPC inspection and mutation of reflected world state, discovery, and custom methods. It is an excellent design reference, but persistent source edits, safe transactions, tests, and packaging still need a product layer. Its remote plugins are opt-in; not every component is automatically reflectable.

O3DE's [features](https://o3de.org/features/), [repository](https://github.com/o3de/o3de), and [editor automation](https://www.docs.o3de.org/docs/user-guide/editor/editor-automation/) show that it is extensible and automatable. Its [platform table](https://www.docs.o3de.org/docs/welcome-guide/supported-platforms/) marks macOS and iOS experimental. Audit optional dependencies individually; a permissively licensed core does not make every optional integration free/open source.

## Language decisions

No mainstream programming language inherently makes an engine accessible to an LLM. Small APIs, examples matching the installed version, type information, readable files, good error locations, and quick checks give the agent more useful feedback than a language label does.

**Tooling:** TypeScript remains useful for a thin optional orchestration/MCP adapter. Native C++ operations own engine semantics, so we do not implement a second scene model in TypeScript. The shipped game does not need Node.js. [TypeScript is Apache-2.0 licensed](https://github.com/microsoft/TypeScript/blob/main/LICENSE.txt); [Node.js includes its own and bundled dependency notices](https://github.com/nodejs/node/blob/main/LICENSE).

**Selected:** C++20 for engine code and Luau in interpreter mode for ordinary game behavior. C/C++ interfaces connect naturally to the selected libraries. Luau's types and checker supply useful feedback for generated scripts once we provide typed engine bindings. Ordinary Lua is a fallback if qualification reveals a concrete blocker; it is not a second scripting language to support initially.

**Rust is credible**, especially with Bevy/wgpu. Safe Rust's [ownership model](https://doc.rust-lang.org/book/ch04-00-understanding-ownership.html) provides memory-safety guarantees, while native FFI remains a separate responsibility. With a C/C++-heavy stack, integration and target-toolchain validation can offset some of its advantages. The recommendation is about this dependency graph, not presumed model skill at C++ versus Rust.

**Python is useful for offline asset scripts**, but is not the proposed frame-loop language. **C# can support productive gameplay**, but it adds another runtime and AOT/export considerations; it does not resolve the API design problem. Keep routine prompt-driven changes in scripts and data so they do not trigger a native engine rebuild.

## Renderer selection and alternatives

Use one primary graphics foundation. Combining these would multiply resource ownership, build, and driver-testing work.

| Candidate | License | What it provides | Why choose it / limitation |
| --- | --- | --- | --- |
| [bgfx](https://github.com/bkaradzic/bgfx) | BSD-2-Clause | C/C++ rendering abstraction across multiple graphics APIs | Selected baseline, conditional on the early OpenXR integration gate; it explicitly expects us to bring the engine |
| [wgpu](https://github.com/gfx-rs/wgpu) | MIT OR Apache-2.0 | Rust GPU abstraction with native and web backends | Natural with Rust; GPU access rather than a finished scene renderer |
| [Filament](https://github.com/google/filament) | Apache-2.0 | Physically based 3D renderer, materials, glTF tooling | Useful when attractive PBR 3D quickly is the priority; still needs game systems and deliberate 2D support |

Their public target lists do not establish turnkey support for every current console. For VR, first verify access to native devices/queues, external textures and swapchains, stereo/multiview needs, and frame timing. The convenient desktop abstraction must also permit the chosen XR integration. The selected stack document makes this an early decision gate.

## Reusable custom-runtime components

This was the initial candidate inventory. See the selected stack document for current integration status and phase assignment. Repository licenses below summarize the main project; the alpha's license copies and nested notices are recorded in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Future additions need the same revision-specific review.

| Component and source | Main license | Proposed role and remaining integration |
| --- | --- | --- |
| [SDL3](https://github.com/libsdl-org/SDL) | zlib | Windows, input, controllers, lifecycle and platform layer; [console paths include restricted forks](https://wiki.libsdl.org/SDL3/README-platforms) |
| [Flecs](https://github.com/SanderMertens/flecs) | MIT | Entity/component organization, queries, reflection and serialization; keep stable saved IDs above runtime entity handles |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | MIT | 3D collision, rigid bodies and character foundation; integrate contacts, transforms, steps and save behavior |
| [Box2D](https://github.com/erincatto/box2d) | MIT | Dedicated 2D physics; separate world and capabilities from Jolt |
| [Luau](https://github.com/luau-lang/luau) | MIT | Typed Lua-derived scripting, checker/linter, embeddable VM; write and maintain typed engine bindings |
| [Lua](https://www.lua.org/about.html) / [license](https://www.lua.org/license.html) | MIT | Small portable interpreter alternative; lacks Luau's built-in type-checking workflow |
| [miniaudio](https://github.com/mackron/miniaudio) | Public domain OR MIT-0 | Audio playback, mixing, resource management and spatialization; choose one audio owner instead of duplicating SDL audio work |
| [cgltf](https://github.com/jkuhlmann/cgltf) | MIT | glTF/GLB parsing; renderer upload, animation evaluation and texture processing remain our work |
| [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) / [license details](https://github.com/KhronosGroup/OpenXR-SDK/blob/main/COPYING.adoc) | Main headers/loader Apache-2.0 OR MIT; file-specific terms | Shared XR API/loader; actual runtime, headset interactions and device qualification are separate |

Luau is derived from Lua 5.1 and is not a drop-in replacement for every modern Lua library. Its [performance documentation](https://luau.org/performance/) describes an interpreter with optional native generation; do not make JIT a platform requirement. Its [sandbox design](https://luau.org/sandbox/) requires careful host bindings: a game script should not automatically inherit the development agent's filesystem powers.

This list does **not** complete a custom engine. The selected stack now adds animation, navigation, UI, and profiling libraries, while our engine still owns their integration, particle/material systems, resource lifetimes, networking semantics, and platform services. Choose further dependencies against concrete requirements rather than treating the remaining work as trivial glue.

## Reuse in the alpha and next milestones

Study Godot's scene format and CLI as examples of inspectable projects and unattended workflows. Study Bevy's remote protocol for discoverable queries and mutation. Use [JSON Schema](https://json-schema.org/draft/2020-12) for our command/data contracts and the [MCP tool specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools) for the optional external-agent adapter. Use [glTF](https://www.khronos.org/gltf/) for asset exchange, not as a substitute for game logic and project metadata.

Implementation is now authorized. A pinned subset of these libraries has been downloaded inside `.deps/` and integrated into the alpha, with tested commands and primitive reference games exercising the authoring contract. The alpha's evidence covers its current Windows configuration; a downloaded or linked library alone does not establish broader feature or platform readiness.

For reference-game art, [Kenney's asset packs](https://kenney.nl/support) offer CC0 game assets, while [Poly Haven](https://polyhaven.com/license) provides CC0 models, textures and HDRIs. Retain the actual asset's license and source record. Website/API access terms are separate from asset reuse rights; neither catalog should become a mandatory online build dependency.

## Open-source policy proposal

Prefer maintained permissive dependencies for the engine: MIT, Apache-2.0, BSD and zlib families are present in the shortlist. Keep exact license text, attribution, version/hash and source URL in the dependency record. Evaluate copyleft tools in context rather than assuming either automatic incompatibility or automatic permission to embed them.

Code licenses, asset licenses, model licenses, vendor SDK agreements and store terms are separate. Our engine should not depend on a proprietary model service. Closed vendor SDKs, operating-system components and device drivers cannot all be replaced with open-source libraries; the platform plan makes that boundary explicit. Original Langmake code and examples now use the [MIT license](../LICENSE); third-party terms remain separate.
