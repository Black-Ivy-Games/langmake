# Windows game icon and version export

The optional [game workflow](../../docs/26-game-workflow.md) invokes this tool after copying the runtime into a portable game. It uses Win32 resource APIs and the Python standard library; no compiler or global package is installed.

```powershell
python scripts/windows/windows.py apply --project EXPORTED/game/project.json --executable EXPORTED/runtime/langmake.exe --source-executable build/langmake.exe --configuration release
```

The destination must be a distinct existing copy. Same paths and hard links are rejected; the original executable's hash is checked afterward. Application metadata comes from the resolved cooked project. Icons use opaque square non-interlaced 8-bit RGB/RGBA or 1/2/4/8-bit indexed PNG, 256..4096px. The tool generates seven PNG-backed icon resources, a group icon and game version strings, then reads them back through Windows APIs. It changes the game copy's executable hash; generate final package fingerprints afterward. A signed executable would require signing again after resource changes. Legacy projects without application metadata keep the player resources.

`python scripts/windows/test_windows.py` includes a native copy/update/resource-read/version-query/CLI-execution test when a local Windows player exists. This does not claim Windows Store publication, installer signing or visual Explorer cache behavior.
