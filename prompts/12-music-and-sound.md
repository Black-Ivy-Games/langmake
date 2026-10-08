# Music and sound prompt structures

These examples are adaptable briefs. They do not authorize work independently of the human's request. Read [the music and sound guide](../docs/30-music-and-sound-guide.md), inspect the installed palette/schema, and preserve the [shared human-directed workflow](README.md).

## Original soundtrack through audible iterations

> Create music for **[game/scene]** that feels **[emotional direction]**. The player should feel **[intended response]** during **[specific situation]**. I like **[musical characteristics: broad instrumentation, pacing, dynamics]**, but make an original composition.
>
> First give me **three contrasting 15–30 second sketches**. Vary the motif, harmony, rhythm and instrumentation, rather than changing the sound of one repeated loop. Use Langmake's supported recorded instruments when they fit, with text-authored composition files I can keep editing. Show what the installed palette actually supports; do not promise unsupported guitars, vocals or articulations.
>
> Render each sketch so I can listen. Tell me what each is aiming for and ask which direction to develop. While awaiting my creative choice, finish independent technical checks and preparation. Once I choose, develop **[approximate duration/structure]**, with **[desired contrast or progression]**, a deliberate ending or checked loop seam, and space for **[dialogue/effects]**.
>
> Keep composition sources, instrument provenance/licenses, final synchronized stems and useful audition reports in this game's directory. Verify clipping, sample alignment, memory and the exported target build. Passing those checks does not substitute for my listening feedback.

## Adaptive cue

> For **[existing game]**, make a cue with a calm foundation plus **[percussion/brass/another supported layer]** that enters when **[event]** happens. The layers should retain alignment and fade on the next **[beat/bar]**. On **[event ending]**, return to the foundation without restarting the cue.
>
> Use version-2 music tracks and `engine.set_music_layer`; preserve current controls, progression and unrelated content. Record a native input scenario that triggers both directions, export its game-audio WAV and inspect requested layer state. Test pause/resume and save/load; explain that restoring preserves cue/layer targets but restarts audio from its beginning. Hand me a playable iteration with the trigger controls and a focused question about the musical transition.

## Focused revision

> Keep **[approved cue]** and change only **[named track/section]**. The issue is **[too busy, weak motif, harsh attack, unclear bass, insufficient contrast]**. Try **[requested direction]** while preserving the approved melody/structure elsewhere. Save a new composition revision; do not overwrite the approved source or exports. Provide a section audition and the complete revised mix for comparison, noting any normalization change in isolated renders.

## Sound effects and external recordings

> Add distinct sounds for **[specific events]**. Use native synthesis for **[electronic/noise effects]** and sampled instruments or appropriately licensed WAV/FLAC for **[acoustic/recorded effects]**. Avoid giving every action the same beep. Keep rapid events from flooding the voice queue, balance effects against the music, and inspect clipping/drop counters under a short stress scenario.
>
> If importing audio, record its source and actual usage license. Keep sample banks and authoring tools out of the exported game. Supply an audition sheet or sequence and a playable version so I can judge clarity and comfort on **[target hardware]**.

Reviewed September 21, 2026 against the source audio feature revision 1 contract. These are prompt examples, not evidence that a requested game or soundtrack has been approved.
