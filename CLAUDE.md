# Instructions for Claude

This is Non-Destructive Cloth CAD: a parametric pattern CAD for clothing in
C++23, with a 2D table, a 3D view and a cloth simulation. Read `README.md`
for what it does and `ROADMAP.md` for what is planned. 

## What the owner cares about

These came up again and again and outrank convenience:

1. **Non-destructive.** Nothing is baked in. Every result is recomputed from
   the document; an edit to an early step must flow through everything after
   it (later operations, copies, instances, seams, the draped cloth).
2. **Strong types, no string lookups.** Things are named by typed handles
   (`Param`, `PanelId`, `OpId`, `SeamId`, `EdgeRef`, `CornerRef`, `LineRef`,
   `OpRef<T>`), never by text or bare indices. Strings are for people to read.
3. **Explicit types and a small, clean API.** Few verbs, each doing one
   thing. Hide implementation in a library's `src/` (never under
   `include/`) or in the `.cpp`.
4. **Code that reads like a DSL.** A plain struct describes a thing, a verb
   takes it and returns a handle:
   `OpRef<Rect> rect = doc.add(panel, Rect{.width = waist / 2, .height = 4_cm});`
5. **Easy to test.** Everything that matters works without a window.
6. **Viewport-driven interface.** Work happens on the table and in the 3D
   view with a small toolbar; the details pane only shows and fine-tunes.
7. **Small steps.** One contained piece at a time, with its limits written
   down, not a large rewrite.

## Layout and what may depend on what

