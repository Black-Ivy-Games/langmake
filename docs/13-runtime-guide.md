# Langmake 0.5.0 runtime and authoring guide

For human UI corrections and agent review exports, read [the UI layout guide](34-ui-editor-guide.md). Before UI changes, inspect the game's `ui-design.json` when present and preserve pinned human intent unless the developer changes it.

For acoustic/orchestral compositions, WAV/FLAC music and effects, synchronized adaptive tracks, and EQ/compression/limiting, read [the music and sound guide](30-music-and-sound-guide.md). Native music recipes now support versions 1 and 2; optional authoring tools bake text scores without adding sfizz or Python to games.

This documents the implemented Windows/D3D11 and Android/GLES3 alpha. Native macOS/Metal local packaging is documented in [the Mac guide](28-macos-guide.md); its qualification and public distribution are separate. Start with the [human-directed workflow and command contract](08-agent-api.md). The human directs the game and is its primary gameplay playtester; reference scenarios establish technical behavior, not design acceptance.

The [0.5.0 game workflow](26-game-workflow.md) documents shared presentation profiles, portable application icons/versions, custom held UI actions, viewport/glyph diagnostics, assessed presentation-only save compatibility and reusable verification. `workflow-2d` is an editable small example. Native platform evidence remains separate: see the Android/iOS qualification records and current release evidence before claiming a device behavior.

The same project format is used by the optional [Studio](17-studio.md) and [Android implementation](18-android.md). Mobile touch configuration is documented in the Android guide. See [Android qualification](20-android-qualification.md) for actual phone/emulator checks and device limits. Desktop controls and existing projects retain their behavior.

For an Android game, follow the [agent workflow and reading list](21-android-agent-guide.md) alongside this subsystem reference.

For iOS, read the [iOS agent workflow](23-ios-agent-guide.md) and [qualification record](24-ios-qualification.md). The same game formats run in the native Metal player for tested 2D/3D simulator scenarios, with sixteen state comparisons, persistence checks and adventure UIKit/Core Audio background recovery. The signed adventure also passes nine native capture/state comparisons on an iPhone 17 Pro Max. The human confirmed movement, interaction, music and readable text; physical persistence/lifecycle coverage and final release qualification remain pending. Simulator evidence, phone checks and initial human feedback are recorded separately.

## Project structure and reusable modules

Use schema version 2 for the expanded project format. Version 1 projects retain their compatibility rules. This manifest fragment shows the registries; all referenced files must exist inside the project:

```json
{
  "schema_version": 2,
  "project_id": "orchard-game",
  "save_version": 1,
  "dimension": "3d",
  "title": "Signal Orchard",
  "script": "scripts/game.luau",
  "entities": [],
  "entry_scene": "orchard",
  "scenes": {"orchard":"scenes/orchard.json","relay":"scenes/relay.json"},
  "modules": {"inventory":"modules/inventory.luau","quests":"modules/quests.luau"},
  "prefabs": {"worker":"prefabs/worker.json"},
  "definitions": {"items":"data/items.json","quests":"data/quests.json"},
  "assets": {"robot":{"type":"model","path":"assets/robot.model.json"}},
  "state": {"chapter":1},
  "seed": 42
}
```

Registry IDs are stable project-local names. A module returns a Luau table; `require("inventory")` loads only a registered module, caches its result per VM and diagnoses cycles. Use `engine.definitions()` to retrieve registered JSON data. Modules and all registered data participate in source revisions, checkpoints, copies and reloads even if not used by the active scene. UI and external model inputs belong to this graph too. Paths use `/`, stay inside the project, and cannot name reserved `.langmake` content, parent traversal or network locations.

New Windows/Android source builds can compile explicitly marked `--!native` Luau entry scripts and registered modules using the pinned Luau native compiler. Numeric annotations help hot loops. Unmarked code remains interpreted; unsupported platforms/functions or exhausted native allocation fall back to bytecode. Each VM has an additional bounded 8 MiB native code allocator. The existing 50 ms hook allowance and VM memory limit still apply, including native loop interrupts. New Windows source builds measure thread CPU and report elapsed time separately; native synchronous work is checked on return and can overshoot. See [script execution and diagnostics](39-script-execution-guide.md) for clock policies, bounded traces, profiling and failed-world reconstruction. Runtime statistics expose `script_native_functions` and `script_native_code_bytes`; measure the actual target rather than assuming a speedup. This does not qualify historical players or enable native compilation on Apple platforms.

