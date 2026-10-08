# Frontend integration handoff: script execution and diagnostics

Prepared October 8, 2026, for the Cabi frontend development agent.

**Status:** the Windows engine source build is ready for frontend integration. The engine repair and graphics/live regression suite passed. The frontend has not been rebuilt, packaged, deployed or qualified against this repair. This document records the completed engine work and recommends the next frontend iteration; the human assigns and authorizes that iteration.

## Read first and preserve scope

Engine workspace: `.`. Frontend workspace: `./games\cabi-frontend`.

Read the frontend's current `AGENTS.md`, `README.md`, `docs/IMPLEMENTATION_HANDOFF.md`, `docs/PROTOCOL.md` and `docs/ACCEPTANCE.md` before frontend work. Follow their Core-document references only within the access scope granted to that agent. Read these engine records:

- [Execution contract](39-script-execution-guide.md)
- [Qualification and fingerprints](releases/script-execution-1-evidence.json)
- [Public World API](../include/langmake/world.hpp)

Preserve all existing dirty/untracked work and human sessions. The engine began with 99 Git status entries; 106 remained immediately before this handoff was added. No commit, reset or stash was made. `main` HEAD is `3c55bae7372c29ad1ae9ed6aa1c5d85537841494`; this commit alone does not identify the implemented working tree. Pre-repair status and selected source copies are in `./.cache\script-budget-repair-20261008\status-before.txt` and `before\`. Those copies cover selected repair files, not the complete dirty checkout.

Runtime and exported games remain independent of Studio, model providers and credentials. No frontend/Core/Studio code was changed for this repair. Core remains authoritative for financial/device state, and required-child supervision remains intact. Shared runtime data, cabinet installations and external installations were untouched. The frontend's separate `core-runtime` uses a real shared workstation identity/database; it is not a disposable test environment.

## What was repaired

Windows previously enforced the 50 ms Luau allowance with elapsed time. A controlled script-thread suspension in the supplied frontend investigation reproduced a false rejection with negligible thread CPU. That demonstrates a faulty clock mechanism; it does **not** establish the cause of the original incident around 11:11 a.m. Mountain, at update tick 33431 and source revision `fnv1a64-3d3fad583ce33074`.

The repair changes Windows accounting to executing-thread CPU, adds bounded failure evidence and optional binding measurements, and prevents continued use of a faulted World. The original console `q`/Enter versus Core pipe-input question remains outside this engine change.

| Repair files | Result |
| --- | --- |
| `src/script_budget.hpp`, `src/world.cpp` | CPU guard, final-return checks, sticky failures, bounded pre-unwind trace, retained diagnostics |
| `src/world_services.inc`, `src/world_materials.inc` | Nested calls share the guard; binding measurements; protected migration decoding; failed-world providers skipped |
| `include/langmake/world.hpp` | Passive tick/execution/diagnostic accessors and host profiling switch |
| `src/preview.cpp` | Original failure survives observation/recovery errors; structured failure receipt |
| `src/main.cpp`, `src/project.cpp` | CLI profiling flag and capabilities discovery |
| `CMakeLists.txt`, `tests/script_execution_tests.cpp`, `tests/preview_session_tests.cpp` | Native guard/diagnostic/recovery regression coverage |
| `scripts/script_execution_bench.py` | Isolated before/after synthetic measurements |
| `README.md`, `docs/08-agent-api.md`, `docs/13-runtime-guide.md`, `docs/39-script-execution-guide.md`, release evidence | Current canonical contract and qualification |
| `prompts/06-bug-report.md`, `prompts/README.md`, `scripts/package.ps1`, `scripts/workflow/workflow.py` | Prompt review and canonical discovery/package mapping |

Full UI/state copies, JSON serialization, write semantics and size limits were preserved. Batching, unchanged-value optimization, new binding APIs, configurable budgets, asynchronous native cancellation and Cabi automatic recovery were deferred. Existing unrelated audio/editor/mobile/Studio changes remain in the working tree; a full diff against HEAD includes them.

## Budget semantics inherited by a rebuilt frontend

- Each outer protected Luau call receives **50 ms**. Load/start/update, authored migration, load/recovery hooks and lazy providers use this boundary. Nested module loads, Lua catches and engine callbacks do not renew it.
- Windows uses `GetThreadTimes(GetCurrentThread())`, adding user and kernel durations in integer units. The baseline belongs to the thread actually executing the hook. VM work and synchronous native bindings on that thread count; suspension, sleep, scheduling delays and other threads' CPU do not.
- Elapsed telemetry uses a monotonic clock. It schedules CPU queries at a 1 ms cadence but does not determine Windows budget exhaustion. A forced query runs on protected return, with checks before/after registered engine native bindings. VM interrupt checks cover interpreted and supported native Luau execution.
- A caught `pcall`/`xpcall` interrupt remains a host failure. The outer call cannot succeed by catching it repeatedly.
- Windows clock initialization/read failure, regression or executing-thread change fails explicitly as `SCRIPT_CLOCK_FAILURE`; there is no silent elapsed fallback on Windows. Android/iOS retain thread-CPU accounting where available and an explicit elapsed fallback. Other desktop platforms retain the prior elapsed policy. Only Windows was newly qualified.
- Counter units do not imply precision. Observed Windows increments were often 15.625 ms. The 1 ms query cadence is neither 1 ms CPU resolution nor a hard deadline. A synchronous C++ operation can overshoot before returning; a test consuming about 891 ms CPU was rejected on return. Arbitrary native work is not asynchronously preempted.
- Native migration input decoding is allocation-protected before the authored migration clock starts. Decode failure preserves memory classification, reports authored timing unavailable, and leaves the prior running World intact.

Do not raise the allowance or disable the guard to integrate this repair. CPU accounting fixes false rejection during pauses; it does not guarantee native I/O responsiveness or window liveness.

## Diagnostic and host API contract

Embedded failures still throw `std::runtime_error` containing the JSON diagnostic. Existing fields remain `code`, `phase`, `file`, `tick`, `message`, `source_revision`. The CLI wraps records in `diagnostics`; an embedded host receives the record directly. Codes are `SCRIPT_ERROR`, `SCRIPT_BUDGET_EXCEEDED`, `SCRIPT_MEMORY_LIMIT`, and additive `SCRIPT_CLOCK_FAILURE`.

New optional fields:

| Field | Meaning |
| --- | --- |
| `diagnostic_version` | `1` for this repair |
| `execution` | `budget_ms`, clock, phase, separate CPU/elapsed ms, timing availability, fallback/reason/OS error, query count/cadence and profiling state |
| `vm_memory_bytes`, `vm_memory_limit_bytes` | VM allocation and unchanged 16 MiB cap |
| `location` | First authored frame, if captured; may identify a required module more precisely than legacy `file` |
| `stack`, `stack_truncated` | Up to 16 pre-unwind frames, file/function/line/native-binding flag |
| `diagnostic_truncated` | May appear when supplemental diagnostic capture fails |

Trace strings are bounded to 255 file bytes and 95 function bytes; message input to 2048 bytes. Invalid/clipped UTF-8 is replaced during serialization. Trace collection executes no authored error handler/provider and captures no locals, arguments, state or IPC payloads. Missing location and `null` timing are valid. The VM cap does not cover every native allocation; process-wide allocation/I/O failure can still prevent rich capture or persistence.

An actual ordinary-error record from the final callback preview fixture follows. Only repository path prefixes were replaced with `<project>`; this is a synthetic engine fixture, not the original frontend incident. A measured CPU zero is possible because of counter granularity.

```json
{
  "code": "SCRIPT_ERROR",
  "diagnostic_version": 1,
  "phase": "update",
  "file": "<project>/scripts/game.luau",
  "tick": 0,
  "message": "<project>/scripts/game.luau:3: original update failure",
  "source_revision": "fnv1a64-85b5d620df56f46e",
  "execution": {
    "binding_profiling": false,
    "budget_ms": 50,
    "clock": "windows_thread_cpu",
    "clock_error": "",
    "clock_os_error": 0,
    "cpu_ms": 0.0,
    "cpu_queries": 2,
    "elapsed_ms": 0.0638,
    "fallback": false,
    "phase": "update",
    "sample_interval_ms": 1,
    "timing_available": true
  },
  "vm_memory_bytes": 379064,
  "vm_memory_limit_bytes": 16777216,
  "location": {
    "file": "<project>/scripts/game.luau",
    "function": "update",
    "line": 3,
    "native_binding": false
  },
  "stack": [
    {"file": "=[C]", "function": "script_error_capture", "line": -1, "native_binding": true},
    {"file": "=[C]", "function": "error", "line": -1, "native_binding": true},
    {"file": "<project>/scripts/game.luau", "function": "update", "line": 3, "native_binding": false}
  ],
  "stack_truncated": false
}
```

Original receipt: `./.cache\preview-session-1791491860357\script-fault\script-failure.json`.

Public C++ additions:

```cpp
std::uint64_t World::tick() const noexcept;
Json World::script_execution() const;
Json World::script_diagnostic() const;
void World::set_script_profiling(bool enabled);
```

The three getters execute no Luau. `statistics()` also includes `script_execution`; timing describes the last protected call, not the whole frame. `script_diagnostic()` can be null before failure or after reconstruction. Use World on its owning host thread; these APIs add no concurrent-access guarantee. Profiling must be selected between calls and is off by default.

For a frontend catch path, retain the World until diagnostic collection completes. Preserve the original exception text first; prefer its retained structured diagnostic when available, parse JSON defensively for older binaries/non-script exceptions, and keep plain-text fallback. Use passive `tick()`/diagnostic accessors instead of calling providers to discover the failure. Persist bounded evidence before teardown without dumping private Core payloads. Optional host logging/API adoption needs the frontend's assigned implementation scope; rebuilding alone keeps serialized exceptions compatible.

## World, bundled preview and Cabi recovery boundaries

**World:** failed gameplay hooks and CPU/memory/clock failures fault the instance. Subsequent step/save/scene change rejects the retained original failure. Passive declared-state observation skips providers but may show partial mutations; it is not a safe checkpoint. Ordinary snapshot-provider errors retain transactional retry semantics. Reconstruct from a previously completed save via `restore()`, or create a fresh World. Arbitrary Lua locals are not rolled back.

**Bundled preview/player:** existing opt-in `on_recover` completed-save recovery remains. `script-failure.json` retains `diagnostic` text, `tick` and `recovered`; it adds `script_diagnostic` and a distinct `recovery_diagnostic` when reconstruction fails. Failure persistence is best effort. Partial failed state is discarded on successful checkpoint reconstruction.

**Custom Cabi host:** the current `src/main.cpp` calls `world->step()` in its frame loop and has a top-level catch returning exit code 1. A rebuild inherits CPU accounting and better exceptions, but does not inherit the bundled preview's recovery UI, checkpoint policy or receipt writer. Default true-failure behavior remains terminal unless the human separately defines a frontend recovery policy.

Do not introduce automatic retries, replay queued financial/device intents, or restore stale Core authority from a game save. Any future recovery requires a fresh authoritative Core snapshot and an explicit outbox/sequence/intent policy agreed with the human. Preserve Core supervision and existing IPC/financial/device behavior.

## Build identity and frontend adoption

Qualified engine command, run from `.`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Jobs 4
```

