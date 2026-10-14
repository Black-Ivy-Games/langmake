# UI layout: human direction, agent-readable files

The optional Studio **UI editor** tool helps the human adjust existing game interfaces and communicate exact intent. External agents remain first-class developers. The editor writes ordinary RML inline styles, preserves IDs/actions/bindings and never connects a model to the game runtime.

## Human workflow

1. Open your game in Studio and stop any human Play session or active worker. Click **UI editor** next to Play. No provider connection is needed.
2. Choose a game profile and an active RML document, or leave the document at its default. Choose a screen size/density; phone/tablet presets only set dimensions, not the game's profile. Optionally set a safe rectangle in physical pixels and a simulation tick/input timeline. Click **Open draft**.
3. The image comes from Langmake's renderer in an isolated cooked copy. Click an element in the image or choose its stable ID in the list. No game input is sent through the canvas. Elements without IDs must first receive stable IDs in source from an agent.
4. Edit offsets, dimensions, margins, padding, font size, colors, opacity, drawing order or text alignment. Filled values are inline overrides; placeholders show native computed properties. Clearing a field removes its override and restores stylesheet behavior. Click **Update draft**.
5. Absolute-positioned elements can be dragged, or resized from their lower-right handle. Snapping rounds movement to 8 dp; turn it off for 1 dp movement. Anchor presets preserve the current position while choosing top/bottom and left/right anchoring. Flow elements use property edits until you explicitly choose an absolute anchor. Native captures refresh after the edit completes; the moving outline is provisional.
6. **Undo/Redo** operates on the draft. Review original/draft source under the preview. **Apply to game** atomically replaces this one RML document after validating the draft and checking the original project revision. Studio takes a source checkpoint before applying. Use History to restore an applied edit, or open a new draft for further work.
7. Select elements, add notes and optionally **Pin this design intent**. **Export agent review** writes a screenshot and structured review to the game, plus the discoverable `ui-design.json` pointer. Export before Apply to propose a design, or after Apply to document an adopted one. Export does not send a message or call a model.

Closing the editor retains its draft. Use **Saved drafts → Resume** after restarting Studio. Discard old drafts when finished; discarding never reverses an already applied change. If a native operation fails, read its diagnostic and refresh the draft before proceeding. Do not remove an operation lock while its process is running.

## Agent workflow

Before changing UI, read the game's `ui-design.json` if present, the linked review/desired RML and screenshot, and the human's current instruction. Reviews identify the source document, stable IDs, viewport/profile/tick, native geometry, original/desired hashes and per-element notes. `applied:false` means a proposal, not already installed game content. Preserve pinned design intent unless the human changes it; pins are advisory instructions, not permissions that override the developer.

Do not recreate a reference screenshot as unrelated sprite art. Work in the game's registered UI documents and styles, keep button actions and state bindings intact, and use native captures to verify the requested appearance. If the game changed since a review, reconcile the current source with the human's intent rather than replacing it blindly with `desired.rml`. Reviews are ordinary, versionable project files and do not enter the runtime source graph unless a game explicitly registers them.

Agents need no Studio installation. The optional Node 22.12+ service exposes the same operations:

```powershell
node scripts/ui-editor/ui-editor.cjs .cache/ui-request.json
```

Example request (use actual authorized paths):

```json
{"root":"PATH_TO_AUTHORIZED_GAME","engine":"PATH_TO_LANGMAKE_EXECUTABLE","action":"open","config":{"profile":"phone","width":780,"height":1440,"density":2,"tick":0,"safe_area":"0,48,780,1344"}}
```

`open` returns a `session`, `version`, selected document, inline properties and native capture sidecar. CLI output omits image base64; use `.langmake/ui-editor/SESSION/` and the capture filenames in `session.json`, or `export` to obtain ordinary evidence paths. Every mutating request includes the last returned session/version:

```json
{"root":"PATH_TO_AUTHORIZED_GAME","engine":"PATH_TO_LANGMAKE_EXECUTABLE","action":"patch","session":"RETURNED-UUID","version":1,"id":"pause-button","properties":{"right":"24dp","width":"120dp","color":"#ffffff"}}
```

Actions are `discover`, `open`, `read`, `capture`, `patch`, `undo`, `redo`, `note`, `apply`, `export`, `discard`. `note` takes `id`, `text` and boolean `locked`. `read` needs the session but not a version. `discover` lists profiles, documents, scenarios and saved drafts. `config.input` can reference an ordinary timeline or a scenario with an `input` member. A new configuration requires a new draft; use the same tick and input to compare layouts reproducibly. A draft can be continued by Studio or the CLI using its shared on-disk session. Studio workers can read reviews through their normal file/image tools; provider calls do not receive unrestricted editor process execution.

## Boundaries and recovery

- Existing stable-ID elements only; one root UI document from the selected profile per draft. Scene-specific UI documents outside that root list require agent source editing. No widget creation, arbitrary reparenting, text/content editing, shared stylesheet rewriting or keyframe editor. Agents handle structural changes in text. A shared document change affects every profile/scene that references it.
- Supported numeric units are dp, px and percent. Size/padding must be nonnegative. Colors accept hex and a small named subset; complex inline functions remain a source-editing task. There is no automatic conversion of arbitrary CSS into an editable design model.
- Dragging uses simple absolute offsets; transformed elements/ancestors require source positioning and are excluded from drag/anchor changes. flow positioning remains intentional. Preview selection is based on native element bounds, not a browser DOM. Overlapping controls can be selected unambiguously from the ID list. Native scale, safe areas and dynamic state come from the selected capture.
- Presets are Windows host layout checks, not Android/iOS font, input or device qualification. Test the actual game on its target hardware before claiming it matches there.
- Eight saved drafts per game, 30 edits per draft, 1 MiB per RML file, at most 128 element notes (2,000 characters each), viewport 128..2560 pixels per axis, density 0.25..8, simulation tick 0..3600. Each draft contains a cooked source graph subject to the runtime's existing 256 MiB project budget. Discard completed drafts to reclaim storage. Review exports persist intentionally and are managed as game authoring files.
- Studio blocks editor work while its game is playing or another worker holds the project. External agents must also coordinate independently launched play sessions. The isolated preview itself never attaches to a live human session. Apply checks the complete source revision and selected document hash; it refuses intervening source edits.
- Per-operation locks and atomic file replacement prevent conflicting editor writes and partial RML saves. A process crash can leave `operation.lock`; verify the operation has stopped before removing that lock. Saved sessions retain the original and draft source for recovery. External noncooperating writers still require coordination; revision checks are not a filesystem-wide transaction.

No engine-owned model connection, new third-party library or mandatory Node/Studio dependency is added to exported games. Read [qualification](35-ui-editor-qualification.md) for executed checks.
