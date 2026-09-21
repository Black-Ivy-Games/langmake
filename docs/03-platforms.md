# Platforms and distribution

Research checked September 17, 2026; implementation status updated September 19. Windows and Android have runnable alphas with documented technical evidence. Android has run on a Samsung SM-A146U and Android 16 emulator; see [Android qualification](20-android-qualification.md) for exact coverage and limits. The iOS source adapter and tools are implemented; native Apple build and device qualification remain pending. Read the [iOS plan](22-ios-plan.md) and [qualification record](24-ios-qualification.md). Other targets below remain future work; dependency support lists are not Langmake support claims.

The selected implementation direction is our C++20 runtime with Luau and the libraries in [Selected runtime stack](06-selected-stack.md). In particular, bgfx/OpenXR interoperability requires validation during the platform milestone on Windows PC VR and an Android headset; library-level support alone does not clear that gate.

The intended experience is: the creator names a target; the external agent selects the corresponding profile, checks prerequisites, adapts the game where necessary, then asks the engine to build and validate it. A profile can automate available operations. It cannot create vendor approval, credentials, development hardware, or a port that has not been implemented.

## Target matrix

| Target | Proposed order | Key integration and prerequisites | Evidence required before claiming support |
| --- | --- | --- | --- |
| Windows | Runnable alpha on development workstation | Direct3D 11 preview, input/audio, local folder packaging | Current evidence in the alpha document; clean-machine and wider device qualification remain |
| Linux | Desktop milestone | Graphics drivers, distribution/runtime dependencies, controller/audio behavior | Packaged builds on documented supported distributions |
| macOS | Desktop milestone | Metal-compatible path, Mac build/test access, signing/notarization for distribution | Intel/Apple Silicon configurations explicitly stated and tested |
| Android | Reference-device alpha qualified | Workspace SDK/JDK/Gradle, matching native player, SDL/GLES3, authored touch and lifecycle recovery | See [actual phone/emulator evidence](20-android-qualification.md); wider hardware and thermal qualification remain |
| iOS | Source adapter implemented, Apple qualification pending | Mac/Xcode, Metal, explicit destination, provisioning/signing and mobile lifecycle | Apple builds, simulator and physical-device checks pending; see [qualification](24-ios-qualification.md) |
| PC VR | XR milestone | OpenXR runtime, supported graphics binding, tracked input and stereo rendering | Named headset/runtime/backend combination, frame-time and comfort tests |
| Standalone VR | XR milestone | Mobile build path plus headset-specific OpenXR features/publishing | At least one named headset; battery/thermal, lifecycle and input tests |
| Xbox consoles | Authorized pilot | Partner access, GDKX, console adapter, dev hardware, platform services | Device-tested build, then title/certification milestones |
| PlayStation consoles | Authorized pilot | Approved access, agreement, SDK, adapter, dev hardware, services | Same, with individual hardware generations specified |
| Nintendo consoles | Authorized pilot | Portal registration and applicable platform approval, SDK/adapter, dev hardware | Same; Switch and Switch 2 must not be silently treated as identical |
| Native visionOS | Separate later evaluation | Apple immersive rendering integration and device-specific interaction | Immersive native build; a flat window or streamed game does not qualify |

Web export is a useful optional target for sharing demos, but it is additional scope with browser, threading, storage and graphics constraints. It was not an original required target and should not delay the first desktop proof.

## The free/open-source boundary

Keep the engine core, public authoring tools, formats, and ordinary third-party libraries free/open source. Define platform interfaces for graphics, input, audio, lifecycle, persistence, accounts/services, and packaging. Restricted vendor adapters and SDK material must be supplied through authorized channels rather than published in the public source tree.

