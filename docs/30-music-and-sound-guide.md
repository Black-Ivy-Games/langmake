# Music and sound: agent authoring guide

Read this before composing a soundtrack, importing recordings, or creating sound effects. Check `langmake music status`, `langmake music instruments`, `langmake music schema`, `langmake asset schema --type music`, and `langmake script-api` against the installed build. The [implementation plan](29-expressive-audio-plan.md) is separate from [executed qualification](31-audio-qualification.md).

## What agents can create

There are three complementary routes. All work with external directory/CLI agents; neither Studio nor a particular model is required.

| Route | Use it for | What the game needs |
| --- | --- | --- |
| Existing music recipe version 1 | Chiptunes, electronic instruments, procedural tones/noise, laser/explosion/UI effects | JSON recipe; existing synthesis remains compatible |
| Text composition + recorded instrument pack | Original acoustic/orchestral music, expressive instrumental cues, tuned/percussive sound effects | Baked version-2 music recipe and WAV stems |
| Imported WAV/FLAC | Human recordings, licensed music, externally generated music, voices, ambient beds and effects | Version-2 recipe and registered audio files |

The supplied CC0 palette contains **15 instruments**: piano, violin section sustain, violin section spiccato, cello section, horn, trumpet, flute, clarinet, harp, marimba, glockenspiel, bass drum, snare, claves and sleighbells. These use real recordings from VSCO 2 CE, including multiple recorded dynamics where available. Instrument discovery lists actual pitch ranges and articulations. `C4` means MIDI 60 throughout Langmake compositions; the pack adapter handles upstream naming differences.

This is an instrument renderer and game mixer. An agent still composes the music. It does not generate a finished song from an engine-owned neural model. The current curated pack has no electric guitar, sampled bass guitar, vocals or true legato transitions. A convincing rock performance can be imported as WAV/FLAC, but do not promise that this pack produces a realistic rock band or singing from notes. Native synthesis remains useful alongside recordings. General SFZ/SoundFont import, arbitrary VST/AU plugins, lyrics-to-vocals and streaming audio are not implemented.

## Human-directed musical workflow

1. Read the human's creative brief and decide what the music should do: mood, instrumentation, pace, intensity, places for silence, approximate cue length and gameplay transitions. Offer useful choices without making the human specify every note.
2. Propose two or three **contrasting short sketches**, rather than repeatedly changing the oscillator or instrument on one loop. Change the motif, rhythm, harmony, register, density and instrumentation. `examples/music/` contains editable structural examples, not required styles.
3. Render audio and hand it to the human. Ask focused questions about the musical direction. Technical validation establishes file/timing correctness; only listening feedback establishes artistic acceptance.
4. Develop the chosen sketch with a recognizable motif, an answering phrase, contrast and a deliberate return or ending. Use register and spacing to separate parts; leave headroom for sound effects/dialogue. Avoid filling every beat with every instrument.
5. Use velocity for individual attacks, expression for held phrases, and real articulation mappings. Render isolated tracks or a named section to revise a weak passage. A small amount of seeded timing/velocity variation is optional; random timing is not a substitute for phrasing.
6. Bake final stems, validate the game, export an offline mix, and test loop boundaries and gameplay transitions. Give the human a playable iteration with exact controls and known limits. Do not silently replace their chosen music when revising unrelated gameplay.

## Setup and first render

Examples below use the source Windows executable. In a portable package use `./langmake.exe`. Direct Python commands work on hosts with a matching native engine via `python scripts/music/music.py --engine PATH COMMAND`. Python 3.9+ is required for authoring; games need neither Python nor sfizz. Keep packs and outputs within your authorized workspace.

```powershell
./build/langmake.exe music status
./build/langmake.exe music instruments
# Source builds: Git, CMake and a C++ compiler must already be available.
# This builds pinned sfizz 1.2.3 locally, without installing global tools.
./build/langmake.exe music setup --jobs 4
# Optional download: about 395 MiB, hash-verified CC0 instrument recordings.
# One local pack can be reused across games. No account is required.
./build/langmake.exe music pack-install --directory .cache/music-packs/vsco-ce
./build/langmake.exe music pack-verify --pack .cache/music-packs/vsco-ce

./build/langmake.exe music validate --score examples/music/lanterns.composition.json
./build/langmake.exe music render --score examples/music/lanterns.composition.json --pack .cache/music-packs/vsco-ce --project games/my-game --output-dir games/my-game/audio/lanterns-v1
```

