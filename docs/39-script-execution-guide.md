# Script execution and failure diagnostics

Source feature revision 1, October 8, 2026. Discover it with
`langmake capabilities`: `scripts.execution.feature_revision` and
`scripts.execution.diagnostic_version` are both `1`. The engine version remains
0.5.0; version alone does not identify this change. Historical runtime/Studio
packages have not been regenerated and do not acquire this feature automatically.

## Execution allowance

Each outer protected Luau call has a 50 ms allowance. Load, start, update,
save migration, load/recovery hooks and lazy snapshot providers use the same guard.
Registered modules and nested engine callbacks stay inside their caller's
allowance; they cannot renew it. Interpreted and supported native Luau loops have
interrupt checks. Catching an interrupt with `pcall` or `xpcall` does not clear the
host's failure verdict or permit the outer call to succeed.

Windows measures the executing thread's **user plus kernel CPU time** through
`GetThreadTimes`. Sleeping, process suspension and time while the scheduler runs
another thread increase elapsed telemetry without being charged as script CPU.
Monotonic elapsed time only controls the 1 ms CPU-query cadence. A forced check
also runs when each protected call returns. Windows initialization/read failures,
counter regression or a change of executing thread fail explicitly with
`SCRIPT_CLOCK_FAILURE`; protection is never silently disabled.

Android/iOS retain their prior `CLOCK_THREAD_CPUTIME_ID` policy where available,
with an explicit elapsed fallback if that clock is unavailable. Other desktop
platforms retain their previous monotonic elapsed allowance. These policies are
source behavior, not new Apple or Android device qualification.

The 50 ms allowance is not a hard real-time deadline. Windows CPU counters can
advance in coarse increments despite being expressed in 100 ns units. The 1 ms
query interval does not guarantee 1 ms CPU precision or overshoot. Synchronous
native bindings are checked before and after their operation; an expensive
bounded native operation can exceed the allowance before it returns. The final
check rejects that hook even if no later Luau instruction executes. There is no
asynchronous cancellation of arbitrary C++ work, and worker-thread CPU is not
charged to the calling thread. These checks are not a process sandbox.