The source sprite renderer packs compatible animation frames into padded GPU atlases and batches consecutive sprites sharing a texture. Stable draw order, alpha, flip and transforms are preserved; no asset schema or game code change is required. Frame counts and GPU allocation remain bounded. Large assets that do not fit an atlas use individual textures. Historical packaged renderers may still submit one draw per sprite.

Ordinary real-time play yields to presentation after approximately one 60 Hz frame's worth of simulation catch-up work (a single expensive tick can exceed that duration). Excess backlog is capped at one tick. This keeps rendering and input responsive under overload; simulation may slow on a device that cannot sustain its workload. Offline simulation, requested fixed-frame runs and explicit scenario ticks retain their exact fixed-tick behavior.

An entry script returns optional hooks:

The source runtime also supports an optional `on_recover()` hook for player recovery after an interrupted update. Opting in keeps a known-good world snapshot from initialization or the last completed game save/load. The player restores that snapshot transactionally, runs `on_load()` and then `on_recover()`, clears transient input, and resumes updates so the game can show a recovery dialog. Use the hook to freeze gameplay and offer a checkpoint restart; never continue the partially mutated failed update. Save checkpoints promptly through `engine.save_game` to limit lost progress. Recovery failures are bounded, and `script-failure.json` retains the original diagnostic. This is a source feature; historical packaged players do not implement it.

```luau
local Inventory = require("inventory")
return {
    start = function()
        engine.state_set("chapter", engine.state_get("chapter", 1))
    end,
    update = function(dt)
        if engine.input_pressed("interact") then
            engine.ui_set("coins", Inventory.count("coin"))
        end
    end,
    on_load = function()
        engine.ui_set("coins", Inventory.count("coin"))
    end,
}
```

Store persistent progression using `state_get(key, default?)` / `state_set(key, value)`. Reads return JSON copies; change a copy and write it back. Nil removes a state key. JSON state and UI state each have a 1 MiB budget, bounded nesting and value counts. Do not store save-critical progression solely in module/VM locals. `engine.tick()` is the fixed simulation clock; `engine.random()` uses a persisted seeded generator. Manifest `seed` accepts integers from 0 through 9007199254740991; zero or omission selects the engine's fixed nonzero default. Use `langmake check` for offline Luau checking and `script-api` for generated declarations. Runtime validation and relevant scenarios are still required.

## Scenes and persistent saves

A scene JSON file overrides `entities`, `script`, `camera`, `game_camera`, `gravity`, `music`, `audio`, `navigation`, `state` or `ui` from the root project. Source builds also support complete replacement of `assets`, `modules`, `prefabs` and `definitions` per scene; an omitted registry inherits the root, and an empty object clears it. Registry paths remain project-relative. The same ID may refer to different content in different scenes, and each resolved scene retains the 256-asset limit. Script checking, cooking, revision tracking and inactive-scene save validation include those local registries. This requires a newly built runtime; older packaged players reject these fields.

Only one scene runs at a time. `engine.change_scene("relay")` queues a transition at the tick boundary. `engine.scene()` returns the current scene ID. Scene registries do not isolate declared global state: assign each game's state its own keys. Asset discovery commands describe the selected entry scene. The full source graph still resides in memory, with bounded catalog/source-copy estimates; this is not on-demand disk streaming.

The first visit constructs the scene and calls `start()` while carrying global declared game state. A revisit restores that scene's saved entities/systems and calls `on_load()` rather than awarding startup items again. Scene construction happens before replacing the active scene, so a failed transition retains the prior world. A session supports at most 64 distinct visited scenes, including its active scene; existing scenes remain revisitable at that limit. This is not background simulation or streaming of inactive areas.

The convenience `won` and `message` fields belong to the scene being restored. Put whole-game completion in declared project state (for example, `state_set("story_complete", true)`) and derive the HUD from it when visiting another area; a different scene's `won:false` does not undo a persistent quest.

```luau
if engine.input_pressed("save") then engine.save_game("journey") end
if engine.input_pressed("load") then engine.load_game("journey") end
```

Player save requests are serviced by the running window. Slots contain 1..64 ASCII letters/digits/underscore/hyphen. Live CLI aliases are `save/load --project PATH --slot journey`. Headless workflows use `simulate --save-output FILE` and `--load-save FILE`; script requests are not a headless filesystem API.

