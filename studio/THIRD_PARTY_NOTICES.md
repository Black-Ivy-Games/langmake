# Studio third-party notices

Langmake Studio's authored application code uses the repository's MIT license.

The optional desktop distribution contains Electron44.4.3 (MIT) and its Chromium/Node dependencies. Preserve Electron's `LICENSE`, `LICENSES.chromium.html` and other supplied notices in the distribution. Full transitive runtime notices are in those files.

Development-only pinned tools are `@electron/packager`20.3.0 (BSD-2-Clause) and `playwright-core`1.63.0 (Apache-2.0). Their transitive versions and integrity hashes are fixed by `package-lock.json`; packaging emits `studio-dependencies.json`. Development packages are not shipped as application dependencies.

No Claude Agent SDK, Claude Code executable or Codex executable is bundled or automatically installed. Studio's optional external CLI adapters communicate with user-supplied software under its own license and terms. Direct API adapters use built-in Node networking. The engine inside `runtime/` has its own dependency lock and third-party notices.