Migration input decoding deliberately runs inside a protected Lua call for
allocation safety, before starting the authored migration hook's clock. This
preserves the existing save contract; it does not grant the authored hook extra
CPU time. A failed decode retains its allocation error with authored timing
unavailable, and leaves the running world intact. See
[saves and migrations](13-runtime-guide.md#scenes-and-persistent-saves).

## Diagnostic contract

The existing script fields remain: `code`, `phase`, `file`, `tick`, `message` and
`source_revision`. Codes include `SCRIPT_ERROR`, `SCRIPT_BUDGET_EXCEEDED`,
`SCRIPT_MEMORY_LIMIT` and the new `SCRIPT_CLOCK_FAILURE`. CLI failures wrap this
record in `diagnostics`; embedded C++ calls still throw its JSON text through
`std::runtime_error`. Read optional fields defensively when supporting older
executables.

Revision 1 adds:

| Field | Meaning |
| --- | --- |
| `diagnostic_version` | Additive diagnostic revision, currently 1 |
| `execution.phase` | Protected call being measured |
| `execution.cpu_ms`, `execution.elapsed_ms` | Separate consumed CPU and elapsed durations; unavailable values are `null` |
| `execution.clock`, `fallback` | Actual accounting clock and whether elapsed fallback was used |
| `execution.timing_available` | False when no protected call was measured, such as compilation or an invalid hook value |
| `execution.clock_error`, `clock_os_error` | Explicit clock failure/fallback reason and OS error when supplied |
| `execution.cpu_queries`, `sample_interval_ms` | Query count and sampling cadence |
| `vm_memory_bytes`, `vm_memory_limit_bytes` | VM allocation at failure and unchanged 16 MiB limit |
| `location` | First captured authored frame, when available |
| `stack`, `stack_truncated` | At most 16 frames captured before VM unwinding; file/function/line/native binding only |

Trace collection uses fixed native buffers, no game callback, no global debug
trace buffer and no local-variable or argument payloads. File/function fields
are bounded to 255/95 bytes; messages to 2048 input bytes. Invalid or clipped
UTF-8 is replaced during JSON serialization. Missing locations are valid when
there is no available authored frame. Supplemental capture failure may set
`diagnostic_truncated`. The VM memory limit does not bound every native allocation.

The retained record and timing can be read passively through C++
`World::script_diagnostic()` and `World::script_execution()`; `World::tick()` also
avoids executing a provider. `World::statistics()` includes `script_execution`.
These accessors do not call Luau. The timing describes the last protected call,
not aggregate frame CPU or total time spent in every provider.
Use these accessors on the owning host thread; `World` does not add concurrent
inspection or mutation support.

## Failure and recovery

A failed gameplay hook, or any CPU/memory/clock failure, faults that `World`.
Further steps, saves and scene changes reject with the retained original diagnostic. Observation
of the faulted world's declared state skips lazy providers. That state may
contain partial mutations and must not be treated as a completed checkpoint or
gameplay acceptance. Ordinary snapshot-provider errors retain their existing
transactional retry behavior; their staged declared-state changes are discarded.

Reconstruct through `World::restore()` from a previously completed save, or load
a fresh world. Arbitrary Luau locals are not rolled back or serialized.
Opt-in preview recovery (`on_recover`) keeps its existing completed-save policy.
Preview failure reporting now reads passive tick/diagnostic accessors and writes
`script-failure.json` even when a provider would fail. That receipt retains the
original `diagnostic`, adds `script_diagnostic` when available, and records a
separate `recovery_diagnostic` if reconstruction fails. It does not replace the
original update failure with a recovery error.

An embedding application must handle `World::step()` exceptions and implement
its desired recovery/user experience. Rebuilding the engine does not add the
preview's recovery policy to a custom frontend host.

## Optional native binding measurements

```powershell
.\build\langmake.exe profile --project GAME --steps 180 --input TIMELINE.json --script-bindings
```

`simulate` also accepts `--script-bindings`. It enables measurement after world
construction for subsequent hooks. C++ hosts may use
`World::set_script_profiling(true)` between calls; games cannot select a clock,
raise their allowance or enable this host option themselves. Profiling is off by
default and stays enabled across restore/scene replacement when selected by the
host.

`profile.runtime.script_execution.bindings` reports per-last-hook `calls`,
`elapsed_ms`, `max_elapsed_ms`, `cpu_ms` and `max_cpu_ms` for `input_events`,
`ui_set` and `state_set`. Durations include each native binding's conversion,
copying and validation work. No event/state/UI payload is retained. Coarse CPU
counters can legitimately measure zero for short calls; this is not evidence of
zero work. Unavailable CPU or no calls produce `null`. Elapsed binding timing is
useful for sub-quantum operations but still includes scheduler delays. Profiling
adds clock reads and should be disabled for normal performance comparisons.

`scripts/script_execution_bench.py` generates small and large isolated engine
fixtures and compares a supplied baseline executable with a new one. It uses
three 180-step unprofiled runs per case and a separate profiled run. It never
opens a consumer project or contacts a provider/device. Each output must be new
and inside this engine workspace. Full JSON copy/serialization in `ui_set` and
`state_set` is unchanged by this repair; the measurements identify costs for a
future optimization without weakening atomic writes or JSON limits.

## Qualification

`tests/script_execution_tests.cpp` covers private deterministic clock failures
and fallback, real Windows sleep versus busy CPU, interpreted/native runaway
hooks, caught/repeated exceptions, registered-module locations, VM memory
failures, bounded malformed error text, passive observation, profiling, repeated
hooks and completed-save reconstruction. A bounded navigation rebuild verifies
native final-operation accounting and demonstrates that native overshoot is
possible. The callback preview regression exercises original-failure reporting,
safe recovery and distinct recovery errors.

The original frontend incident's cause remains undetermined. A controlled
suspension reproduction establishes the old wall-clock false-positive mechanism;
it does not prove that mechanism caused every observed incident. Current Windows
qualification is recorded separately in
[script execution evidence](releases/script-execution-1-evidence.json). No new
Android/iOS/macOS native builds, physical-device checks, frontend rebuilds or
packaged distributions are included in this engine repair.
