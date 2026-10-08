# Text-authored graphics, music and sound effects

Recorded tracks and sampled-instrument compositions are covered in the [music and sound guide](30-music-and-sound-guide.md). This page retains the version-1 synth recipe reference; `asset schema --type music` exposes both native recipe versions.

Langmake 0.3 compiles pixel sprites, procedural solid meshes, animated models and synthesized music. They are project-local JSON recipes. An external coding agent can create or revise them with ordinary file operations; no Blender installation, image service, music service or engine-owned model connection is required. The Windows/D3D11 preview displays the results and miniaudio plays the scores. The [runtime guide](13-runtime-guide.md) covers the new model, UI, gameplay and audio-effect contracts.

Follow the [human-directed development workflow](08-agent-api.md#human-directed-development-and-playtesting) before creating game assets. The human developer directs the visual and musical style and evaluates it in play; agents implement, validate and present iterations. Correct geometry, a clean render or an unclipped WAV is technical evidence, not creative approval. Default to human gameplay playtesting rather than replacing it with a computer-use playthrough.

The Star Patrol examples demonstrate the same idea in 2D and 3D: a rocket, aliens, a star field, a planet, an original electronic theme and a laser sound. Start either with `run-rockets-2d.cmd` or `run-rockets-3d.cmd`. A/D moves and Space fires. These are small reference games, not full game-production tooling.

## Discover, create and inspect

```powershell
.\build\langmake.exe asset schema --type sprite
.\build\langmake.exe asset schema --type mesh
.\build\langmake.exe asset schema --type model
.\build\langmake.exe asset schema --type music
.\build\langmake.exe new --template rockets-2d --output .cache/my-star-game
.\build\langmake.exe asset list --project .cache/my-star-game
.\build\langmake.exe asset inspect --project .cache/my-star-game --id rocket
.\build\langmake.exe validate --project .cache/my-star-game
.\build\langmake.exe preview --project .cache/my-star-game
```

Use a fresh destination for `new`. `new --dimension 2d|3d` still creates the original platform-game starters; `--template rockets-2d|rockets-3d` creates the asset examples. Do not combine the two selectors. Schema output describes field types and limits; compilation also checks references, geometry budgets and cross-field constraints.

Register every recipe in `project.json`. This is a manifest fragment, not a complete project:

```json
{
  "assets": {
    "rocket": {"type": "sprite", "path": "assets/rocket.sprite.json"},
    "theme": {"type": "music", "path": "assets/theme.music.json"},
    "laser": {"type": "music", "path": "assets/laser.sfx.json"}
  },
  "music": "theme"
}
```

An entity's `asset` selects a registered sprite, mesh or model. Its `size` sets rendered sprite/mesh dimensions, `color` tints the asset, `rotation` gives visual XYZ angles in degrees, and `flip_x` mirrors a sprite horizontally. Sprites/models accept `animation` and positive `animation_speed` (default 1). Set tint to `[1,1,1,1]` to retain original asset colors. Schema-2 entities support opacity and independent `visual_scale`; schema-1 projects retain their older opaque mesh-tint rules. A mesh's bounds are centered and fitted independently to all three size axes; preserve its proportions by choosing matching size ratios. Models instead preserve native units and pivots, multiplied by entity size and visual scale.

**Visual assets do not define collision.** The entity's `shape`, `body` and `size` define a primitive box/sphere/capsule collider. Visual rotation and additional visual scale do not change that collider. A detailed rocket or alien therefore still needs an appropriate simple gameplay collision shape. See the [runtime guide](13-runtime-guide.md#entities-prefabs-and-collision) for entity and physics rules.

## Pixel sprites

This complete 8×8 recipe has two hand-authored frames and a looping animation:

```json
{
  "schema_version": 1,
  "width": 8,
  "height": 8,
  "palette": {".": "#00000000", "W": "#EAF8FF", "B": "#399FFF", "F": "#FF8C30"},
  "frames": [
    {"id": "idle", "pixels": ["...WW...", "..WWWW..", "..WBBW..", "..WBBW..", ".WWWWWW.", "WWWWWWWW", "...FF...", "........"]},
    {"id": "thrust", "pixels": ["...WW...", "..WWWW..", "..WBBW..", "..WBBW..", ".WWWWWW.", "WWWWWWWW", "...FF...", "...FF..."]}
  ],
  "animations": {"fly": {"frames": ["idle", "thrust"], "durations": [0.1, 0.1], "loop": true}},
  "default_animation": "fly"
}
```

Each pixel row must have exactly `width` single-byte palette characters, and each frame exactly `height` rows. Palette keys are single printable ASCII characters; values use `#RRGGBB` or `#RRGGBBAA`. Transparent pixels use alpha `00`. Coordinates are pixels, with the origin at the top left. Sprite/mesh recipes have no top-level `type` field; their type comes from the manifest registry.

A frame can also contain `draw`, an ordered array of integer-coordinate operations. Color may be a palette character or hex color:

```json
{"id": "planet", "draw": [
  {"op": "circle", "cx": 16, "cy": 16, "radius": 12, "color": "#2874AD"},
  {"op": "rect", "x": 7, "y": 12, "width": 14, "height": 3, "color": "#69CFA9"},
  {"op": "line", "x1": 2, "y1": 21, "x2": 30, "y2": 11, "thickness": 2, "color": "#E8D798"}
]}
```

Use that frame in a 32×32 recipe. Drawing clips to frame bounds and replaces affected pixels; it does not alpha-composite successive operations. Each frame may have a `source_rect: [x,y,width,height]` when the recipe supplies `source_png: "assets/sheet.png"`. The PNG path is relative to the project root, remains inside it and becomes part of the authored source graph. The crop dimensions must match the recipe's width/height. Layers apply in this order: source crop, pixel rows, draw operations.

Limits include 1–64 frames, dimensions 1–1024, 4,194,304 total frame pixels, 64 drawing operations per frame and a 64-million-pixel draw-work budget. PNG source dimensions are at most 4096×4096; project file-size limits also apply. Animation clips contain 1–256 frame references with matching durations in seconds, each 0.01–60. Without `animations`, all frames form a default looping clip at 0.1 seconds per frame. With explicit clips, specify `default_animation` to avoid depending on sorted-name selection.

```luau
engine.play_animation("rocket", "fly")       -- changing clip starts it
engine.play_animation("rocket", "fly", true) -- explicitly restart the clip
engine.set_rotation("rocket", 0, 0, -12)      -- visual tilt in degrees
```

Animation time advances with simulation time and pauses with the world. Sprite exports are horizontal contact sheets, one frame after another, with nearest-neighbor integer scaling. They are PNG images, not animated GIFs.

## Procedural meshes

Named parts provide opaque colors and solid geometry. This complete recipe combines a cylinder and a cone into a rocket silhouette:

```json
{
  "schema_version": 1,
  "parts": [
    {"id": "hull", "color": "#DDEAF4", "geometry": {
      "op": "cylinder", "radius": 0.5, "height": 2, "segments": 24
    }},
    {"id": "nose", "color": "#FF593D", "geometry": {
      "op": "cone", "radius": 0.5, "height": 1, "segments": 24, "translate": [0, 1.5, 0]
    }}
  ]
}
```

Primitives are centered, Y is up, lengths use meters and cones point toward +Y. `box` requires `size: [x,y,z]`; `sphere` requires `radius`; `cylinder` and `cone` require radius and height. Round primitives accept `segments` from 4 to 64. Nodes can apply `scale`, `rotate` in XYZ degrees and `translate`, in that order. Scale is positive; mirroring by negative scale is not supported.

Boolean nodes have `op: "union"|"difference"|"intersection"` and `children`, an array of 2–32 geometry nodes. For a difference, subtract every later child from the first. These operations use Manifold solid geometry, so a hole is real geometry rather than a painted visual. For example:

```json
{"op": "difference", "children": [
  {"op": "box", "size": [3, 1, 3]},
  {"op": "cylinder", "radius": 0.6, "height": 2, "segments": 24}
]}
```

Limits include 64 parts, 256 geometry nodes, 16 nesting levels and 100,000 expanded output vertices. Empty or degenerate parts and geometry over its work budget are rejected. Each part has a single color and the renderer supplies simple directional lighting. `asset export` writes OBJ with normals, named part groups and the common vertex-RGB extension; some third-party OBJ readers ignore that color extension. There is no material/texture export bundle.

## Music and short sound effects

Music is composed from notes and built-in instruments. Short effects use exactly the same `type: "music"` recipe; `.sfx.json` is a naming convention. This complete half-second score plays two notes:

```json
{
  "schema_version": 1,
  "type": "music",
  "title": "Two-note signal",
  "bpm": 120,
  "ppq": 96,
  "length": 96,
  "gain": 0.7,
  "seed": 17,
  "polyphony": 8,
  "instruments": {
    "lead": {"wave": "pulse", "gain": 0.2, "pan": 0,
      "attack": 0.005, "decay": 0.03, "sustain": 0.5, "release": 0.04,
      "pulse_width": 0.5}
  },
  "notes": [
    {"tick": 0, "duration": 36, "note": 72, "instrument": "lead"},
    {"tick": 48, "duration": 36, "note": 79, "instrument": "lead", "velocity": 0.8}
  ]
}
```

`ppq` is ticks per quarter note; a tick lasts `60 / (bpm × ppq)` seconds. `length` is the loop length in ticks. Note `tick` and `duration` are integers, pitch uses MIDI note numbers 0–127 (60 is middle C), and omitted velocity is 1. Notes may overlap to form chords/tracks. Gaps are rests. Endpoints must remain within score length. One-shot playback retains the final release tail if it extends beyond the loop boundary.

The five voices are `sine`, `triangle`, `pulse`, `saw` and seeded `noise`. Each instrument supports gain 0–1, pan −1 to +1, attack/decay/release 0–5 seconds, sustain 0–1, pulse width 0.05–0.95, vibrato frequency 0–20 Hz and vibrato depth 0–1200 cents. Optional note `slide_to` gives a target MIDI pitch reached across that note's held duration. A sine pitch drop makes a kick; short noise envelopes make hats and snares. Seeded noise allows reproducible renders without sampled instruments or SoundFonts. Scores now accept ordered lowpass/highpass filters, delay and reverb; see [effects and bus mixing](13-runtime-guide.md#music-effects-mixing-and-offline-audio).

For longer compositions, put reusable phrases in `patterns` and place them with `arrangement`. This fragment adds a repeating bass alongside other notes or patterns:

```json
{
  "patterns": {
    "bassline": {"length": 384, "notes": [
      {"tick": 0, "duration": 72, "note": 36, "instrument": "bass"},
      {"tick": 192, "duration": 72, "note": 43, "instrument": "bass"}
    ]}
  },
  "arrangement": [
    {"pattern": "bassline", "tick": 0, "repeat": 2, "transpose": 0}
  ]
}
```

Define the `bass` instrument and set score length to at least 768 ticks for that fragment. Parallel arrangement entries form tracks; there is no separate required track object. Transposition applies to pitch and slide destination. Pattern repetition must fit completely in the score, even when a pattern ends with silence. Scores allow 1–64 instruments, up to 256 patterns, 1024 arrangement entries, 128 repeats per entry, 65,536 authored/expanded notes, a 1 MiB recipe and 600 seconds including release tails. Tempo is 20–400 BPM, PPQ is 1–960 and length is 1–1,000,000 ticks. Polyphony is 1–64; the oldest active voice is replaced when full.

```luau
engine.play_sound("laser")       -- one-shot score
engine.play_music("theme")       -- replace background score and loop
engine.play_music("theme", false) -- play background score once
engine.stop_music()
```

Top-level manifest `music` starts background playback automatically. P/control pause freezes audio sample clocks; resume continues. Music rendering uses the audio sample clock, independent of visual frame rate. Offline renders are reproducible across different render-block sizes on the same build. Headless simulation records audio events without an audio device; adding `--audio-output MIX.wav` renders those events, music, scene configurations and bus changes through the shared game mixer.

Live playback has one background score, eight simultaneous SFX scores and bounded command/voice preparation queues. Excess playback requests are dropped instead of blocking or terminating gameplay; the live audio dropped-command counter exposes this overload. Saturation can also drop music changes or stop requests, so avoid flooding playback calls. Separately, scripts may emit at most 32 audio events per update; exceeding that script contract produces a diagnostic. Live output clamps the mixed signal. Keep gain low enough for chords and overlapping effects; a score's offline report provides peak and clipping measurements.

The included theme is an original chiptune with bass, drums, power-chord-like pulse voices and a lead riff. It demonstrates computer-composed electronic music. Ordered filters, delay, reverb and bus mixing can shape synthesized sounds. Recorded performances and vocals can be imported as WAV/FLAC; the optional curated sample pack supports original acoustic compositions. Its current instrument list does not include realistic sampled guitars. Vocal generation, tracker import and general DAW/plugin hosting remain unsupported; the effect chains are bounded. See the music guide for the exact palette and limits.

## Render, export and replace

```powershell
.\build\langmake.exe asset build --project examples/rockets-2d
.\build\langmake.exe asset render --project examples/rockets-2d --id rocket --output .cache/rocket.png --scale 4
.\build\langmake.exe asset render --project examples/rockets-3d --id rocket --output .cache/rocket-3d.png
.\build\langmake.exe asset export --project examples/rockets-3d --id rocket --output .cache/rocket.obj
.\build\langmake.exe asset render --project examples/rockets-2d --id theme --output .cache/theme.wav
.\build\langmake.exe asset render --project examples/rockets-2d --id laser --output .cache/laser.wav
```

`asset build` exports registered assets to `.langmake/assets/<id>/<asset-revision>/`; use `--id` to select one. It emits PNG sprite sheets, OBJ meshes or WAV music. `asset render` produces a PNG mesh view using the graphical preview; sprite sheets and WAVs are generated without opening a window. `asset export` emits the native exchange form and does not open a mesh preview. PNG scale is an integer 1–8. WAV is 48,000 Hz, stereo, signed 16-bit PCM; its report includes sample frames, exact duration, unclipped peak, RMS and clipped-frame count. Optional integer `--seconds 1..120` trims or pads a one-shot render; omitted/zero uses the entire score, including tails. It does not repeat a score to fill the requested length.

Commands return JSON and write an adjacent `.json` metadata sidecar. For mesh preview PNGs, the sidecar also preserves the camera and rendered state. Check command success before consuming outputs: an existing file alone is not evidence of a successful new export. Authored source files are protected from being used as export destinations.

For a revision-checked replacement, prepare a complete recipe, then inspect the current project revision:

```powershell
$lmSource = .\build\langmake.exe inspect --project .cache/my-star-game | ConvertFrom-Json
.\build\langmake.exe asset replace --project .cache/my-star-game --id rocket `
    --file .cache/replacement.sprite.json --expected-revision $lmSource.revision
.\build\langmake.exe validate --project .cache/my-star-game
```

Replacement compiles the candidate before writing and saves a backup. A stale project revision rejects the write. The revision covers the complete registered source graph, including external model buffers/images, modules, scenes and UI. `checkpoint`/`restore` save and recover that authored graph; `new` copies it without generated caches. A successful hot reload recompiles assets and restarts gameplay. Invalid edits leave the last good preview running with a diagnostic. There is no in-place preservation of script state or music playback position across reload.

## Animated model assets

Register a `model` JSON recipe to author joints, attached solid parts, translation/rotation/scale clips and animation markers. Models use ozz-animation for sampling/blending and an engine-authored shader pipeline with CPU skinning. Native rigid-part characters require no external modeling tool. The new adventure and strategy references contain original animated robot recipes.

A recipe with `source:"models/character.gltf"` or `.glb` imports a bounded subset: triangle meshes, hierarchy, four-weight skinning, LINEAR TRS animation, UV0/base-color textures and simple materials. External local buffers/images participate in revisions and source copying. Unsupported features fail explicitly. See [native models and imports](13-runtime-guide.md#native-animated-models-and-bounded-gltf-imports) for complete examples, supported fields, markers/attachments, limits and exclusions.

`asset render` produces a PNG of the default sampled pose; `asset export` produces posed OBJ geometry. These outputs do not constitute an animated rig export. Inspect each model's source units/pivots before assigning entity size; model scaling differs from center-fitted solid meshes.

## Current boundary

This release supports direct text control of pixel art, sprite animation, colored solids, joint/clip models, supported imported skins and synthesized scores with effects/mixing. It does not implement organic sculpting, automatic rigging/retargeting, arbitrary import formats, image-to-3D generation, full PBR materials, sampled instruments or vocals. New platforms remain separate qualification work: graphics is Windows/D3D11, with other desktop, mobile, VR and console targets still on the roadmap.
