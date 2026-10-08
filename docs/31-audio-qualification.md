# Audio feature revision 1: qualification and limits

September 21, 2026. Runtime version remains **0.5.0**, with separately discoverable **audio feature revision 1**. Older 0.5.0 players do not imply support for these new assets. The native music format now accepts versions 1 and 2; old synthesized recipes remain valid.

## Executed technical checks

| Area | Evidence |
| --- | --- |
| Windows native/runtime | All 13 CTest suites pass, including legacy music, mixer, recorded audio and lifecycle tests. The full integration/reference-game test script passes. |
| Recorded playback | WAV and FLAC decoding, 44.1-to-48 kHz conversion, mono-to-stereo, malformed sources, source paths, automation, budgets, synchronized loops, beat/bar scheduling across loop boundaries and fades are tested. |
| Audio DSP | EQ frequency response, compression ratio, stereo-linked sample-peak protection, block-size invariance and callback allocation/destruction checks pass. Recorded layer/SFX event PCM matches offline WAV export exactly after PCM16 conversion. Existing synthesis PCM regression tests pass. |
| Authoring | Five dependency-free composition tests pass. Separate CLI integration checks cover binary source revisions/checkpoints, Windows/Android/iOS cooking, authored WAV overwrite protection, importing a recording, native audition rendering and targeted transformations that preserve other parts. |
| Windows graphics | Music Room captures render at ticks 0, 61 and 420. A phone-density host layout check catches no diagnostics after the compact layout correction. |
| Physical Android | Samsung SM-A146U, arm64. Current player builds, installs and runs with a real AAudio device. Eight native commands verify pause/reset, layer changes, recorded chime, save/mutate/load and resume; the restored layer target is correct. Native measurements report zero clipped frames and dropped commands. Fixed-tick device captures complete at 0, 61 and 420. |
| iOS simulator | Apple silicon Mac, macOS 26.6.2, Xcode 27.0, arm64 iPhone 16 simulator with iOS 27.0. Current native player builds, installs and runs with Core Audio ready and non-null output. Eight equivalent native commands pass. Captures complete at 0, 61 and 420; corrected UI shows all controls. |
| Portable Android binaries | Current arm64-v8a and x86_64 development/release players compile. Export rejects old player binaries when cooked content requires audio feature revision 1. Only the arm64 physical device above establishes new runtime-device evidence. |
| Evidence transport | A physical test reproduced Windows ADB newline conversion corrupting PNG pulls despite non-PTY shell mode. Pull now uses device-side base64 plus strict decoding and atomic publication, preserving remote error status. All 32 Android host-tool tests pass, and the downloaded PNG matches the device file hash. |
| Studio | All 74 tool/security/runtime-package tests pass. A real broker invocation discovers the sampler and renders a text-authored sampled chime through the native audition mixer. The new commands remain project-scoped and honor human play locks. |
| Portable runtime | With an empty PATH, all eight packaged starters create, validate, check, simulate and render. Packaged Music Room plays through offline simulation without sampler lookup. The packaged sampler DLL and Python bridge render a text score using an explicitly supplied CC0 pack. All package manifest hashes are checked. |

Local raw evidence is under `.cache/expressive-audio/`: `full-tests.log`, `music-integration.json`, `android-controls.json`, `android-final-captures/`, `ios-evidence/`, `ios-final-captures/`, and the native audition reports. These machine-local build/test files are intentionally excluded from Git. The committed machine-readable record is [audio feature evidence](releases/audio-feature-1-evidence.json).

No new physical iPhone/iPad listening or interaction qualification is claimed by the simulator result. Existing platform qualification records describe earlier runtime revisions. Native output-device status and waveform counters are technical evidence, not proof that a person heard and liked the device output. New authoring-library binaries are qualified on Windows; exported games do not include that library or the source bank.

## Human listening result

**The musical examples did not meet the requested quality target.** The human described the initial three sketches as tacky, asked for a fuller ensemble, and rejected the ten-part revision as still artificial/tacky. There is no artistic approval. Adding parts did not establish convincing orchestral realism. Preserve that feedback when preparing the next musical direction; do not describe these examples as professional, emotional successes, or approved soundtracks.

The fuller rejected revision renders 22.08 seconds, ten synchronized parts, peak 0.7834 and RMS 0.1427 with zero clipped frames. Those numbers establish bounded signal levels, not composition quality. The examples remain editable **technical demonstrations**. The current small palette has limited articulations, one repeated take per key/dynamic, finite recorded sustains and no recorded legato transitions. Sample playback and mix controls provide useful capabilities but do not guarantee convincing performance.

## Distribution and reproducible checks

The audio release uses a new directory, `dist/Langmake-0.5.0-audio1`, preserving earlier packages. Build/package evidence is recorded separately from artistic feedback. The source and portable guides describe the same CLI and format contracts; optional Studio may carry the same verified runtime as a separate distribution.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
powershell -ExecutionPolicy Bypass -File scripts/test.ps1
python scripts/music/test_music.py
python scripts/music/integration_tests.py --engine build/langmake.exe
./build/langmake.exe music status
```

The sampled authoring library is optional; `music setup` builds the pinned sfizz source locally. Package with `-IncludeMusicAuthoring` to include its verified host binaries. The 395 MiB CC0 source pack remains a separate hash-checked download. Runtime-only playback needs neither the pack, Python nor sfizz. A generated playable Music Room can be included with `-MusicExampleDirectory` pointing at a cooked, validated project inside the workspace.

This workspace's tested player inputs and demo can reproduce the new package without replacing the previous release:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -BuildDirectory build/audio -AndroidPlayerDirectory .cache/expressive-audio/android-player -PackageDirectory dist/Langmake-0.5.0-audio1 -IncludeMusicAuthoring -MusicExampleDirectory .cache/expressive-audio/music-room-final-cooked
python scripts/package_smoke.py --package dist/Langmake-0.5.0-audio1 --archive dist/Langmake-0.5.0-audio1-windows.zip
```

The `.cache` inputs are generated local artifacts, not prerequisites shipped through Git. On another machine, build the current runtime/Android players and render/cook the example first, then pass those local paths. A separately packaged Studio distribution uses the same verified runtime. Archive/extraction checks and artifact hashes accompany the final local distribution report; they do not change the listening assessment above.

Read [the music/sound guide](30-music-and-sound-guide.md) for exact commands, limits, import licensing, adaptive-layer semantics and save behavior. Saves retain chosen music/layer targets but restart the playback cursor and DSP state. Resident PCM is bounded to 96 MiB per asset and 192 MiB per compiled project; this release does not add streaming, arbitrary plugins/SoundFonts, generated singing, or realistic electric-guitar/bass instruments to the curated pack.