Build directory: `./build`, Windows x64 Release/D3D11. Existing toolchain: CMake 4.0.2, Ninja 1.12.1, MinGW GCC 15.1.0 (installation path omitted). Relevant unchanged pins: Luau 0.692 (`9457397af45f9d69423c5168d23fddd998a5f0fd`), SDL 3.4.16 (`fa2c02bb6e21974a89ea9824bc53c9932abe5f9c`), nlohmann JSON 3.12.0 (`55f93686c01528224f448c19128836e7df245f72`). The full dependency inventory is in `CMakeLists.txt` and `dependencies.lock.json`; the latter already contained unrelated dirty work and was not changed by this repair. No tools were installed or pinned dependency checkouts edited.

Engine artifacts, SHA256:

| Artifact | SHA256 |
| --- | --- |
| `build/langmake.exe` | `1fe951934390a5b7a958e4196b101392ef083e56f88a4fe56448215baed70021` |
| `build/liblangmake_core.a` | `43c5656963ba428d8fd90d363839ec3471e13c71c8a306a0fd10feab90785c04` |
| Pre-repair executable in `before/langmake.exe` | `d3e6bc04376ecc74209bfc7288a15e11483def73fa24ce80b65b81977a24f348` |

Source fingerprints for integration receipts:

| File | SHA256 |
| --- | --- |
| `src/world.cpp` | `56b312762260ddca7b283fcd52fe5a3d93a337638e4284414c3a60907e4e69f3` |
| `src/world_services.inc` | `a4b4cddd27b1107a8ce9bb544c070d284900faca4b73c19185de20550f63c972` |
| `src/script_budget.hpp` | `f611295fcaaa79c46d58a4fcd3fb7f9ff61a717130252c72a566e9aaf86f2b37` |
| `src/world_materials.inc` | `c2d0b298ec1957d325cb00a494291f47b2f8ef3f1a0c1e2afde8cce4ec2fdaa9` |
| `include/langmake/world.hpp` | `7e8e217a38f515fed1227855816d17ab3242468b5b01f49855195898359574e7` |
| `src/project.cpp` | `09753dfbb3c9c86ff98bee4706daf3426febe89a7e4d9f2ffd25b42229e9ff10` |
| `src/preview.cpp` | `03a8b9bd5a47cc2caf3f806e440cbdcf171a6e3b3ca0dd43769b9c0b953fe707` |
| `src/main.cpp` | `7ce093ef966ce6b2a57f82efd745ee72a8ee5ce4d5618a408661587fec9ef644` |
| `CMakeLists.txt` | `6015b33694aa38c48c96261119ef62bf3a9a2110bafcf50f09f0de74b5457bb0` |
| `dependencies.lock.json` | `5d74d4f48b12eb60517a42fdce693812dd6ed6ee7647f3e089ae1c9a92a3d014` |