| Folder | Holds | May use |
| --- | --- | --- |
| `libs/core/` | Document, formulas, operations, evaluation to 2D outlines, files | the standard library only |
| `libs/sim/` | Meshing, arrangement in space, bodies, the cloth solver | `core`, CDT |
| `libs/session/` | One document being edited and all worked out from it: `Editor` (changes, refusals, undo, redo), `Simulation` (the cloth's thread), `Session` (mesh, arrangement, body, cloth kept in step) | `core`, `sim` |
| `apps/imgui/` | The window of 0.1 (`App`, `Canvas`, `View3D`, `Picture`), until the Qt window replaces it | `session`, Dear ImGui, GLFW |
| `examples/` | Five garments as small programs; they also run as tests | `core` |
| `tests/integration/` | Tests across libraries and of whole programs | anything |
| `tests/benchmarks/` | How long things take, against baselines | anything |

Each library has `include/clothcad/<library>/` (its public headers, the only
ones other code may include), `src/` (sources and private headers) and
`tests/` (one test file per source file: `src/Editor.cpp` is tested by
`tests/EditorTests.cpp`). CMake makes it with `clothcad_library()` from
`cmake/ClothCad.cmake`, as `clothcad::<library>`.

Never make `core` depend on anything, never put drawing in `core`, `sim` or
`session`, and never put computing in a window: a window shows what the
`Session` holds and hands it what the user did.

## Rules that must stay true

- **The document is a value.** Copying it is an undo snapshot. Every change
  goes through a verb of `Document`; reading gives `const` access.
- **`evaluate`, `sim::meshOf`, `sim::Cloth` never throw.** Problems are
  reported where they are (`Result::errors`, `SeamResult::problem`,
  `PanelMesh::problem`). Document verbs throw only for an unknown handle.
- **`core` recomputes from scratch.** No caches, dirty flags or threads in
  `core`. `session` keeps results and works them out again only when their
  inputs change (version numbers, see Performance).
- **Fields have a contract.** Every formula field is listed in
  `core/Fields.cpp` with its `Limits`; evaluation reads values through that
  table. A new field without limits is a bug.
- **Every change of the document goes through `session::Editor::change`.**
  It refuses a change that adds a problem (unless told `IfItBreaks::Allow`)
  and says why; the window shows that. One drag is one undo step
  (`Step::New`, then `Step::Continue`). Moving panels is
  `Touches::PlacementOnly`, so nothing is evaluated again.
- **Ownership is plain.** Results shared beyond one call are immutable
  snapshots in a `std::shared_ptr<const T>` (the mesh, the outlines). A
  pointer or reference handed to a function is for that call only; no class
  keeps a raw pointer to something another class owns.
- **What a formula drives is locked** in the viewport (`apps/imgui/Locks`).
- **Placement is not shape.** A panel's place on the table, its angle and
  its `Arrangement` in space never change its outline.
- **The simulation is deterministic**, in cm, seconds and grams.
- Units: lengths in cm, angles in degrees, with the literals `_cm`, `_mm`,
  `_deg`. Outlines run counter-clockwise. In space y is up, the floor is
  y = 0 and the body faces +z.

## Style

- Names are plain words for what a thing is (`placedNow`, `carriedFrom_`,
  `nearness_`), not abbreviations or pattern names. Members end in `_`.
- Comments say why, or what a reader cannot see from the code. Each header
  opens with a few lines on what it is for and a short usage example.
- Messages to the user are whole sentences that say what to do next.
- No macros (the tests' `EXPECT`, `EXPECT_NEAR`, `EXPECT_THROWS` are the
  only ones), no global state, no inheritance where a `std::variant` or a
  table will do. Errors a caller must handle come back as
  `std::expected<T, std::string>`. Prefer `std::optional` and small structs to out-parameters.
- Match the code around you before inventing a new way.

## Adding an operation: every place it must appear

1. `libs/core/include/clothcad/core/Operations.h`: the struct, its `Names` (roles as enums), and the
   `OpData` variant.
2. `libs/core/src/Fields.cpp`: `describe` (label, fields, limits),
   `describeReferences`, and `dimensions` if a field is an edge's length.
3. `libs/core/src/References.cpp`: remapping when a panel is copied.
4. `libs/core/src/Evaluate.cpp`: an `apply` overload.
5. `libs/core/src/File.cpp`: its name in `typeNames` (same order as `OpData`, never
   renamed once released) and `extras` if it holds more than formulas and
   references.
6. `libs/core/tests/EvaluateTests.cpp` (what it does, how it fails),
   `ReferencesTests.cpp` (it survives a copy) and `FileTests.cpp` (it
   survives save and open).
7. If it has a tool: a `Gesture` in `apps/imgui/Tools.h`, reported by `Canvas`,
   handled in `App::on(...)`, with a hint and a toolbar entry.

## Working in the window

- `Canvas` and `View3D` only report what the user did (gestures, events).
  `App` decides what that changes in the document.
- On a symmetric panel (mirror applied last), actions on the mirrored side
  are turned into actions on the drawn side (`drawnSide`, `acrossTheMirror`).
- Colours with a meaning live in `apps/imgui/Style.h`: teal = driven by a formula,
  violet = instance, pink = symmetry, blue = seam, amber = selected.
- Sizes scale with `pixel()`; the owner uses a 4K display.

## Performance

The owner wants the interface and the simulation as fast as they can be;
the budgets are in `README.md` ("Performance").

- **Never work something out every frame that has not changed.** `Session`
  makes the mesh again only when `Editor::shapesVersion()` moves, and a 3D
  view redraws only when `Session::sceneVersion()` or its camera changed.
  Anything new that a 3D view shows must bump `sceneVersion_` when it changes.
- **The interface never waits for the cloth.** It runs in
  `session::Simulation` on its own thread; nothing else touches it.
- **Hot loops are written for speed:** no allocation per step or per pixel,
  flat arrays, what does not change within a step worked out once before it.
- **Run the benchmarks** (CTest label `benchmark`, optimised build) after
  any change to `sim`, `session` or the drawing. Write new baselines
  (`--update`) only when the owner agrees, and say which numbers moved and
  why; never to make a failure go away.

## Testing and checking

- Tests use Catch2 and read as sentences:
  `EXPECT(r.ok(), "cutting is not an error");` (`tests/support/Expect.h`).
  Unit tests sit beside each library, one file per source file; tests that
  go across libraries are in `tests/integration/`; benchmarks in
  `tests/benchmarks/`. CTest labels: `unit`, `integration`, `benchmark`.
- Every feature and every fix arrives with a test that fails without it.
- Dependencies come from vcpkg (`vcpkg.json`); Qt from the owner's install
  (`C:/Qt/6.12.0/msvc2022_64`). Build and run:

      cmake --preset x64-win-msvc2026
      cmake --build --preset x64-win-msvc2026-release
      ctest --preset x64-win-msvc2026-release

- The owner builds with MSVC on Windows. If you verify with another
  compiler, or cannot run the window, say exactly that. Never report
  something as working that you have not run or seen; name the part you
  could not check.
- Treat warnings as defects (`clothcad_warnings()`: `/W4`, or `-Wall -Wextra
  -Wpedantic -Wshadow`).

## How to work with the owner

- Start on a clear request without asking for permission; ask only when a
  wrong guess would be expensive. When a requirement is unclear and you had
  to choose, say what you chose and why.
- Report briefly: what changed, how to use it, what is limited, what was
  and was not verified. Plain words; English is not the owner's first
  language.
- When something is a real limitation, write it down (README "Known
  limits", `ROADMAP.md`, the design doc) instead of hiding it.
- Keep `README.md` and `ROADMAP.md` current when a roadmap step lands or a
  limit goes away.
- Do not leave stray files in the project, do not delete the owner's files,
  and do not add dependencies without saying so first. They go in
  `vcpkg.json` and are found with `find_package`.
- If a request conflicts with the rules above (a string lookup, a cache in
  `core`, computing in `app`), say so and offer the way that keeps them.