Use a **new output directory per render**. The command preserves existing output and fails if that directory is nonempty. Outputs include separate instrument WAVs, `music.json`, an editable `composition.json`, source/pack hashes, instrument license, `render-report.json`, `audition.wav` and a native audition report. `audition.wav` is rendered by the actual Langmake mixer, including recipe effects. Listen to it. Reports include sample frames, peak, RMS and clipping; RMS is not a perceptual loudness or artistic score.

Register the generated recipe in the game manifest:

```json
"assets": {
  "lanterns": {"type":"music", "path":"audio/lanterns-v1/music.json"}
},
"music": "lanterns"
```

Every `source` inside a recorded recipe is relative to the **project root**, not the recipe folder. Moving the audio folder requires updating those references. Runtime WAV/FLAC files enter the source graph: changing them changes revisions, and cooking, cloning and checkpoints include them. Composition JSON and authoring sample packs are editable source materials but are not automatically dependencies of the baked recipe. Keep them in the game's own repository; optional provenance hashes document which render produced the asset. Exported games need only the baked recipe/audio and appropriate notices.

### Using these tools in Studio

Studio agents discover this guide through the `documentation` tool and invoke the same commands through `engine` with `command: "music"`, a `subcommand`, and `options`. For example, render with `subcommand: "render"` and `options: {"score":"music/score.json","pack":"music-packs/vsco-ce","output-dir":"audio/review-1"}`. Studio supplies the selected project automatically. All paths must stay inside that project; install its pack with `subcommand: "pack-install"` and `options: {"directory":"music-packs/vsco-ce"}`. Writes honor the active human play-session lock.

Sampler installation (`music setup`) is a separate host setup step, or use a distribution that includes the optional sampler binaries. Studio does not expose compiler installation to a project agent. Status, instrument/schema discovery, validation, pack verification, rendering, auditions, targeted transforms and recorded imports are available through its bounded command tool. Have the agent provide the generated audition file to the human; Studio does not add a piano roll or audio editor. External directory/CLI agents can continue using the commands directly and may reuse a shared pack inside their authorized workspace.

## Text composition contract

```json
{
  "schema_version":1, "type":"composition", "title":"A small discovery",
  "length_beats":8, "beats_per_bar":4,
  "tempo":[{"beat":0,"bpm":92},{"beat":4,"bpm":86}],
  "sections":{"question":{"start":0,"end":4},"answer":{"start":4,"end":8}},
  "seed":7, "humanize":{"timing_ms":3,"velocity":0.02},
  "loop":true, "tail_seconds":2, "gain":0.7,
  "patterns":{"idea":{"length_beats":4,"notes":[
    {"beat":0,"duration":0.7,"pitch":"C4","velocity":0.55},
    {"beat":1,"duration":0.7,"pitch":"E4","velocity":0.65},
    {"beat":2,"duration":1.5,"pitch":"G4","velocity":0.75}
  ]}},
  "tracks":[{"id":"piano","instrument":"piano","gain":1,"pan":-0.1,
    "arrangement":[{"pattern":"idea","beat":0},{"pattern":"idea","beat":4,"transpose":5}],
    "expression":[{"beat":0,"value":0.8},{"beat":6,"value":1},{"beat":8,"value":0.6}]
  }],
  "effects":[{"type":"reverb","room_size":0.5,"wet":0.18},{"type":"limiter","ceiling_db":-1}]
}
```

Beats may be fractional. Notes require `beat`, `duration` and `pitch` (MIDI integer or scientific note name); `velocity` defaults to 0.7. A track chooses an instrument ID, optional direct notes and/or pattern placements. Placements support `repeat`, integer `transpose`, and a velocity multiplier. Pattern changes affect every placement; use a targeted transform when you intend to revise one track/section.

`tempo` is an increasing list beginning at beat zero; tempo is constant between entries. `expression` is a per-track 0..1 curve, linearly interpolated at 10 ms intervals into smoothed MIDI CC11. It changes held phrases as well as new notes. Articulation names must be explicitly mapped, for example `"articulations":{"short":"violins-spiccato"}` on a violin track and `"articulation":"short"` on a note. Unknown articulations and out-of-range pitches fail with diagnostics. The curated sustained recordings have finite lengths and no fabricated sustain loops; avoid assuming indefinitely held notes or true legato/round-robin behavior.

