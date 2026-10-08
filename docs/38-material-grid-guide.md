# Native material grids and bound state

Langmake exposes a native material grid for games that need a bounded powder/fluid board without creating one entity per sample. The API is shared runtime API, but the current kernel is specific: `classic-chemistry-v1`, a fixed material simulation used by Poppleworks-style matter puzzles. It is not an arbitrary chemistry DSL.

Current qualification for this API is Windows-only. Do not treat this document as Android, iOS or other mobile qualification.

## Grid storage

Create a grid from Luau with:

```lua
local q = engine.material_new(width, height, scale)
```

The result is a table with numeric metadata and four typed userdata channels:

- `q.m`: material id
- `q.t`: temperature
- `q.u`: auxiliary/wetness/solution payload
- `q.b`: burn counter
- `q.width`, `q.height`, `q.scale`

Channel indexing from Luau is 1-based: `q.m[1]` is the first sample. Out-of-range reads return `nil`. Writes reject out-of-range indices, non-finite or extremely large values, and material ids outside integer `0..26`.

Native geometry arguments are sample coordinates and are zero-based. For example, `engine.material_line(q, 0, 0, 10, 0, false)` starts at native sample `(0, 0)`. This differs from channel indexing. The optional `lift` callback passed to `material_step` receives 1-based scaled coordinates (`1 + x / scale`, `1 + y / scale`) because it is meant for authored gameplay tests rather than direct channel indexing.

Grid dimensions are bounded by the native constructor: width and height must be `1..1024`, `width * height` must be at most `262144`, and `scale` must be `1..8`. Live material storage in one world is also budgeted: active native grids held by Lua must total at most `1048576` live samples. Dead weak references are discarded before checking this budget.

## Material slots

The native kernel uses stable numeric slots `0..26`. Slot `0` is empty/air in the native grid. Poppleworks' catalogue names `1..26` as:

| Id | Material |
| --- | --- |
| 1 | Reserved catalogue label Air; author empty space as slot 0 |
| 2 | Dirt / loose soil |
| 3 | Mud |
| 4 | Quartz sand |
| 5 | Gravel |
| 6 | Clay |
| 7 | Wet clay |
| 8 | Limestone |
| 9 | Basalt |
| 10 | Water |
| 11 | Ice |
| 12 | Water vapor / steam |
| 13 | Sodium chloride / salt |
| 14 | Brine |
| 15 | Vegetable oil |
| 16 | Wood |
| 17 | Charcoal |
| 18 | Wood ash |
| 19 | Smoke-bearing air |
| 20 | Carbon dioxide |
| 21 | Vinegar / dilute acetic acid solution |
| 22 | Calcium acetate solution |
| 23 | Basaltic lava |
| 24 | Glass |
| 25 | Fired clay / ceramic |
| 26 | Iron |

The kernel classifies fluids, gases, powders and solids internally. These classifications are part of the implemented rules, not project-authored data.

## Simulation API

The public functions are:

```lua
local packed = engine.material_pack(q)
engine.material_unpack(q, packed, source_width, source_height)
local counts = engine.material_counts(q)
local clear = engine.material_line(q, x, y, tx, ty, endpoint)
local moved = engine.material_pull(q, x, y, radius, blocked, strength, hold)
local changed = engine.material_step(q, blocked, environment, grate, lift)
local rev = engine.material_revision(q)
local fx = engine.material_effects(q)
engine.material_surface(q, "surface_entity_id", palette)
```

`material_pack` run-length encodes all channels. `material_unpack` accepts an exact snapshot or a snapshot whose source dimensions evenly divide the destination; each source sample expands over the matching destination block. Corrupt unpack is staged through a temporary grid, so a rejected unpack does not partially mutate the existing grid.

`material_counts` returns `materials`, `hot`, `burning`, and `checksum`. `materials` is a 26-element count array for ids `1..26`, with id `0` counted in the first bucket by the current implementation.

`material_line` tests whether a line crosses solid material. Coordinates must stay within `+/-4096`.

`material_pull` pulls fluid samples toward a zero-based native sample coordinate. `radius` must be `0..512`. The optional `blocked` table uses 1-based sample indices. The function writes `q.held` as a 1-based array of samples that moved or were reserved during pull, and returns the move count. Optional `strength` is an integer 1..4 (default 1), multiplying the bounded movement passes. Optional `hold=true` reserves connected fluid samples inside the reachable Pull region for the next simulation step, including particles that have already gathered. Reapply while held; release restores normal gravity. Solids and walls remain unaffected.

`material_step` runs the fixed kernel. Callers own the tick; the function reads `q.tick` but does not increment it. The caller should update `q.tick` before stepping if tick cadence matters. The step reads:

- `blocked`: optional 1-based sample table
- `environment`: optional table with `x1`, `x2`, `y1`, `y2`, `temp`
- `grate`: optional table with `y`, `x1`, `x2`
- `q.held`: optional 1-based samples reserved from the previous pull
- `q.reactions`: table updated with discovered reaction ids