Discovery command `build\langmake.exe capabilities` reports `scripts.execution.feature_revision=1` and `diagnostic_version=1`. Engine version remains 0.5.0; version alone is insufficient. No engine or frontend package was produced. Historical `dist/` packages and Core-installed frontend executables do not acquire the repair automatically.

The frontend's CMake currently compiles engine sources into its own `cabi_langmake` library and links existing engine dependency archives. It does **not** consume `build/liblangmake_core.a` or run the engine CLI for gameplay. Therefore replacing the engine executable or copying that core archive alone cannot update Cabi.

After the human authorizes frontend integration:

1. Inspect current frontend changes and preserve them. Confirm the source/header fingerprints still match this handoff; stop to reconcile changed contracts rather than overwriting newer work.
2. Rebuild the frontend's Debug and Release configurations against current engine sources/dependency archives. The new private header is included by `world.cpp`; it is not a separate translation unit or new library dependency. Reconfiguration is appropriate; no guard disable, new SDK or engine linking redesign is required.
3. Refresh `data/native-dependencies.json` from actual inputs. Its current `world.cpp` entry still has pre-repair SHA `66854916825a3efc1372e73758af113fa51e7834968cc983baf355d4af9fa334`. Include transitive inputs `script_budget.hpp`, `world_services.inc`, `world_materials.inc` and `world.hpp`, not just listed `.cpp` files and archives. Add feature/diagnostic revision and new frontend binary/configuration identities to an appropriate integration receipt while preserving existing receipt consumers. The build script currently does not regenerate this receipt, and the package script copies it verbatim; automate or explicitly perform refresh and verify it before any separately authorized package.
4. Adopt passive failure logging if assigned, retaining original exceptions and existing true-failure exit behavior. Optional binding profiling is a diagnostic host choice; no Luau/frontend binding rewrite is needed.

