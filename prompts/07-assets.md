# Structure: native art, animation and music

Describe the asset's purpose and how it will read in play, then provide the constraints that matter. Ask for only the asset types this task needs. A still image cannot establish animation quality, and unclipped audio cannot establish that music suits the game.

```text
TASK MODE: Create and integrate these assets into my existing game.

Engine package: [ENGINE_PACKAGE_DIRECTORY]
Project: [EXISTING_PROJECT_DIRECTORY]
Authorized workspace: [DIRECTORY]
Read AGENTS.md, prompts/00-workflow.md, the native asset guide and the
relevant asset schemas. Inspect the current asset IDs and references first.
For music, also read docs/30-music-and-sound-guide.md and discover the installed
instruments. Use prompts/12-music-and-sound.md for a soundtrack-specific brief.

PURPOSE AND DIRECTION
These assets support [PLAYER ACTION, CHARACTER OR SCENE].
The style should feel [MOOD], using [PALETTE, SHAPES OR MUSICAL DIRECTION].
They must remain readable at [ACTUAL GAME VIEW/SIZE].

DELIVERABLES
- [SPRITE: PURPOSE, APPROXIMATE DIMENSIONS, FRAMES AND REQUIRED STATES].
- [MODEL: SCALE, SILHOUETTE, SIMPLE JOINTS AND REQUIRED ANIMATION CLIPS].
- [MUSIC: SYNTHESIZED OR SAMPLED CUE, MOOD, RHYTHM, PHRASING AND INTENSITY].
- [SOUND EFFECTS: DISTINCT CUES AND THEIR GAMEPLAY TRIGGERS].
Use the native text-authoring pipeline for the selected deliverables.
Keep original recipes editable and register all required dependencies.
For sampled composition, retain the note source and instrument provenance,
then bake synchronized stems. Offer contrasting auditions before developing
the soundtrack. More instruments alone do not establish better music.

INTEGRATION
Replace or add [ASSET IDS]. Keep [GAMEPLAY RULES AND OTHER VISUAL CHOICES].
Make visual and collision choices explicitly: a detailed visual model does
not automatically become matching collision geometry. Keep music and effects
balanced enough for important cues to remain audible.

CHECK AND HAND OFF
Validate affected assets/scripts. Render sprites/models and capture animation
at useful ticks in the actual game view. For audio, export a WAV and report
duration, levels and clipping; inspect event timing with native tools.
Give me the updated player launch command and relevant previews/audio files.
I will judge visual style, motion and sound in play. Ask focused questions
about recognition, animation feel and musical fit as appropriate.
```

Example deliverables for Star Courier:

```text
Create a compact teal courier sprite with a clear forward direction and
three thruster frames. Make an orange triangular drone visibly different
from the round cyan station. Use five-frame-or-fewer flashes for pickups
and damage. Keep shapes readable against a dark starfield.

Compose a short looping electronic theme with a light pulse melody, quiet
bass and restrained percussion. Give collecting, damage, victory and defeat
distinct synthesized cues. Preserve movement and damage rules. Show the
sprites in game and provide the theme WAV for my listening feedback.
```

If the human asks for outside assets, add their permitted sources, reuse terms and attribution requirements explicitly. Supported imports and synthesis limits belong to the current runtime guide, not assumptions about an asset's file extension or a musical genre.

Return to the [library](README.md) or [new-game structure](01-new-game-template.md).
