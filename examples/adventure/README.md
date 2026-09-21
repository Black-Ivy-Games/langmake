# Signal Orchard
An original small adventure reference: two areas, persistent branching dialogue,
quest, equipment and inventory modules, capsule movement, native skeletal animation,
marker-driven melee, camera/actor tracks, anchored HUD and player saves.

Run `langmake play --project <this-directory>` (or `build/langmake.exe play --project examples/adventure`
from the repository). WASD moves; E talks/uses; 1/2 choose dialogue; Space strikes.
F6 saves slot `journey`, F9 loads it after you have saved.

Talk to Ada on your right, collect the amber coil, hit the red sentinel three
times, then use the blue door. In the relay room, approach the amber machine and
press E. You can return through the blue marker afterward.

All character parts, clips and music are text-authored. Modules in `modules/`
are editable building blocks, not fixed engine story rules. Progress uses engine
state; `on_load` restores UI without granting items again. Saves require a matching
source revision. Existing project edits restart preview gameplay.

Technical verification uses the recorded input timeline and state/capture
commands. Human playtesting remains required: Is interaction range clear? Does
movement/camera feel comfortable? What should the next story choice change?
