# UI layout editor implementation

Authorized September 22, 2026. Keep game development agent-first: this is a human correction and communication tool over existing RML/RCSS, not a second game-authoring format or an engine-owned AI integration.

## Delivery phases

1. Extend native UI inspection with source document, parent bounds and editable style observations. Existing game behavior and formats remain unchanged.
2. Implement an optional, dependency-free Node authoring service and CLI. Create isolated native-cooked drafts; select a document/profile, viewport, density, safe area and simulation tick/input timeline. Use native captures and sidecars, never browser-rendered approximations of RmlUi. Bound sessions and history, protect paths, reject stale revisions and preserve originals until Apply.
3. Add a separate Studio UI-layout dialog. Select elements from native bounds or a list; edit dimensions, offsets, margins, padding, fonts, colors and drawing order. Offer anchor presets, snapping, drag positioning and resize for absolute-positioned elements. Flow layout remains supported through property editing; do not silently flatten a responsive hierarchy. Undo/redo drafts, inspect the resulting source diff and apply a single document atomically.
4. Export an agent-readable review with screenshot, viewport/profile/tick, stable element IDs, source revision, requested changes and advisory design locks. Store it in the game's ordinary files. External agents use the same CLI and files without Studio; Studio agents can discover the guide and review from their standard file tools.
5. Verify containment, conflicting edits, invalid drafts, undo/redo, save/reopen, play/worker locks and real native geometry. Exercise the actual Electron UI, inspect a screenshot, update documentation/prompts and produce separate runtime/Studio distributions without replacing earlier packages.

## Contracts and boundaries

- The authoritative result is a minimal inline-style edit to the selected RML opening tag. Preserve IDs, children, bindings, actions and shared stylesheets. Clearing a property removes its inline override and reveals the stylesheet value.
- Each draft edits one registered RML document. Selecting another document creates another draft. Apply is explicit; an atomic single-file replacement prevents partially applied multi-file layouts.
- Previews run in isolated copies, with audio and developer overlays disabled. They do not attach to, pause or reset a human's running game. Platform presets are host layout previews, not physical-device qualification.
- Native captures refresh on request or after a completed edit; dragging displays a provisional outline until the next capture. They are not a continuously embedded native game viewport.
- Safe numeric/property subsets have specific diagnostics. Unsupported transforms/layouts remain editable in source. This first release does not add widget creation, arbitrary DOM reparenting, animation authoring or a general vector design application.
- Design locks are explicit instructions for agents, not filesystem permissions. They never overrule a later human request. Review exports do not automatically send anything to a provider.
- No new third-party libraries or player dependency on Node/Studio. Node 22.12+ is only for the optional editor tools. Existing runtime UI remains portable; editor qualification is Windows/D3D11 and Studio.

Read the implemented guide and evidence before claiming a capability is ready. Creative acceptance remains the human's decision.
