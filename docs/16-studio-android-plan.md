# Langmake 0.4 implementation: optional Studio and Android

Authorized September 18, 2026. Checked implementation items describe completed code and applicable host tests. They do not substitute for the explicit Android cross-build/device and distribution qualification gates below. See [current Android completion evidence](20-android-qualification.md) and the preserved [0.4.0 record](19-release-evidence.md).

## Product contracts

- The C++20/Luau/JSON runtime remains usable by any external coding agent with directory and command access. Studio is optional and never required to create, build or run a game.
- Studio owns projects, chats, provider integration and credentials in a separate module. Shipped games contain no model SDK, credentials or chat dependency.
- Produce a runtime-only distribution and a distribution including Studio; preserve the previous 0.3 package and authored games.
- The human owns creative decisions and primary gameplay playtesting. Technical automation must not interrupt an active human play session.
- All implementation downloads, caches, build products and evidence stay in this workspace. No global tool installation. Provider login and Android SDK terms are handled explicitly through their supported routes.

## Phase 1: shared contracts and build separation

- [x] Separate host build tools from the Android player target; preserve native Windows builds and tests.
- [x] Define the Studio broker/provider/tool interfaces, project registry and durable chat format.
- [x] Add Android target configuration and a versioned, verified game bundle.
- [x] Keep command discovery, schemas and external-agent instructions accurate.

## Phase 2: optional Studio

- [x] Separate desktop application: create/import/select projects, multiple persistent chats, provider/model selection, streamed progress, cancellation and recovery.
- [x] Codex, Claude CLI and local/API adapters, with documented credential/billing distinctions. Real Codex protocol and fake API integration tested; live authenticated generation still requires the human's account.
- [x] Project-scoped read/search/edit and native engine tools, bounded outputs, revision checks/checkpoints, changes and restore.
- [x] One active writer per project, explicit provider handoff, durable human decisions and task history.
- [x] Play/Stop and feedback workflow protecting human sessions; external agents remain documented first-class clients.
- [x] Credential isolation, local IPC confinement, integration tests, UI inspection and portable packaging.

## Phase 3: Android runtime

- [x] Cross-compile the same game/runtime systems for arm64-v8a and x86_64 with a pinned NDK and 16 KiB-compatible native linkage.
- [x] SDL Android entry/activity and bgfx OpenGL ES 3 renderer; backend-appropriate world, asset/model and UI shaders.
- [x] App-private content and saves, verified installation/update extraction, lifecycle suspension/resumption, audio and surface handling.
- [x] Touch joystick/buttons mapped to named game actions, pointer/UI interaction, scaling/safe display area, keyboard/controller support where present.
- [x] Bundle content through host commands, with source/asset revisions and bounded runtime loading.

## Phase 4: Android build and device workflow

- [x] Workspace-local toolchain bootstrap/doctor, explicit SDK license acceptance, pinned versions and checksums.
- [x] Implement game-specific Gradle generation and debug/release/signing commands with external key references. Actual debug/release APK and signed AAB exports passed in 0.4.1.
- [x] Explicit device selection, install, launch, stop, logs, screenshot and save/evidence retrieval through adb.
- [x] Expose documented command-line operations independently of Studio; connect Studio to the same operations.
- [x] Actual cross-build, package inspection and emulator/device installation when available. Record real hardware and lifecycle/input/render/audio evidence separately from compile results.

## Phase 5: integration and release

- [x] Existing native/runtime/graphics regressions, all seven starters, cooked/source parity where applicable.
- [x] Studio broker/provider protocol tests, project/chat persistence, cancellation, handoff, play-session protection and filesystem boundaries.
- [x] Android 2D and 3D rendering/UI/input, pause/resume, saves across process death/update, audio and rotation/display behavior.
- [x] Runtime-only and Studio Windows distributions with manifests, licenses, launchers, docs, prompt examples and measured evidence. 0.4.1 adds verified debug/release players for both ABIs, source-free export tests and physical-device evidence.
- [x] Separate assigned agent authored a new game using only packaged documentation/native commands. It passed positive/negative scenarios and capture/state parity without engine changes. This was not a context-free model exercise.

## Initial platform boundaries

Windows hosts authoring and Android builds. Android initially targets API 26+, OpenGL ES 3, arm64-v8a devices and x86_64 testing. Device compatibility is qualified only by actual evidence. Vulkan, iOS, XR, consoles, store submission and signing enrollment are separate milestones. API/provider authentication requires the developer's own supported credentials. SDK license acceptance or unavailable hardware must be reported as concrete prerequisites, never disguised as passed tests.

## Handoff requirements

Provide exact launch/build/install commands, package locations, controls, verified tests, remaining environment prerequisites and supported limits. "Ready for human playtesting" requires a runnable verified artifact; transport fixtures and successful compilation alone do not prove a live AI account or physical phone workflow.