```powershell
# Hear an instrument at several pitches/dynamics.
./build/langmake.exe music audition --instrument horn --pack .cache/music-packs/vsco-ce --project games/my-game --output-dir games/my-game/audio/horn-audition
# Render an isolated part or crop a named section. Omit selectors for full music.
./build/langmake.exe music render --score games/my-game/music/score.json --pack .cache/music-packs/vsco-ce --project games/my-game --output-dir games/my-game/audio/review-2 --track piano --section answer
# Preserve the original; expand only the chosen track, then edit the named section.
./build/langmake.exe music transform --score games/my-game/music/score.json --output games/my-game/music/revision-2.json --track piano --section answer --transpose 2 --velocity 0.9
```

Render normalization applies one common gain to all stems, preserving the composition's relative dynamics while using PCM headroom. Because peak normalization depends on the selected material, an isolated/section audition may receive different export gain; compare `baked_scale` in reports. Runtime track gain and pan remain editable. Loop renders fold the exported release tail into the start of the next cycle; allow enough `tail_seconds` for decaying instruments, and audition several repeats. A section render is a crop for review, not a guaranteed seamless new loop. Non-looping cues retain their release tail.

## Recorded music, ambience and sound effects

Both sustained music and short effects use asset type `music`. A version-2 recipe supplies synchronized tracks:

```json
{
  "schema_version":2,"type":"music","title":"Encounter",
  "length_frames":384000,"bpm":120,"beats_per_bar":4,"gain":0.7,
  "tracks":[
    {"id":"strings","source":"audio/strings.wav","gain":1,"pan":-0.15},
    {"id":"drums","source":"audio/drums.flac","gain":0.8,"pan":0,
     "automation":[{"frame":0,"gain":0.5},{"frame":192000,"gain":1}]}
  ],
  "effects":[{"type":"reverb","wet":0.15},{"type":"limiter","ceiling_db":-1}]
}
```

All timing uses decoded **48,000 Hz stereo output frames**. WAV/FLAC inputs can have other supported sample rates/channel counts and are converted during asset compilation. `length_frames` sets the exact score loop boundary; shorter tracks pad with silence and longer sources are cut at that boundary. `offset_frames` delays a track's start. A generated `beat_frames` array preserves the composition's tempo map for adaptive transitions; manually authored assets can omit it to use fixed `bpm`. Each track also supports up to four effects.

Track automation uses increasing `frame` points starting at zero. Gain is multiplied by track gain and runtime layer gain; automated pan is absolute. Values interpolate linearly, then hold the last point. Use sensible endpoints to avoid a discontinuity when the loop wraps. Import is explicit about duration and license:

```powershell
./build/langmake.exe music import --source games/my-game/source-audio/impact.wav --project games/my-game --output-dir games/my-game/audio/impact-v1 --frames 72000 --license "Original recording by the game developer"
```

The command copies audio, creates its recipe and renders an audition. It does not obtain rights to external audio. Record the actual source/license in your game repository. Imported speech, performances or generated tracks are playback assets; import does not imply native voice synthesis or transcription. Sound effects can also be composed using the sampled instruments, or use the existing version-1 noise/envelope/pitch-slide synthesis. `examples/music/chime.composition.json` demonstrates an original recorded-instrument effect.

## Playback, adaptive layers and mixing

```lua
engine.play_music("encounter", true)
engine.set_music_layer("drums", 0, 0, "immediate")
-- At an encounter: retain stem alignment, enter on the next bar, fade over 0.4 s.
engine.set_music_layer("drums", 1, 0.4, "bar")
engine.play_sound("impact")
engine.set_audio_bus("music", 0.6, 0, false)
```

`set_music_layer(id, gain, fade_seconds?, quantization?)` changes a track in the **current version-2 background score**. Gain is 0..2; fade is 0..10 seconds, default 0.1. Quantization is `immediate` (default), `beat` or `bar`. Muted tracks keep the shared clock. The next eligible boundary includes the current sample if it is already on that boundary; otherwise it waits. At most one pending target per layer is retained, with later requests replacing it. A request replaces an in-progress fade from its current gain. Unknown layers, unsupported synthesis-only assets or invalid values diagnose rather than silently succeeding.

Snapshots expose `music_layers` as **requested targets**, including request tick and transition settings. These are not a claim that an audio-device transition has already completed. Audio measurements expose sample-frame progress, peak, RMS, clipping and dropped commands; tick-stamped `layer` events also feed offline `simulate --audio-output`. Audio clocks freeze during host pause. Saves preserve the chosen music and layer targets; restoring/reloading starts the music from its beginning and applies saved target gains immediately. Exact playback cursor, pending fade phase and DSP delay histories are not saved.