Existing frontend commands, supplied for that later authorized iteration and **not run during this engine work**:

```powershell
# Working directory: ./games\cabi-frontend
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Debug
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release
powershell -ExecutionPolicy Bypass -File scripts/test.ps1 -Configuration Debug -Graphics
powershell -ExecutionPolicy Bypass -File scripts/test.ps1 -Configuration Release
```

Frontend output directories are `build\Debug` and `build\Release`. Debug builds the separate `HeadlessFrontend-Debug.exe` and fake-Core tools; the Core-facing frontend rejects test-only flags even in Debug. Use the custom host's documented scenarios and fake-Core workflow for replay, not generic engine `simulate`. These builds reference Core/Mule sources and existing external libraries; the receiving agent must have authorization for those read/build inputs. Packaging, Core fetch/install/deploy and live Core/cabinet testing remain separate steps.

## Qualification, measurements and remaining acceptance

Completed engine verification:

```powershell
# Working directory: .
build\script_execution_tests.exe
powershell -ExecutionPolicy Bypass -File scripts/test.ps1 -Graphics
```

Both passed. The final full suite exited 0 with 15/15 native tests plus authoring/integration, deterministic simulation, assets/cooking/audio, graphics references and live-control/workflow checks. Mobile tool mocks/cooked targets do not imply device qualification.