A script requesting a save has queued work, not confirmed a disk write. Verify technical save behavior using the live command result and a fresh-process load. Keep save artifacts, simulation result JSON and shell command logs at different paths so an evidence logger cannot overwrite a save.

Saves include project identity, source revision, save version, scene states, declared game/UI state, entities/velocities/animation, camera, navigation orders, audio selections/bus controls, fixed clock and seeded randomness. They reconstruct systems; they do not serialize Luau heap, solver caches, device playback position or transient audio effect delay buffers. Save input is bounded to 16 MiB. Same-revision restore calls `on_load()` rather than `start()`.

`project_id` identifies the game; legacy projects fall back to the title. By default a source revision or `save_version` change requires an explicit entry-table migration hook. Version 2 can explicitly opt into assessed presentation-only compatibility with `save_compatibility`; structural fingerprints still reject gameplay changes. See [the workflow save contract](26-game-workflow.md#save-compatibility-and-the-next-agent) and use `save-check` plus fresh-process restoration. Changes outside that opt-in require migration:

```luau
migrate = function(saved, fromVersion, toVersion)
    assert(fromVersion == 1 and toVersion == 2, "Unsupported save migration")
    saved.state.chapter = saved.state.chapter or 1
    return saved
end
```

Migration receives and returns the complete saved document in an isolated sandbox VM. Update saved entities/scene data as needed when content changes; a passthrough is appropriate only after assessing compatibility. Engine-owned clock/RNG/spawn metadata is preserved. Missing/failing migration or invalid returned data leaves the existing game intact. Increment the version when deliberately changing your game save contract. A source checkpoint is a different feature: it restores authored files, not player progress.

Full-save migration conversion permits up to 1,048,576 JSON values so a valid
4,096-entity save is not rejected by the smaller per-state conversion limit.
The 16 MiB save, 16-level nesting, entity and field validation limits still
apply. Ordinary `state_set`/UI conversions retain their 65,536-value limit;
this does not increase the 50 ms script execution budget.
Bounded native decoding of a migration's input snapshot runs inside a protected
Lua call for allocation safety, before starting the authored hook's 50 ms clock.
The input is checked for supported data/nesting and still obeys save and VM
memory bounds. Infinite migration scripts remain subject to the same timeout.

## Entities, prefabs and collision

Entities have stable IDs, role, position, primitive shape/body, size and visual selection. New v2 fields include `visual_scale`, `collision_enabled`, `controller`, optional `parent` and `parent_joint`. Body types are `none`, `static`, `dynamic`, `kinematic` and `trigger`; shapes are box, sphere and capsule. Collidable spheres require equal axes; capsules require `size.y >= size.x == size.z`. A `character` controller requires a dynamic body.

```json
{
  "id":"player", "position":[0,1,0], "size":[0.7,1.8,0.7],
  "shape":"capsule", "body":"dynamic", "role":"player",
  "controller":"character", "asset":"robot", "animation":"idle",
  "visual_scale":[1.8,1,1.8], "collision_enabled":true
}
```

Visual geometry is separate from physics. By default, authored `rotation` rotates rendering and primitive physics bodies remain axis aligned. An explicit `physics2d` configuration opts a 2D body into shaped/rotating collision (see below). `size` defines the primitive collider, while `visual_scale` changes only the rendered object. Sprite/mesh bounds fit entity size; models retain authored units/pivots and multiply by size. Opacity/visibility do not remove colliders. Call `set_collision_enabled(id,false)` when an inactive entity should stop colliding or appearing in physical queries.

Register prefabs as JSON files containing one entity template or `{ "entities":[...] }`. `spawn("worker", {position={x,y,z}, role="worker"})` returns a reserved deterministic runtime ID; physics creation commits at the tick boundary. It is then usable for scripting in that update. At most 4096 live entities and 256 entities per prefab instance are allowed. Spawn cannot replace the authored player.

For shaped 2D stacking, an unparented body without a character controller may declare:

```json
"physics2d": {
  "rotate": true, "friction": 0.48, "restitution": 0.06, "angular_damping": 0.3,
  "polygons": [[[-0.4,-0.2],[0.4,-0.2],[0.3,0.3],[-0.3,0.3]]]
}
```

`polygons` overrides the primitive collider with 1–4 convex fixtures, each containing 3–8 strictly convex counterclockwise vertices in normalized sprite coordinates (-0.5..0.5, +Y up). Vertices scale with entity `size`, not `visual_scale`; omit polygons to retain the primitive. Omit tiny appendages rather than making pixel-level fixtures. Material ranges are 0..1; angular damping is 0..20. Only Z rotation is supported. With `rotate:true`, native Box2D inertia and contacts rotate the rendered entity. `rotation(id)` returns XYZ degrees; `angular_velocity(id)` and `set_angular_velocity(id,degrees_per_second)` expose spin (setter bounded to ±3600). `set_rotation` also sets the physical orientation for configured bodies. Saves preserve fixtures, angle and spin, including when collision is disabled. Debug collision captures draw polygon outlines. Legacy bodies and 3D retain their prior behavior. This extension has Windows host coverage; mobile builds/device behavior have not been qualified for it.

Multi-entity prefabs use local IDs and optional `parent` references, with at most 16 hierarchy levels. `overrides.entities` maps local IDs to per-part property overrides. Parent references are rewritten to instance IDs. Child positions/rotations/scales are local; translation, rotation, visual scale, visibility and opacity inherit. Children have no independent physics body. Rotated children require uniform parent scale; transforms requiring shear are rejected. `set_position(child,worldX,worldY,worldZ)` converts a world location back to its local position; child rotation/visual-scale setters remain local. Destroying a parent/instance root removes descendants and associated physics/navigation data.

For a model socket attachment, add `parent_joint:"hand"` beside `parent:"player"` on a body-less child. The named parent model joint contributes its sampled/blended translation, rotation and scale. Parent model size scales the joint anchor, and child local offsets follow the resulting transform. Missing joints and singular/sheared/reflected joint transforms are rejected; changing the parent asset revalidates attachments. This is a hierarchy attachment, not an IK solver.

| Runtime operation | Result |
| --- | --- |
| `entities(role?)`, `exists(id)` | Sorted live IDs and existence checks |
| `spawn(prefab, overrides?)`, `destroy(id)` | Bounded creation/destruction; destroy reports whether a live entity changed |
| `position(id)`, `distance(a,b)` | Three world coordinates; 2D distance ignores Z |
| `set_position(id,x,y,z)` | Teleport, clearing velocity |
| `set_velocity(id,x,y,z)` | Dynamic/kinematic movement as supported by body type |
| `set_asset(id,asset,clip?)` | Validated visual replacement and animation reset |
| `set_color(id,r,g,b)`, `set_opacity(id,a)` | Tint and alpha (0..1) |
| `set_visible(id,bool)` | Rendering participation only |
| `set_rotation(id,x,y,z)` | Degrees around XYZ; physical Z for configured `physics2d` bodies |
| `set_visual_scale(id,x,y,z)` | Additional positive visual scale, 0.001..1000 |
| `set_collision_enabled(id,bool)` | Remove/recreate physical participation independently |

`raycast(x,y,z,x2,y2,z2, options?)` returns `{hits=[{id,fraction,distance,point,normal}], truncated=...}`. `shape_cast` accepts the same arguments with `options.radius` for a sphere sweep. `overlap(x,y,z,radius,options?)` returns `{entities=[IDs], truncated=...}`. Options include `exclude={IDs}`, `role`, `limit` (1..256) and `sensors=false` to omit triggers. Results concern actual physics bodies, not visible pixels or detailed mesh triangles. `contacts(id?)` exposes bounded `enter`/`stay`/`exit` events with entity pair and tick; it includes sensor overlaps. See snapshots for current contact evidence.

## Character movement and gameplay cameras

The 3D capsule controller uses Jolt CharacterVirtual for grounded movement, slope and stair handling. The 2D capsule mover uses Box2D casts/sliding for floor, wall and ceiling behavior. `move_player(x,z)` controls the authored player horizontally; `jump(speed)` returns whether a grounded jump was accepted. `grounded(id)` exposes support. Use `set_velocity` for non-player bodies and top-down 2D motion. Controller movement is an engine foundation, not a finished genre-specific movement design; tune feel with the human.

Put game-owned camera settings in `game_camera`, distinct from the legacy developer orbit camera:

```json
{"game_camera":{"mode":"third_person","follow":"player","offset":[0,6,10],"smoothing":8,"obstruction":true,"fov":60}}
```

`camera_set({...})` merges validated changes and enables the game camera. `camera_get()` returns its current configuration/results. Modes are `fixed`, `follow`, `third_person`, `orthographic` and `lock_on`. Use `position`/`target` for fixed shots, `follow`/`offset` for tracking, `target_entity` for a lock target, and `half_height` for orthographic scale. Smoothing and obstruction update on fixed ticks; obstruction uses native casts. Lock-on frames the two actors and reports `target_locked`/`occluded`; destroyed/missing targets unlock safely. Captures record the actual rendered camera matrices. Developer orbit remains a preview aid, not an authored cinematic.

## Screen-space UI and input

RmlUi and FreeType render a dedicated UI pass after the world, independent of world depth/sprite sorting. Register every UI document, stylesheet, image and optional font in the source graph:

```json
{"ui":{
  "documents":{"hud":"ui/hud.rml"},
  "styles":{"main":"ui/hud.rcss"},
  "images":{"badge":"ui/badge.png"},
  "data":{"score":0,"healthbar":{"value":10,"max":10}}
}}
```

RML references are relative to the document, so `href="hud.rcss"` resolves beside `ui/hud.rml`. The built-in `Langmake` font is Lato Latin; broader scripts require appropriate registered fonts and their own qualification. This is RML/RCSS, not a browser; it has no network fetching, embedded JavaScript or HTML user-agent stylesheet. Set `display:block` on headings/paragraphs when desired, and `body { width:100%; height:100%; }` for bottom/right anchors.

Link each registered shared/profile stylesheet explicitly from RML, with shared rules first and profile rules afterward. Do not assume CSS `@import` or browser shorthand properties work. The `workflow-2d` documents demonstrate explicit links and `background-color`; inspect a native capture as well as validation before handing over a layout.

UI receives SDL's display density: use `dp` for density-independent dimensions and fonts, or `px` for physical pixels. The inherited default font scales in `dp`. On Android, authored HUD layout and generated touch controls both use the safe display area; rendering and hit-testing translate back to full physical coordinates. Native observations expose `ui_renderer.safe_area`, `insets` and physical element bounds. Existing `px`-sized HUDs can look small on a dense phone display, so author mobile fonts/buttons in `dp` and flexible anchors in percentages. Actual multi-monitor/high-DPI transitions still need hardware qualification; a headless layout check alone does not establish that behavior.

```xml
<rml><head><link type="text/rcss" href="hud.rcss"/></head><body>
  <div id="panel"><span id="score">0</span>
    <progress id="healthbar" value="10" max="10"/>
    <button id="continue">Continue</button>
  </div>
</body></rml>
```

```luau
engine.ui_set("score", 120)
engine.ui_set("healthbar", {value=8,max=10})
engine.ui_set("panel", {visible=true,class="ready"})
if #engine.ui_event("continue","click") > 0 then engine.state_set("chapter",2) end
```

Stable element IDs are the binding contract. Scalars set escaped text, or the value for form/progress elements; objects support `text`, `value`, `max`, `visible` and `class`. Replacing an element's text replaces its descendants; put binding IDs on leaf text containers. `ui_get(id)` and snapshots expose declared values headlessly. IDs without a rendered element can remain declared data, but have no visual effect.

Buttons/interactive elements emit `{type="ui",id="continue",event="click",value=""}`. `change` and focus-cancel events are also supported; `data-event` overrides the semantic event name. `ui_event(id,event?)` filters this tick's events; `input_events()` returns all semantic input. Mouse, Tab/Shift+Tab, Enter/Space, Escape, and gamepad D-pad/accept provide focus interaction. Focused interactive UI suppresses gameplay keyboard input. Passive HUD elements stay click-through. Basic rectangular clipping works; advanced CSS filters and rotated clipping are unsupported.

For a styled **held** action, add `data-action="attack"` to an element with a stable ID and register `attack` in `input.actions`. Read it with `engine.input("attack")`, `input_pressed` and `input_released`; ordinary click events remain available for discrete actions. `data-hit-padding="12"` expands the pointer region by 12 dp without changing the visible shape. Fully transparent controls stay interactive; hidden or disabled controls cancel the hold. Dragging outside releases it, and re-entering the captured control while still pressed restores the hold. Separate fingers retain their UI or native-stick ownership. Style the pressed state using `:active`. Focus loss, backgrounding and document replacement cancel active holds; technical state checks do not replace a human trying actual multitouch.

Use `ui-check --project GAME --profile phone --width 1920 --height 1080 --density 2 --safe-area 72,0,1776,1032` to inspect physical visual/hit bounds, overlap/offscreen warnings, action bindings and glyph coverage. Declare dynamic strings with `ui.expected_characters`, or add `--text` for a one-off check. `engine.viewport()` reports runtime physical/logical bounds when available; real device observations remain authoritative for density and safe areas. See [the game workflow](26-game-workflow.md) for profiles, registered scenarios and exact export commands.

Named action bindings are text-authored:

```json
{"input":{"actions":{
  "interact":{"keys":["E"],"gamepad":"south"},
  "attack":{"keys":["Space"],"gamepad":"west"},
  "horizontal":{"positive":["D"],"negative":["A"],"gamepad_axis":"leftx"}
}}}
```

Use SDL key/gamepad names. `input("interact")` returns a **number**, including zero; Lua zero is truthy, so use `>0` or `input_pressed("interact")`. `input_released` provides the opposite edge. Legacy `move_x`/`move_z` are numbers and `jump`/`fire` are booleans; `pointer_x`/`pointer_y` expose normalized coordinates. Fixed-input timelines and semantic UI events use the same gameplay input contract, allowing headless verification without clicking through a desktop.

## Native animated models and bounded glTF imports

Register a model as `{ "type":"model", "path":"assets/robot.model.json" }`. Original models can be authored from native solid geometry attached to joints. This small recipe animates an attached part entirely from text:

```json
{
  "schema_version":1,
  "joints":[
    {"id":"root"},
    {"id":"hand","parent":"root","translation":[0,1,0]}
  ],
  "parts":[{"id":"arm","joint":"hand","color":"#80C090",
    "geometry":{"op":"box","size":[0.25,0.8,0.25]}}],
  "animations":{"wave":{"duration":1,"loop":true,
    "tracks":{"hand":{"translation":[
      {"time":0,"value":[0,1,0]},
      {"time":0.5,"value":[0,1.4,0]},
      {"time":1,"value":[0,1,0]}
    ]}},"markers":[{"name":"top","time":0.5}]}},
  "default_animation":"wave"
}
```

Joints use local translation, quaternion rotation `[x,y,z,w]` and positive scale; absent values use identity. Parent order is arbitrary; cycles/missing parents fail. Each part uses the native mesh geometry grammar, a joint and a color. Joint tracks contain strictly increasing `{time,value}` keys for translation, rotation or scale. Durations/markers use seconds. Missing channels retain the rest pose. Native parts are rigidly attached; imported skins can have weighted deformation.

Sampling/blending uses pinned ozz-animation. Skinning currently runs on the CPU and sends deformed positions/normals to engine-authored bgfx shaders. This supports the 128 simple animated followers in the reference; it is not evidence for 128 arbitrary production characters. Native model units and joint pivots are preserved. Entity `size` and `visual_scale` multiply the model; they do not center-fit each bone's bounds.

```luau
engine.play_animation("player","walk",false,0.15)
for _, marker in ipairs(engine.animation_events("player")) do
    if marker.name == "strike" then engine.state_set("hit_tick",engine.tick()) end
end
local x,y,z = engine.joint_position("player","hand")
engine.set_position("held_effect",x,y,z)
```

`play_animation(id,clip,restart?,blendSeconds?)` supports model crossfades; `restart=false` avoids restarting the same clip every update. Model markers appear for the completed fixed interval `(previousTime,currentTime]`, including loop crossings, bounded to 128 events. `joint_position` returns a sampled/blended joint origin in world space for Lua-driven effects; use `parent_joint` for inherited visual attachments. Neither facility provides IK. Sprites retain their frame animation behavior.

For imported content, the recipe is a wrapper:

```json
{"schema_version":1,"source":"models/character.glb","default_animation":"Walk"}
```

cgltf reads `.gltf`/`.glb`; model file, external buffers and PNG/JPEG image inputs are registered dependencies, with base64 embedded data supported. The importer does not fetch remote resources. It supports triangle geometry, hierarchy/TRS, normals or computed normals, UV0, vertex colors, base-color factors/textures, opaque/masked/blended alpha, unlit/simple lighting, skin weights (four influences) and LINEAR TRS animation. Supported imports can come from any legally usable source, but exporting/converting unsupported FBX or other formats remains external work. Asset rights are the game's responsibility; the engine does not grant a license to downloaded art.

Unsupported inputs produce diagnostics: compressed geometry, sparse accessors, morph targets, STEP/CUBICSPLINE animation, unsupported required extensions, UV transforms and non-UV0 base-color mapping. There is no retargeting, root motion, IK, FBX importer or full PBR pipeline. No automatic conversion through Blender is required for native assets.

Per-model limits include 128 nodes/joints, 64 clips, 65,536 animation keys, 100,000 vertices/300,000 indices, 128 imported primitives or 64 native parts, and 4096×4096 decoded images. Project source/compiled-resource budgets may reject a model sooner. `asset inspect` reports geometry, joints, clips, markers and bounds; `asset render` shows the default sampled pose, while `asset export` emits posed OBJ geometry rather than an animated interchange bundle.

## Navigation and many-agent behavior

Navigation uses Recast/Detour and DetourCrowd. Configure one bounded ground-navigation mesh per scene:

```json
{"navigation":{"enabled":true,"cell_size":0.25,"cell_height":0.1,
  "agent_height":1.8,"agent_radius":0.3,"max_step":0.4,"max_slope":45}}
```

3D builds from axis-aligned static and kinematic primitive collider bounds; imported visual mesh triangles do not define navigation. Top-down 2D requires `bounds:[minX,minY,maxX,maxY]`, projects XY into the navigation plane and treats static and kinematic collider bounds as obstacles. Both initial construction and `navigation_rebuild()` include kinematic positions at the time of the build. Call `navigation_rebuild()` after changing obstacles to rebuild the mesh and replan orders. This is explicit rebuilding, not a streaming/dynamic tile-cache system.

`navigation_path(x,y,z,x2,y2,z2)` returns path status and points. `navigate(id,x,y,z,speed?)` assigns an order, with default speed 3 and maximum 100. `navigation_status(id)` observes path/goal/status (`ready`, `arrived`, `unreachable`); `navigation_stop(id)` cancels it. Native crowd steering handles local avoidance and separation; game modules define selection, formation and tasks. A tight destination still needs enough space for the requested group.

Limits are 256 active navigation agents, 256 path points and 4,194,304 raster cells. Paths may be partial/unreachable; scripts must inspect status rather than assuming every goal can be reached. There are no off-mesh jumps, platformer paths, flying navigation, large-world streaming or networking guarantees. The strategy reference runs 128 followers around a central wall, with staggered path requests and explicit per-worker gather/return assignments.

## Music effects, mixing and offline audio

The [score format](10-text-assets.md#music-and-short-sound-effects) supplies sine, triangle, pulse, saw and seeded noise instruments with envelopes, panning, vibrato and slides. Add up to four ordered effects to a score:

```json
{"effects":[
  {"type":"lowpass","cutoff_hz":6000},
  {"type":"delay","seconds":0.18,"feedback":0.3,"wet":0.2},
  {"type":"reverb","room_size":0.55,"damping":0.5,"wet":0.15}
],"tail_seconds":2}
```

These are score fields alongside the normal instruments/notes, not a complete score. Lowpass/highpass cutoffs are 20..20000 Hz. Delay time is 0.001..2 seconds, feedback 0..0.95 and wet 0..1. Reverb room size is 0..1, damping 0..0.99 and wet 0..1. Native filters and bounded delay/comb-reverb implementation are deterministic signal processing, not neural music generation. Effects run after synthesis; tails default from the effect chain and may be explicitly bounded to 0..10 seconds. A score including envelopes/effect tails remains at most 600 seconds.

Project-level audio config gives master, music and sfx buses plus optional custom buses:

```json
{"audio":{"buses":{
  "master":{"gain":0.8},
  "music":{"gain":0.6,"effects":[{"type":"reverb","wet":0.1}]},
  "sfx":{"gain":0.8},
  "ui":{"gain":0.7}
},"tail_seconds":2}}
```

There are at most eight buses including the three defaults, at most four effects per bus, and all non-master buses feed master. Gains are 0..2, pan -1..1, mute boolean. Runtime controls ramp over 10 ms:

```luau
engine.play_sound("confirm","ui")
engine.play_music("theme",true,"music")
engine.set_audio_bus("music",0.4,0,false)
engine.stop_music()
```

The shared mixer supports one background score and eight simultaneous SFX, bounded preparation/command queues and measured dropped commands. Avoid emitting repeated play requests every tick. Pause freezes playback clocks. Live and offline use the same synthesis/effects/mixing implementation, and deterministic scheduled event tests compare PCM across render block sizes. This does not mean audio-device timing is synchronized sample-for-sample with visual frames.

`asset render --id theme --output theme.wav` exports one score. `simulate --steps N --input timeline.json --audio-output mixed.wav` renders the game's collected audio events, scene music/configuration and bus changes through the same mixer at 48 kHz stereo, with a bounded tail. Its report includes peak, RMS, clipped frames and dropped commands. Device playback can continue without audio if no device initializes. Keep levels below clipping; saturation is not a mastering workflow. Audio feature revision 1 adds recorded WAV/FLAC music and SFX, an optional sampled-instrument text-authoring tool, adaptive stems, EQ, compression and limiting. Read [the music and sound guide](30-music-and-sound-guide.md) for commands, budgets and limitations. SoundFont/plugin hosting and native singing remain unsupported; imported vocal/rock recordings are supported playback assets, not generated performances.

## Editable gameplay building blocks

The new templates include project-local modules, not fixed engine story rules. Copy with `new`, then customize them with the human's direction:

| Module | Contract demonstrated |
| --- | --- |
| `inventory` | `count`, integer bounded `add`, `equip(slot,item)`, `equipped(slot)`, `unequip(slot)` and idempotent `grant(rewardId,rewards)`. Item definitions provide `limit` and equipment `slot`; removing the final equipped item requires unequipping. |
| `quests` | Named stages, persistent progress and idempotent completion |
| `dialogue` | Registered nodes/choices, flag conditions, next-node branches, persistent flags, once-only item rewards and semantic UI choices; sample UI displays two choices |
| `combat` | Health, nearest role-filtered target from native overlap, damage and tick-based attack cooldown |
| `fsm` | Inspectable state/entered tick and elapsed time; `after(id,ticks)` and one-shot `due(id,consume?)` timers persisted in engine state |
| `cinematics` | Fixed-tick camera shots, interpolated actor position/rotation and animation changes; progress survives save/load; prior camera restores on completion |
| `orders` | `select`/`selected`, configurable `formation(group,columns,spacing)`, `assign` task, continuous `follow` and one-shot `recall`; staggered native navigation and persistent per-worker assignment |

Dialogue choice rewards use an explicit stable `reward_id` and a `rewards` item-count map. Repeating the same reward does not duplicate items; a capacity failure prevents advancing that choice. Cinematic shots define `ticks`, camera `position`/`target` and optional `actors` entries with `id`, `from`/`to`, `from_rotation`/`to_rotation` and `animation`. Persistent state makes these systems observable and resumable. They are deliberately editable small implementations; expand mechanics and presentation iteratively rather than promising a complete commercial RPG toolkit.

## Evidence and current boundaries

Use [Android qualification](20-android-qualification.md), preserved [Windows/Studio evidence](19-release-evidence.md) and [0.3 reference evidence](14-release-evidence.md) for executed scenarios and exact environment qualification. Available measurements include script memory, entity/navigation counts and per-step script/navigation/physics/total timings. `profile` measures the same fixed-step workload as simulation. Rendering/capture measurements depend on GPU, resolution, source and scene; a small reference pass is not a general production performance guarantee.

Current release support is Windows/D3D11 and Android/GLES3 within documented device coverage. The evolving iOS/Metal source port has 2D/3D simulator evidence, nine signed iPhone capture/state checks and positive initial human feedback; its exact coverage and remaining gates are in [iOS qualification](24-ios-qualification.md). The engine has no model provider integration. Remaining major boundaries include one active scene, primitive axis-aligned physics, bounded ground navigation, CPU skinning/simple materials, no networking/streaming and no native vocal generation or arbitrary plugin hosting. Larger creative games should grow through human-directed playable milestones, with source checkpoints, explicit save migrations, technical scenarios and human feedback at each stage.

### Dragging a scrollable list

A container with `data-drag-scroll="true"` and `overflow-y:auto` (or `scroll`) accepts vertical content dragging with a mouse or touch. Use an explicit bounded height and keep exit buttons outside it. Movement beyond 4 dp scrolls; release produces no game click. Nested controls and the native scrollbar retain their own input. Hiding, replacing, or resetting the container cancels its drag. This opt-in behavior does not change existing held game actions.


For the 0.5.0 correction aligning flat-sprite rotation with Box2D polygon contacts, see [2D sprite rotation and contact verification](32-sprite-rotation-correction.md). This rendering correction changes no physics API or saved angles.

Native material boards use contiguous channels, a bounded classic chemistry kernel, active chunks, dynamic sprite surfaces and lazy state providers. See [material grids and bound state](38-material-grid-guide.md). This feature is Windows-qualified; existing mobile qualification does not cover it.
