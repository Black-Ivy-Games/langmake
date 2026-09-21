# Glow Garden: a small shared-platform workflow starter

Guide a gardener along the path. Hold LIGHT near the three golden seeds, then carry them to the blue garden dock. A full collection meter means all seeds are gathered; the separate delivery message confirms completion. This is a short editable technical example, not a finished game or a human-approved design.

From the repository:

```powershell
build/langmake.exe new --template workflow-2d --output games/my-garden
build/langmake.exe context --project games/my-garden
build/langmake.exe verify --project games/my-garden --scenario delivery --profile desktop --graphics
build/langmake.exe verify --project games/my-garden --scenario release --profile phone --graphics
build/langmake.exe play --project games/my-garden --profile desktop
```

Use WASD to move, hold Space for LIGHT, P to pause, F6 to save and F9 to load the `garden` slot. The phone/tablet profiles keep the same entities and mechanics, with a native left movement stick and an authored LIGHT button on the right. The other authored buttons pause/continue, save and load. The LIGHT button has a larger invisible hit region and an active style; holding it must work while another finger moves the stick.

The app ID `org.langmake.example.glowgarden`, `project_id` and compatibility ID are starter placeholders. Choose your new game's stable identities before its first deployment. No device, Mac, SSH config, signing team or credentials are included. Read [the game workflow](../../docs/26-game-workflow.md) and configure targets independently for this game. Never copy `.langmake/` from a different project.

After deploying an explicitly configured mobile target, use `session launch --project games/my-garden --target TARGET_NAME` to reopen its installed artifact for the human without rebuilding. A debug verification target can instead use `--development --fresh` for live technical controls. Bounded `verify --target TARGET_NAME` scenarios end their app session; relaunch before requesting a live capture or save/load receipt. These commands require the current source/profile, runtime and target configuration to match the recorded deployment. See the workflow guide's complete step/save/load/capture example; put transient input JSON under the game's `.langmake/` directory so it does not change registered sources.

`data/balance.json` holds speed, collection reach and music level. Tune these from human feedback. Declared state exposes `collected`, `delivered`, `reward_count`, `held_ticks` and `release_count`; a fresh-process restore does not award delivery again. The two registered scenarios demonstrate completion and a real action release, and both request a save/restore check. They do not prove physical multitouch comfort or enjoyment. The `on_load` hook refreshes presentation from restored state; it does not rerun initialization.

The desktop, phone and tablet profiles replace whole `ui` and `input` objects. They do not fork gameplay or widen collision geometry. The orthographic world stays shared; author different camera/framing policies intentionally if your game needs them. Use `ui-check` at the intended viewport/density/safe area and read warnings before building. The starter has simple ASCII labels supported by the bundled font.

The opaque 1024px launcher icon comes from `assets/icon.sprite.json`, an editable native sprite recipe. Regenerate it after editing the recipe:

```powershell
build/langmake.exe asset render --project games/my-garden --id app_icon --output games/my-garden/.langmake/icon-1024.png --scale 8
Copy-Item -LiteralPath games/my-garden/.langmake/icon-1024.png -Destination games/my-garden/assets/icon.png -Force
```

The recipe is 128×128 and the supported scale of 8 produces 1024×1024. Rendering to a scratch path first preserves the renderer's protection against overwriting authored sources; copy the reviewed result into the registered icon afterward. Application `version` and `build` describe the installed app, independently of `save_version`. Bump build numbers intentionally for app updates. The explicit presentation-compatibility declaration permits only changes that pass the engine's structural fingerprint checks; gameplay/source changes still need assessment and, when necessary, a migration.

Before handing over a phone, verify actual input: move and hold LIGHT together, release, drag outside the hit region, pause while holding, background and resume. Confirm that the light stops and no extra seeds are awarded. Let the human judge reach, readability, sound level and the clarity of collection versus delivery. Record their feedback in the game's notes, then regenerate a handoff with `langmake handoff --project games/my-garden`.
