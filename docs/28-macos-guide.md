# macOS game deployment

September 21, 2026, Langmake 0.5.0 source: the desktop runtime now includes an
SDL/Metal path and standalone local app packager. See the
[native command reference](../scripts/macos/README.md). Qualification is limited
to the specific native capture and application-launch evidence recorded for a
game; adding the backend does not qualify every Mac or Intel architecture.

Use the human-authorized Mac account/workspace. Build `langmake` for authoring and
`langmake_macos_player` for the exported app, then package an explicit desktop
profile. Never use the iPhone app or an iOS simulator as evidence of native Mac
support. The per-game workflow facade currently supports Windows, Android and
iOS targets; macOS uses its standalone packager, not an invented facade target.

Local packages use ad-hoc signing. Public distribution requires a separate
Developer ID/notarization or Mac App Store workflow. This tool does not upload,
register identifiers, accept Apple agreements or obtain signing credentials.
Keep portable identity separate from SSH and signing secrets. Use a macOS
application override or the explicit `--bundle-id` package argument.

Verify an actual Metal capture and sidecar, audio initialization, app launch,
desktop input and readable UI. Protect the human's play session after handing
over. Report packaging, technical checks, human feedback and public distribution
as distinct states. The player keeps saved progress outside its signed bundle.
