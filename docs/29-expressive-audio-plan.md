# Expressive music and sound implementation

Authorized September 21, 2026. This is the implementation contract; completed capabilities and platform evidence will be recorded separately.

## Architecture and decisions

Keep the existing version-1 synthesizer and its sound unchanged. Add two cooperating formats: an editable, symbolic composition for authoring, and a version-2 native music recipe containing synchronized recorded tracks. Both use original compositions and explicit licensed instrument sources. The authoring tool renders individual instrument parts through the pinned sfizz C API; the player mixes the resulting audio without sfizz, Python, model accounts or a music application. The same native track mixer/effects render previews, offline game audio and live playback.

This deliberately keeps the large sampler and source sample banks out of mobile games. Authoring remains text-first through `langmake music` commands. Source compositions retain notes, patterns, sections, articulations, tempo maps and expression; baked stems retain exact frame alignment and provenance. Audio imports also support externally authored tracks and sound effects. Neural generation, sung lyrics, arbitrary plugin hosting and instrument features absent from the selected samples are outside this implementation.

sfizz 1.2.3 is an evaluated dependency, not an assumed integration success. Pin its source and submodules, build library-only without JACK/plugins/system audio packages, and preserve its notices. Its archived upstream status is a maintenance constraint; the adapter and baked format isolate that dependency. Use a curated CC0 VSCO CE instrument pack with explicit download manifests, hashes, provenance and bounded project-local installation. No commercial sample pack, download account or globally installed application is required.

## Delivery phases

1. **Audible proof and pack contracts.** Compile the sampler adapter, install a small licensed palette and render three original sketches: intimate piano/strings, orchestral adventure and rhythmic ensemble. Provide human audition without treating technical measurements as musical approval. Continue independent implementation while feedback is pending.
2. **Composition authoring.** Add validated text scores with named instrument tracks, sections, reusable/transposed patterns, tempo changes, dynamics/expression, articulation selection and bounded deterministic humanization. Expose schema, instrument discovery/audition, validation, section/track renders, stem export and targeted score transformations. Unsupported ranges/articulations must diagnose explicitly.
3. **Native audio assets.** Add version-2 recorded-track music/SFX, WAV/FLAC import via existing miniaudio decoding, source-graph revision/copy/cook/checkpoint inclusion, explicit memory/frame budgets and native schema/discovery. Prepare PCM and effects outside callbacks; old synthesis remains compatible. Initial native tracks are bounded resident PCM; report memory rather than promising unlimited streaming.
4. **Mixing and adaptive music.** Add track gain/pan/automation and EQ/compression/peak protection to existing effects. Implement synchronized layers and beat/bar-quantized gain transitions with fades, shared playback clocks, inspectable state and native game-event/offline parity. Samples and audio callbacks never perform network work or model inference.
5. **Examples and agent workflow.** Ship contrasting original compositions, a recorded SFX example and an interactive layer demonstration. Teach agents to propose distinct short sketches, get human direction, develop motifs and contrast, audition isolated parts, revise selected sections and render the final soundtrack. Document real limits, dependency setup, budgets, source licenses and commands.
6. **Qualification and distribution.** Check legacy PCM regression, timing/block-size invariance, transitions, clipping, invalid assets, source revision/copy/cook, bounded queues/memory and save/lifecycle expectations. Qualify available platforms with actual builds/device evidence, keeping historical results separate. Update discovery, canonical guides, prompts and generated package copies together; preserve previous releases and unrelated working-tree changes.

## Acceptance

An external agent using packaged instructions can create an original multi-instrument piece from text, render contrasting auditions and stems, import the result into a game, adjust synchronized layers, and use richer recorded sound effects without editing engine code or opening a DAW. Human listening decides emotional impact and artistic acceptance. A passing waveform or schema test does not establish musical quality.
