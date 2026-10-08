# Optional text music authoring

Canonical agent instructions: [music and sound guide](../../docs/30-music-and-sound-guide.md). The executable forwards `langmake music ...` here, or call `python scripts/music/music.py --engine PATH COMMAND` directly.

- `composition.py`: validated symbolic notes/patterns/sections/tempo/expression.
- `sampler.py`: isolated sfizz C API rendering into synchronized stems; the native engine performs final auditions.
- `tooling.py`: pinned, workspace-local sfizz build and hash-checked instrument installation.
- `packs/vsco-ce.json`: curated CC0 mappings, official source commit, byte counts and SHA-256 hashes. The recordings are downloaded separately.
- `test_music.py`: dependency-free authoring validation tests. Native tests cover decoding, mixing, clock behavior and effects.

No module here is linked into the game player. A built Windows sampler is optional in portable authoring distributions under `music-tools/bin`; source setup uses `.deps/music/bin`. A trusted custom build can be selected with `LANGMAKE_SFIZZ`. Changing that library changes rendering provenance and requires qualification. sfizz 1.2.3 is pinned and upstream is archived; keep this adapter isolated and do not silently upgrade it.
