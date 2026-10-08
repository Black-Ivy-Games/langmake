# macOS local game packaging

The source runtime supports a native SDL/Metal desktop window on macOS. This is a
local testing path, separate from iOS and the Windows workflow facade. It does not
provide Developer ID signing, notarization, a Mac App Store archive, or an Intel
qualification claim. Read [the Mac guide](../../docs/28-macos-guide.md).

On the explicitly authorized Mac, with Xcode and project-local CMake:

```sh
cmake -S . -B .cache/macos/native -G Xcode -DBUILD_TESTING=OFF -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build .cache/macos/native --config Release --target langmake langmake_macos_player --parallel 4
python3 scripts/macos/macos.py --project games/GAME --profile desktop --host-build .cache/macos/native/Release --output games/GAME/Mac-release --bundle-id com.example.game
```

The output must be new. The packager cooks the registered graph with target
`macos`, bundles native Metal shaders, font, notices and generated icon, and
ad-hoc signs/verifies the resulting app. `build-report.json` records content hashes,
architecture, revisions, signing and qualification status. No tools are installed.
All selected inputs and artifacts must remain within this workspace.

Open the returned `.app` in Finder or with `open /absolute/path/Game.app`. The
player has no developer overlay or source watching and writes saves to
`~/Library/Application Support/Langmake/PROJECT_ID`, outside the signed bundle.
Mouse, keyboard and gamepad use the desktop presentation. Native CLI `capture`
retains bounded verification with sidecars; it is a separate executable from the
standalone player. Mac touch/windowing and human gameplay acceptance must not be
inferred from Windows or iOS tests.