Receipts under `./.cache\script-budget-repair-20261008`:

- `qualified-suite.log`: final complete suite on the executable fingerprint above.
- `focused.log`: final standalone guard tests and measured Windows/native cases.
- `qualified-measurements\benchmark.json`: three 180-step unprofiled before/after runs per synthetic case, plus separate profiled runs.

Coverage includes real Windows sleep versus busy CPU, deterministic clock failure/regression/thread-change/fallback seams, interpreted/native load/start/update runaways, repeated caught errors, module/provider CPU and VM-memory failures, bounded malformed UTF-8/source traces, independent repeated hooks/Worlds, passive fault observation, save/scene refusal, transactional reconstruction/migration decode and final native-operation accounting. Preview regressions preserve the original update error even when a provider would fail, discard partial mutations during successful completed-save recovery, and retain a separate failed-recovery diagnostic.

Observed sleep: 0 ms charged CPU across 96.3066 ms elapsed. Busy fixture: 62.5 ms CPU, 55 ms elapsed, 56 queries. These coarse-counter observations are not a precision guarantee. Bounded final navigation work: 890.625 ms CPU, 893.6032 ms elapsed, rejected as budget exhaustion on return.

Synthetic unprofiled p50 hook timings: small 0.0697 ms before / 0.0715 ms after; large 5.9724 / 6.0578 ms, roughly 1–3% differences. No speedup is claimed. Optional profiling adds reads and timing noise; small profiled p50 was about 9.7% above unprofiled after. A large measured hook attributed about 0.057 ms to one `input_events`, 2.790 ms to four `state_set`, and 3.284 ms to four `ui_set` calls. Full JSON conversion/copy/serialization remains the main large-fixture cost. Those costs are not proof of the original incident's cause.

When scoped by the human, the frontend agent should repeat existing headless/native/fake-Core checks, the supplied informational-message replay, and explicit true-error/budget/memory cases. Verify preserved diagnostics, agreed exit code 1 on terminal failures, current dependency fingerprints, unchanged Core protocol/outbox behavior and visual/input layout. Record frontend binary, packaged script, configuration and receipts separately from engine evidence. Passing engine tests is not frontend or human acceptance.

For any later authorized live incident investigation, collect bounded CPU/elapsed/clock/location/VM evidence and independently measured host frame stages. Preserve private traffic. Do not infer a pure hook duration from whole-frame timings or convert raw CPU cycles into milliseconds using an assumed frequency. Do not use debugger memory-offset probes, pause an active cabinet or replay financial/device actions merely to qualify this repair.

## Final process and support state

At 2:52 p.m. Mountain on October 8, a read-only check found zero repository engine/guard/preview-fixture processes with the inspected executable names. No diagnostic fixture launched by this work remains running. The check did not audit external Core, frontend or cabinet processes; do not infer their current state from the earlier supplied investigation. No human session was interrupted.

The new qualification is Windows x64 Release/D3D11 only. No new Android/iOS/macOS native build, simulator/device, frontend, Core, physical-cabinet, printing/accounting or human visual/touch/audio qualification was performed. Historical packages remain separate artifacts. The original incident cause is unresolved; the corrected clock, measured evidence and failure APIs provide a better basis for the next authorized frontend iteration.