One background score can have up to 16 synchronized stems; eight SFX voices may also play. Changing the whole background score with `play_music` replaces it immediately; cross-cue beat-matched transitions are not implemented. Use layers inside one cue for seamless intensity changes.

Effects run in order on tracks, then the score, then its game bus and master bus. Existing lowpass/highpass, delay and reverb remain supported. Added effects are:

| Effect | Fields and bounds | Behavior |
| --- | --- | --- |
| `eq` | `frequency_hz` 20..20000, `gain_db` -18..18, `q` 0.1..10 | One parametric bell band per effect slot |
| `compressor` | `threshold_db` -60..0, `ratio` 1..20, `attack_ms` 0.1..200, `release_ms` 1..2000, `makeup_db` 0..18 | Stereo-linked envelope compressor; attack can let transients through |
| `limiter` | `ceiling_db` -20..0, `release_ms` 1..2000 | Stereo-linked instantaneous sample-peak ceiling with release smoothing |

The limiter is not an oversampled true-peak mastering limiter. Do not hide a poor balance behind heavy limiting. Start with dry instrument balance, share reverb at the score/bus, and reserve room for sound effects. All new effects are opt-in; unchanged synthesized recipes retain their old dry sound.

## Limits and verification

| Area | Initial bound |
| --- | --- |
| Composition | 16 tracks, 65,536 expanded notes, 256 patterns, 1,024 placements per track |
| Sampled note density | 128 simultaneous voices per instrument in a part, including release envelopes; denser scores diagnose before rendering |
| Timing | 20..400 BPM; 1..16 beats per bar; 256 tempo points; 180 seconds including render tail |
| Expression and variation | 4,096 expression points/track; 0..30 ms timing variation; 0..0.15 velocity variation |
| Native recorded asset | 16 tracks, 96 MiB decoded PCM16; 192 MiB across the compiled project |
| Files/source graph | 32 MiB per binary input; existing total source/scene budgets still apply |
| Native duration/automation | Up to 600 seconds including tails, subject to PCM/source budgets; 4,096 automation points/track; 20,000 beat offsets |
| Audio routing | Eight buses including master/music/sfx; four effects per chain; delay up to two seconds; export tails at most ten seconds |

Resident PCM is prepared outside the callback. Nothing downloads, decodes files, allocates voices or calls an LLM from the audio thread. A 30-second stereo stem uses about 5.5 MiB of decoded PCM; six such stems use about 33 MiB **before** project source copies, effects and other assets. Inspect `decoded_bytes`, keep mobile cues/stem counts modest, and test on the actual device. Exported players do not include the 395 MiB authoring bank.

```powershell
./build/langmake.exe validate --project games/my-game
./build/langmake.exe check --project games/my-game
./build/langmake.exe asset inspect --project games/my-game --id encounter
./build/langmake.exe asset render --project games/my-game --id encounter --output games/my-game/.langmake/encounter-audition.wav
./build/langmake.exe simulate --project games/my-game --steps 600 --input games/my-game/tests/music.input.json --audio-output games/my-game/.langmake/encounter-playback.wav
```

Check idle/encounter transitions, repeated triggers, pause/resume, a save/load, the end-to-start seam, SFX over music, and the complete exported build. Keep native failures, hardware listening and artistic feedback separate. Do not claim the current audio revision is qualified on a mobile device based on old platform evidence.

## Playable reference

```powershell
./build/langmake.exe music example --pack .cache/music-packs/vsco-ce --project .cache/music-room
./build/langmake.exe play --project .cache/music-room
```

The generated Music Room uses all three original sketches and a sampled chime. Keys **1/2/3** choose cues, **D** toggles orchestral percussion on a bar, **Space** plays the chime, **F6/F9** save/load, and **Escape** exits on desktop. Buttons expose the same actions. The reference proves integration; it is not a finished game or proof of musical acceptance.

## Current listening assessment

The initial three sketches and a fuller ten-part ensemble were rejected by the human on September 21, 2026 as artificial/tacky. They are technical examples, not approved soundtracks or evidence of professional orchestral realism. The initial palette uses finite recorded samples, a small set of articulations and one repeated take per key/dynamic. Expression changes amplitude; it does not create recorded legato transitions or new dynamic timbres. More layers, random timing or heavier reverb cannot guarantee convincing performance. Diagnose the exposed part and arrangement, audition alternatives, and use the WAV/FLAC import path when an appropriately licensed performance or another palette is needed. Do not present a similar loop as a fresh musical direction merely because it uses more instruments.
