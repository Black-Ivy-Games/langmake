# Original text-authored music examples

Start with [the music and sound guide](../../docs/30-music-and-sound-guide.md). These are editable composition sources; baked WAVs are generated locally and are not committed as engine build output.

| Composition | Direction | Parts |
| --- | --- | --- |
| `lanterns.composition.json` | Quiet, reflective phrase with a slower answer | Piano, cello, flute |
| `storm-gate.composition.json` | Rhythmic orchestral adventure | Short strings, horn, trumpet, cello, bass drum |
| `clockwork-market.composition.json` | Playful, syncopated ensemble | Marimba, clarinet, harp, snare, claves |
| `ensemble.composition.json` | Ten-part sustained orchestral revision | String sections, horns, woodwinds, harp and percussion |
| `chime.composition.json` | Short discovery/reward effect | Glockenspiel |

The notes and arrangements are original Langmake examples. Recorded instruments come from the separately installed CC0 VSCO CE curated pack. Pack manifests pin individual hashes, source commit and license. Neither a sample bank nor a model connection is part of the exported player.

```powershell
./build/langmake.exe music setup
./build/langmake.exe music pack-install --directory .cache/music-packs/vsco-ce
./build/langmake.exe music example --pack .cache/music-packs/vsco-ce --project .cache/music-room
./build/langmake.exe play --project .cache/music-room
```

`showcase/` contains the source game/UI/scenario used by `music example`; its generated audio dependencies are intentionally absent until that command renders them. Existing examples and games do not need the pack. Output directories must be new. See the guide for partial renders, instrument auditions and targeted transforms.

Use these structures as inspiration, not as a default soundtrack for every game. Human listening decides whether a piece serves the project.

Listening feedback, September 21, 2026: the human rejected the first three sketches as tacky and also rejected the fuller ten-part revision as artificial. These remain technical format examples, **not an approved musical direction or a quality benchmark**. Adding tracks alone did not resolve the issue. Build future auditions around phrasing, articulation, natural register, harmonic movement and intentional balance; evaluate a different sound palette or imported licensed performances when the included samples do not meet the brief.
