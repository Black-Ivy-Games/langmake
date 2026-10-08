# UI layout editor qualification

September 22, 2026. Runtime version 0.5.0, UI observation revision 1. The editor is an optional authoring tool; existing game formats and external-agent workflows remain supported.

## Executed checks

- Native UI tests verify source-document identity, computed styles/content boxes, anchors, resizing, state bindings, focus, pointer controls and existing input behavior.
- 79 Node tests pass across Studio and the editor: file containment, junction refusal, stale revisions, worker/play locks, protected project data, providers/tools, package validation and minimal RML patching. The parser preserves nested markup, actions, comments and unrelated elements; unsupported properties/complex inline styles diagnose.
- Native integration exercises isolated cooking/capture, geometry changes, source preservation until Apply, undo/redo, optimistic session revisions, notes/pins, screenshot exports, atomic single-document apply, persisted drafts, a phone profile at density 2 with an inset safe area, and rejection of an intervening stylesheet change.
- The actual Electron window exercises selection, width editing, undo/redo, a 32-pixel drag checked against subsequent native bounds, resizing without counting padding twice, changing anchors while preserving position, notes/pins and exported evidence, applying RML and the human Play lock. A saved draft resumes after restarting Studio.
- A screenshot of the running editor was visually inspected: native preview, selection outline, element list, property controls and agent notes are present and readable. This is technical UI verification, not human design approval.

All 13 native CTest suites pass. The committed summary is [UI editor evidence](releases/ui-editor-1-evidence.json).

Raw local evidence is under `.cache/ui-editor/`: `unit-final.log`, `integration.json`, `electron-evidence.json`, `ctest.log` and packaging reports. Electron screenshots are under the test directory named in `electron-evidence.json`. These generated files are excluded from Git; canonical test scripts are `scripts/ui-editor/editor.test.cjs`, `scripts/ui-editor/integration.cjs`, `studio/test/ui-layout.test.cjs` and `studio/test/ui-layout.cjs`.

## Reproduction

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1
ctest --test-dir build --output-on-failure
node --test scripts/ui-editor/editor.test.cjs studio/test/ui-layout.test.cjs
node scripts/ui-editor/integration.cjs
node studio/test/ui-layout.cjs
```

Use Node 22.12+ and the existing Studio dependencies for Electron checks. The integration scripts create isolated fixtures inside this workspace and open temporary native capture windows. They never attach to a human's existing session.

## Distribution and boundaries

New distributions use `dist/Langmake-0.5.0-ui1` and `dist/ui1/Langmake-Studio-0.5.0`; earlier packages remain intact. The runtime package includes the optional CLI service and guides; exported games do not require those tools. Studio calls the same service and uses its bundled Node runtime. Archive manifests and extracted-package results accompany the final distribution report.

Qualification is Windows/D3D11 with the current native renderer and Electron. Phone/tablet dimensions and safe areas are host previews, not new Android/iOS device qualification. There is no claim of a full commercial UI design application. The initial scope is existing identified elements in one active-profile RML document per draft, a bounded property subset and advisory agent directions. Complex transforms and structural edits remain source work. The editor captures a chosen scenario tick; script-controlled state may override an authored property and must be revised in gameplay code when that is the intended change.

Read [the workflow guide](34-ui-editor-guide.md) for limits, source-revision conflict handling, saved drafts and recovery from an interrupted operation. Passing automation does not replace human use of the editor or approval of the game's appearance.