After stepping, the function writes `q.static` from the grid sleeping state, resets `q.held` to an empty table, updates `q.reactions`, and returns the number of changes.

`material_effects` returns up to 24 flame/effect positions for burning oil/wood/charcoal. Positions are 1-based scaled coordinates and duplicate scaled cells are collapsed.

## Active chunks

The grid tracks 16x16 sample chunks. Writes wake the touched chunk and its neighbors, mark the touched chunk dirty for rendering, increment the grid revision, and clear sleeping state.

During simulation, unchanged chunks are allowed to quiet down only after a full 12-tick rule cycle. That cycle covers odd/even movement, porous grain periods and environmental cooling. Chunks with reactive thermal state stay active, and any environment table wakes the whole grid. This is a performance optimization around the fixed rules; it is not a change to authored material behavior.

## Dynamic surfaces

`engine.material_surface(q, entity_id, palette)` renders a grid into a dynamic 2x-pixel-per-sample surface attached to an existing entity. The surface is presentation-only and rebuilt from gameplay data.

The palette must provide entries for material ids `1..26`; each entry is a table with:

```lua
palette[10] = { asset = "matter", clip = "water" }
```

The asset must be a compiled sprite asset, and the clip must resolve to a sprite frame. The renderer samples the sprite frame according to `scale`, darkens very hot samples, makes gases and glass partly transparent, and tints burning fuel.

Dynamic material surfaces are limited to 16 surfaces and 16 MiB of pixel storage per world. A surface updates only when its dimensions or material revision require it; otherwise the call returns without work. Dirty 16x16 chunk revision stamps let each surface independently update only changed CPU pixel regions. Changed textures currently upload their whole pixel buffer; unchanged texture revisions skip upload. Surface revisions remain distinct across grid replacement and world restoration.

Surface pixels are not saved as authoritative state. Save the gameplay grid through ordinary state, usually a packed string plus metadata, and call `material_surface` again from startup/load/update to rebuild presentation.

## Bound state providers

`engine.state_bind(key, provider)` binds a state key to a provider function. `engine.state_bind(key)` unbinds it.

Bound providers are evaluated when the engine observes persistent state: snapshots, saves, and fresh `engine.state_get(key)` calls for the bound key. Ordinary updates do not evaluate a provider unless script asks for the key. This allows large native data, such as packed material grids, to remain native during normal frames and enter JSON state only at observation or save time.

Example:

```lua
local q

return {
  start = function()
    q = engine.material_new(72, 44, 1)
    q.tick = 0
    q.reactions = {}

    engine.state_bind("matter", function()
      return {
        width = q.width,
        height = q.height,
        scale = q.scale,
        tick = q.tick,
        cells = engine.material_pack(q),
        reactions = q.reactions,
      }
    end)
  end,

  update = function()
    q.tick = q.tick + 1
    engine.material_step(q)
  end,
}
```

Provider return values use the bound-state limits: the combined bound game state may be up to 8 MiB, and provider JSON conversion allows up to 1,048,576 values. Saved-state validation also accepts saved game state up to 8 MiB inside the overall 16 MiB player save. Ordinary `engine.state_set` remains limited to 1 MiB for the live game state and 65,536 values for one JSON conversion.

Errors are transactional at the flush boundary: providers build a candidate state and commit it only after all providers succeed and the 8 MiB bound-state limit passes. If a provider errors, the previous game state remains live.

Providers run inside the script VM and should be authored as side-effect-free observations. The runtime prevents some recursive cases, but it does not prove provider purity. Avoid changing gameplay state, binding providers, or depending on provider call count.

`state_set` cannot replace a currently bound key; unbind first if you want ordinary state ownership again. Rebinding the same key replaces the previous provider only after the new provider argument has been validated.

On load, bindings are script-owned and must be recreated. Restored JSON state can be exact, but native material grids and surfaces are not resurrected automatically. Use `on_load` to read the saved packed state, create a fresh grid, unpack the cells, restore metadata such as `tick`, `reactions` and `held` if needed, rebind providers, and rebuild dynamic surfaces on the next presentation update.

```lua
on_load = function()
  local saved = engine.state_get("matter")
  q = engine.material_new(saved.width, saved.height, saved.scale or 1)
  q.tick = saved.tick or 0
  q.reactions = saved.reactions or {}
  engine.material_unpack(q, saved.cells, saved.width, saved.height)
  engine.state_bind("matter", function()
    return {
      width = q.width,
      height = q.height,
      scale = q.scale,
      tick = q.tick,
      cells = engine.material_pack(q),
      reactions = q.reactions,
    }
  end)
end
```

## Limitations

The material grid is a native bounded board with a specific classic chemistry kernel. It supports fast storage, packing, pulling, line checks, fixed reactions, chunk sleeping and dynamic surface rendering. It does not currently expose project-defined material rules, arbitrary reaction graphs, arbitrary per-cell channels, or automatic save restoration of native buffers and dynamic surfaces.