[Godot's console documentation](https://godotengine.org/consoles/) illustrates this distinction: an open engine can support console releases, but SDKs and export templates depend on platform-holder approval and private distribution. A free adapter is not necessarily unrestricted open-source software. If the requirement means *every tool and SDK must be FOSS*, that conflicts with the desired licensed-console releases. This is a scope decision for the creator, not something a renderer library resolves.

## Verified vendor constraints

- **Xbox:** the [public GDK repository](https://github.com/microsoft/GDK) distinguishes PC access from licensed Xbox extensions. Microsoft's [publishing process](https://learn.microsoft.com/en-us/gaming/game-publishing/publishing-processes/managed-creators/overview) describes agreements, approvals and certification.
- **Nintendo:** the [development process](https://developer.nintendo.com/the-process) includes registration, agreements, platform access and publication review. The [developer portal](https://developer.nintendo.com/) distinguishes current platforms. Establish the exact authorized target before choosing or advertising an adapter.
- **PlayStation:** Sony's [onboarding guidance](https://sonyinteractive.com/en/news/blog/showing-your-game-to-playstation/) ties tools/documentation access to approved registration and the developer/publisher agreement.
- **Apple:** [membership capabilities](https://developer.apple.com/support/compare-memberships/) separate personal development from distribution services. [Developer ID/notarization](https://developer.apple.com/developer-id/) and [command-line signing guidance](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac/) inform the macOS build pipeline.
- **Android:** [signing documentation](https://developer.android.com/studio/publish/app-signing) covers installable packages and store signing. Compilation alone is not a complete distribution workflow.

Exact fees, agreements, certification details and SDK availability should be checked when opening each platform workstream. No vendor enrollment, restricted SDK acquisition or store submission is part of the Windows alpha. Its public FOSS dependency downloads stay inside the workspace.

## VR architecture

Use [OpenXR](https://www.khronos.org/openxr/) as the primary shared interface where supported, with the [official conformance list](https://www.khronos.org/conformance/adopters/conformant-products/openxr) as a starting point for selecting runtime/device combinations. Track graphics bindings, required extensions, action mappings, reference spaces, refresh rates, controller/hand capabilities and reprojection behavior.

OpenXR is not a renderer, a headset runtime, a complete interaction system, or a promise of every headset. Validate external swapchain handling and native GPU access during the platform milestone before claiming VR support. Windows/D3D11 0.3 does not qualify XR. Treat PSVR2 as a specific platform investigation; neither a Sony-branded entry in a runtime list nor PC OpenXR support proves console headset support.

Apple exposes [Compositor Services](https://developer.apple.com/documentation/CompositorServices) and its [visionOS rendering pipeline](https://developer.apple.com/documentation/visionOS/understanding-the-visionos-render-pipeline) for custom immersive rendering. Plan native visionOS separately from a generic OpenXR route. Streaming to a headset and rendering natively on it are distinct targets.

VR also requires intentional interaction design: locomotion and comfort, scale, readable UI, reach distance, tracked poses, stereo depth, and frame timing. The agent can apply a tested VR template, but automatically exporting a desktop game is not evidence of a good VR adaptation.

## Portable scripting and build hosts

Do not require JIT in the shipped gameplay path. A bundled interpreter or supported ahead-of-time compilation path is a safer baseline for broad targets. Microsoft's [runtime compilation documentation](https://learn.microsoft.com/en-us/dotnet/maui/deployment/runtimes-compilation?view=net-maui-10.0) illustrates iOS JIT restrictions, and [NativeAOT documentation](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/) describes relevant dynamic-code limits. Console rules still need authorized verification.

Ship behavior with the game package by default. Apple's [review guidelines](https://developer.apple.com/app-store/review/guidelines/) distinguish permissible app behavior from restrictions on downloaded code that changes functionality. Development hot reload does not establish permission for unrestricted post-release AI code rewriting on every device.

The authoring client can remain on Windows while requesting builds from appropriately provisioned workers later. Cross-platform source does not imply every target can be compiled, signed and tested on one Windows machine. Keep remote build support a later feature with explicit configuration. Signing keys should be accessed through credential references, not embedded in scene files, prompts or logs.

## Machine-readable target profiles

Each target profile should describe:

- Runtime/adapter version, build host requirements, SDK/toolchain discovery and packaging steps.
- Graphics features, shader/material limits, texture formats and supported renderer tier.
- CPU, GPU, memory, download-size and frame-time budgets.
- Input actions and device capabilities, UI scaling and lifecycle behavior.
- Scripting mode, persistence API, native services and network capabilities.
- Current support status and the last device-test evidence.

The proposed `target check` operation returns structured prerequisites and incompatibilities. Examples: “adapter not implemented,” “SDK unavailable,” “signing identity missing,” “material exceeds target feature tier,” or “package built, device validation pending.” It must report limitations rather than silently changing a game to fit a target.

Use explicit maturity states: **planned → buildable → device-tested → distribution-validated**. Store/certification approval is title-specific and separate from an engine backend passing internal tests.

One portable game design can share most code/assets, while target profiles select input/UI/rendering/content budgets. Platform portability should be designed in early and proven one target at a time.
