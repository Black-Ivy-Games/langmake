# Third-party notices — Langmake 0.4.1

Langmake uses the following free and open-source components. Complete applicable notices are included in `licenses/`; these terms remain attached to the third-party code and assets independently of Langmake's own license. `dependencies.lock.json` records the exact checked-out source revisions and the hashes and provenance of the license copies.

## Native dependencies

| Component | Use in this release | Upstream / terms | Included notice |
| --- | --- | --- | --- |
| SDL3 | Windows and Android platform adapters, events and input | [SDL](https://github.com/libsdl-org/SDL), zlib | `licenses/SDL/LICENSE.txt` |
| Luau and its Lua foundation | Gameplay compiler and interpreter | [Luau](https://github.com/luau-lang/luau), MIT | `licenses/Luau/LICENSE.txt`, `licenses/Luau/lua_LICENSE.txt` |
| Jolt Physics | 3D physics | [Jolt](https://github.com/jrouwe/JoltPhysics), MIT | `licenses/JoltPhysics/LICENSE` |
| Box2D | 2D physics | [Box2D](https://github.com/erincatto/box2d), MIT | `licenses/Box2D/LICENSE` |
| Flecs | Entity/component storage | [Flecs](https://github.com/SanderMertens/flecs), MIT | `licenses/Flecs/LICENSE` |
| JSON for Modern C++ | JSON scene and command data | [nlohmann/json](https://github.com/nlohmann/json), MIT | `licenses/nlohmann-json/LICENSE.MIT` |
| miniaudio | Audio playback | [miniaudio](https://github.com/mackron/miniaudio), MIT No Attribution option selected | `licenses/miniaudio/LICENSE` |
| bgfx | Rendering | [bgfx](https://github.com/bkaradzic/bgfx), BSD-2-Clause | `licenses/bgfx/LICENSE` |
| bx | bgfx support library | [bx](https://github.com/bkaradzic/bx), BSD-2-Clause | `licenses/bx/LICENSE` |
| bimg | Image utilities used by bgfx | [bimg](https://github.com/bkaradzic/bimg), BSD-2-Clause | `licenses/bimg/LICENSE` |
| bgfx.cmake | Build integration | [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake), CC0 dedication | `licenses/bgfx.cmake/LICENSE` |
| Manifold | Procedural mesh construction and solid boolean operations | [Manifold](https://github.com/elalish/manifold), Apache-2.0 | `licenses/Manifold/LICENSE`, `licenses/Manifold/AUTHORS`, `licenses/Manifold/linalg-NOTICE.txt` |
| LodePNG | PNG screenshots, sprite sheets and source-image decoding | [LodePNG](https://github.com/lvandeve/lodepng), zlib-style terms; vendored by bimg | `licenses/LodePNG/LICENSE` |
| ozz-animation | Skeleton construction, animation sampling and blending | [ozz-animation](https://github.com/guillaumeblanc/ozz-animation), MIT | `licenses/ozz-animation/LICENSE.md` |
| RecastNavigation | Navigation mesh construction, paths and crowd movement | [RecastNavigation](https://github.com/recastnavigation/recastnavigation), zlib | `licenses/RecastNavigation/License.txt` |
| RmlUi | Screen-space document layout, controls and input | [RmlUi](https://github.com/mikke89/RmlUi), MIT | `licenses/RmlUi/LICENSE.txt`, `licenses/RmlUi/containers-LICENSE.txt` |
| FreeType | UI font rasterization | [FreeType](https://freetype.org), FreeType License option selected | `licenses/FreeType/FTL.TXT`, `licenses/FreeType/LICENSE.TXT` and nested notices |
| cgltf | glTF and GLB parsing | [cgltf](https://github.com/jkuhlmann/cgltf), MIT; vendored by bgfx | `licenses/cgltf/LICENSE` |
| stb_image | PNG/JPEG decoding for imported models | [stb](https://github.com/nothings/stb), MIT option selected; vendored by bimg | `licenses/stb_image/LICENSE.txt` |
| LatoLatin Regular | Default UI font | [Lato](https://www.latofonts.com/), SIL Open Font License 1.1 | `licenses/LatoLatin/LICENSE.txt` |

This software is based in part on the work of the FreeType Team. Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved. The FreeType License is the selected license for this build; the separately licensed driver and helper notices are retained under `licenses/FreeType/`.

The unmodified `fonts/LatoLatin-Regular.ttf` is Copyright (c) 2010–2015 Łukasz Dziedzic, with Reserved Font Name Lato. Its complete SIL Open Font License and upstream attribution are included in `licenses/LatoLatin/LICENSE.txt`. That upstream notice also lists other sample fonts; only LatoLatin Regular is included in this package. The `Langmake` font-family alias is a runtime lookup alias, not a modification or renaming of the font file.

The `shaders/vs_cubes.bin` and `shaders/fs_cubes.bin` files are unmodified DXBC shader binaries from bgfx's example runtime assets. They remain covered by the upstream bgfx notice, including Copyright 2010–2026 Branimir Karadzic. Their source repository revision is the bgfx entry in `dependencies.lock.json`. The executable also embeds the unmodified DXBC arrays `vs_debugdraw_fill_texture_dxbc`, `fs_debugdraw_fill_texture_dxbc`, `vs_debugdraw_fill_lit_dxbc` and `fs_debugdraw_fill_lit_dxbc` from bgfx's generated debugdraw shader headers, under the same BSD-2-Clause notice. `embedded_upstream_assets` records their complete source-header hashes. Langmake's examples, procedural graphics recipes and synthesized music scores are original project content; no external character, mesh, SoundFont or texture asset packs are bundled. The optional music examples now contain original arrangements rendered from the CC0 instrument recordings described below.

## Nested component notices

- Manifold includes its modified `linalg.h`, under Apache-2.0, based on Sterling Orsten's public-domain linalg 2.2. The complete upstream attribution header is retained in `licenses/Manifold/linalg-NOTICE.txt`. This build disables Manifold parallel processing, cross-sections, bindings, tests and automatic dependency downloads; it does not add TBB, Clipper2 or Python bindings.
- SDL contains HIDAPI; the BSD option is selected. Its license selection text, BSD notice and original notice are preserved in `licenses/SDL/hidapi/`. SDL's yuv2rgb BSD-3-Clause notice is preserved in `licenses/SDL/yuv2rgb/`.
- bimg's ASTC encoder is Copyright 2011–2024 Arm Limited under Apache-2.0; its full license is in `licenses/bimg/astc-encoder/`. bimg's miniz copy is MIT, with its copyright and permission notice in `licenses/bimg/miniz/`.
- The bgfx source includes Microsoft's DirectX headers (MIT), stb components (MIT option selected), and Khronos graphics headers. Notices are in `licenses/bgfx/directx-headers/`, `licenses/bgfx/stb/`, and `licenses/bgfx/KHRONOS-HEADERS.txt`.
- bx's TinySTL notice is retained in `licenses/bx/tinystl/`. Retaining a bundled component's notice does not imply that all of its optional functions or backends are enabled in this release.
- RmlUi's MIT-licensed robin_hood and itlib container notices are retained together. Its samples, test dependencies, alternate rendering backends and Lua bindings are disabled; Langmake supplies its own SDL3/bgfx adapter.
- FreeType uses its bundled zlib decoder and built-in BDF/PCF/hash helpers; their applicable notices are retained. External zlib, bzip2, libpng, HarfBuzz and Brotli integration is disabled. Attribution for the small vendored HarfBuzz source sections is retained even where the optional integration is disabled.
- ozz-animation tools, FBX/glTF conversion tools, samples and tests are disabled. Langmake's import path uses cgltf directly and does not bundle Autodesk's FBX SDK, GLFW or ozz's optional tool dependencies. RecastNavigation demos, examples and tests are disabled.

The Android adapter selects the corresponding unmodified `_essl` arrays from the same upstream debugdraw headers and ESSL cube binaries from bgfx's runtime assets. Langmake compiles its own model shaders for `300_es` using the pinned host shader compiler. These shader adapters are implemented; native Android and device qualification are recorded separately in the release evidence.

Other optional upstream tools, example executables and platform SDKs are not included in the player bundle merely because their source may exist in a dependency checkout. The runtime package supplies Windows Direct3D 11 and Android OpenGL ES 3 players. Actual device coverage is recorded in the Android qualification guide; including upstream notices does not establish additional device support.

## Optional authoring and Android build modules

Langmake Studio is separately packaged using Electron. Its module has its own `studio/package-lock.json` and `studio/THIRD_PARTY_NOTICES.md`; its distribution retains Electron's complete notices and a dependency inventory. Studio does not bundle Codex or Claude Code executables or a proprietary model SDK. These integrations use a developer-supplied executable or documented HTTP API. No Studio/model dependency enters the C++ game runtime.

Android build tools are downloaded only during explicit setup, separately from the player distribution. `scripts/android/toolchain.lock.json` records pinned JDK, Gradle, SDK, NDK and optional emulator sources/checksums. Installed toolchain archives retain their upstream notices. Google's SDK license requires explicit acceptance before installation; these vendor tools are not described as Langmake's FOSS runtime libraries. Android binaries use the static C++ runtime from NDK 28.2.13676358, built with its Clang 19.0.1. The selected NDK's complete `NOTICE` and `NOTICE.toolchain` files are preserved in `licenses/Android-NDK/`, hashed in the dependency lock and included in exported APK assets. These cover the toolchain's applicable LLVM, libc++, libc++abi, compiler runtime and nested component terms; the full toolchain is not included in the player distribution.

## Compiler runtime

The Windows build uses MinGW-w64 GCC 15.1 and static runtime linkage, including libgcc and libstdc++. The applicable GCC GPLv3 text and GCC Runtime Library Exception 3.1 are retained in `licenses/GCC/COPYING3` and `licenses/GCC/COPYING.RUNTIME`. These texts were obtained from the GCC 15.1 release sources at the URLs recorded in the dependency lock.

The build also selects static winpthreads linkage. Its MIT and incorporated Lockless BSD notices are retained in `licenses/winpthreads/COPYING`. General MinGW-w64 terms are retained in `licenses/mingw-w64/COPYING`. The revision used to obtain these upstream notice texts is recorded separately from installed toolchain provenance; it is not presented as the exact source revision of the preinstalled runtime library.

Packaging inspects imported DLLs using the existing objdump tool. Any non-system dependency must be present within the selected project-local build directory and have its redistribution notices included; unresolved imports stop packaging. It does not search external runtime directories or redistribute Windows system DLLs. Packaged files have SHA-256 hashes in `package-manifest.json`. GCC, CMake, Ninja, Git, Python and objdump executables are build tools and are not included in the player bundle.

## Optional recorded-instrument authoring (audio feature revision 1)

The optional host sampler is [sfizz 1.2.3](https://github.com/sfztools/sfizz/tree/4e70dc0bef53b41f2853ed46e26f5911114c92d0), BSD-2-Clause. Its license, authors and nested component notices are preserved under `licenses/sfizz/`, with pinned source/submodule revisions and build options in `dependencies.lock.json`. It uses bundled Abseil, GHC filesystem, SIMDe, st_audiofile/dr_libs/libaiff/WavPack, atomic_queue, cephes, kiss_fft, hiir, tunings, pugixml, spline and cpuid components. Complete notices for disabled test/tool components are retained too; this does not imply they are linked. Generated Faust DSP comes from the pinned sfizz source; no Faust compiler is distributed. JACK, plugins, MIDI renderer, tests, demos and development tools are disabled. Upstream is archived, so Langmake isolates this optional dependency behind an offline adapter.

Windows `music-tools/bin` includes the sfizz DLL and its selected compiler's libgcc, libstdc++ and winpthreads DLLs. The GCC Runtime Library Exception and MinGW/winpthreads notices above apply to these redistributable runtimes as well. Source is available from the pinned upstream repositories; sfizz is not modified. The runtime player and exported games do **not** load this sampler or need these authoring DLLs.

The separately downloaded, curated [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE/tree/440300901dfe9275fd84e0b7763af1f8443ae62e) instrument palette is CC0-1.0. Its complete waiver/license and the upright piano's upstream credit are in `licenses/VSCO-CE/`. `scripts/music/packs/vsco-ce.json` records every selected sample's source path, byte count, SHA-256 and Git blob hash. The piano recordings credit Simon Dalzell / Ivy Audio. Original Langmake example arrangements and their baked stems are included for technical demonstration; recordings remain covered by the CC0 dedication. The large source sample bank is an optional workspace-local download, not a game or runtime-package dependency. Imported third-party recordings require their own project-recorded license; this pack's CC0 license does not cover arbitrary imports.
