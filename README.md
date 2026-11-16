# Non-Destructive Cloth CAD 0.1

A pattern CAD for clothing in which nothing is ever baked in. Every panel is
a short list of operations (rectangle, curve, cut, mirror ...) whose values
can be numbers or formulas over named parameters. Change the waist and every
panel, copy, instance and seam that depends on it follows, and the cloth on
the body follows too.

## What 0.1 does

- **Patterns in 2D**: rectangles, circles, rings, free curves, internal
  lines, cuts and splits, mirrors that keep both halves in step, instances
  and mirrored instances, moving single corners or groups of them.
- **Parameters and formulas**: `waist / 4 + ease`, with limits per field;
  an edit that would break something is refused and says why.
- **Timeline**: every step of a panel can be changed, switched off, or looked
  back at.
- **Seams** between named edges, which follow the edges through every edit.
- **3D**: panels arranged around a body (the avatar in `assets/`, or a
  built-in mannequin), meshed and simulated: gravity, seams drawing closed,
  cloth against body, floor and cloth.
  The selected panel has a move gizmo in the 3D view: drag an arrow to move
  it along x, y or z only, or the centre square to move it across the view
  (Ctrl: whole centimetres).
- **Live link**: edit the pattern while the cloth is draped and it stays
  draped.
- **Files**: save and open (`.clothcad`, plain text), undo.

## Building

Needs CMake 3.25 or newer, a C++23 compiler (Visual Studio 2022 17.10 or
newer, GCC 12, Clang 17) and vcpkg; the libraries in `vcpkg.json` (Catch2,
CDT, Dear ImGui, GLFW) are built by vcpkg the first time.

**Use a Release (or RelWithDebInfo) build to work in.** A Debug build runs
the cloth five to ten times slower, so it moves in slow motion; the interface
stays responsive either way. With the Visual Studio presets (they expect the
environment variable `VCPKG_ROOT`, which Visual Studio's developer prompt
sets):

    cmake --preset x64-win-msvc2026
    cmake --build --preset x64-win-msvc2026-release
    ctest --preset x64-win-msvc2026-release        # everything
    ctest --preset x64-win-msvc2026-unit           # unit tests only
    ctest --preset x64-win-msvc2026-integration    # integration tests only
    ctest --preset x64-win-msvc2026-benchmarks     # benchmarks, with their tables

Without the presets: `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.
`-DCLOTHCAD_WINDOW=OFF` builds everything but the program that opens a
window, for machines without a display.

## Performance

What a frame and a cloth step may cost is checked by two benchmark programs,
`clothcad_core_and_sim_benchmarks` (core, meshing, cloth) and `clothcad_window_benchmarks` (whole interface frames at
3840 x 2160, without a window). Each measurement is held to a limit in
milliseconds and to its own baseline (`tests/benchmarks/baselines/`): a test fails
if it gets more than 1.4 times slower than the baseline. Times are measured
against a fixed piece of arithmetic first, so a baseline taken on one machine
still means something on another. In a build that is not optimised the
benchmarks do nothing and pass.

After a change that makes something faster, or a deliberate trade, write new
baselines from an optimised build and commit them with the change:

    clothcad_core_and_sim_benchmarks tests/benchmarks/baselines/CoreAndSim.txt --update
    clothcad_window_benchmarks       tests/benchmarks/baselines/Window.txt --update

The budgets, on the machine the baselines were taken on:

| What | Limit | Now |
| --- | --- | --- |
| A frame with nothing changing, 3D view shown | 1 ms | 0.1 ms |
| A frame while dragging a panel on the table | 2 ms | 0.1 ms |
| A frame while dragging a corner, 3D shown, 1 cm particles | 12 ms | 4 ms (64 ms when the frame made the mesh) |
| A frame while orbiting (the 3D picture redrawn) | 12 ms | 4 ms |
| A frame while the cloth runs | 12 ms | 4 ms |
| One cloth step, 2,800 particles, on a body | 16 ms | 11-15 ms |
| Meshing a skirt at 2 cm | 25 ms | 8 ms |

How it is kept fast:

- **Nothing is worked out again unless it changed.** The mesh is made only
  when the outlines change; the 3D picture is drawn only when the camera, the
  view's size or what it shows has changed. An unchanged frame costs the same
  with or without the 3D view.
- **The cloth runs on a thread of its own** (`libs/session` `Simulation`), so the
  interface never waits for a step; it picks up where the particles are
  whenever there is news.
- **The mesh is made in the background** (`Session`). While a corner is
  dragged the 3D view shows the last mesh made and takes the next one when it
  is done, always made from the newest outlines; a frame never waits for it.
- **The 3D picture is drawn on several threads** (`Picture`), in bands.

`-DCLOTHCAD_WINDOW=OFF` builds everything but the window program;
`libs/core` alone has no dependencies.

## A first garment

1. Rectangle tool: draw two panels of about 46 x 55 cm.
2. Sew tool: click the right edge of one and the left edge of the other;
   then the two remaining side edges.
3. Select the first panel. Under "In space" set Across 0, Height 95,
   Forward 24, Wrap 110. For the second: Forward -24, Turn 180, Wrap 110.
4. Press Simulate above the 3D view.
5. Change a width while it hangs.

## How the code is laid out

| Folder | What is in it | Depends on |
| --- | --- | --- |
| `libs/core/` | The document (parameters, panels, operations, seams), formulas, evaluation to 2D outlines, files | nothing |
| `libs/sim/` | Meshing, arrangement in space, bodies, the cloth solver | `core`, CDT |
| `libs/session/` | A document being edited, with undo and redo, and everything worked out from it; the cloth on its own thread | `core`, `sim` |
| `apps/imgui/` | The window of 0.1: 2D table, 3D view, details. To be replaced by a Qt window | `session`, Dear ImGui, GLFW |
| `examples/` | Five small garments as programs | `core` |
| `tests/` | Integration tests and benchmarks; each library's unit tests are in its own `tests/` | everything |

The core never draws and the window never computes: `evaluate(doc)` turns a
document into outlines, `sim::meshOf` turns outlines into triangles,
`sim::Cloth` moves them, and `session::Session` keeps all of that in step
with the document as it is edited. A window only shows what the session
holds and hands it what the user did.

## Known limits of 0.1

- Cloth keeps clear of cloth by keeping particles apart, so layers rest
  about 1 cm apart, and cloth pulled open or moving very fast can slip
  through.
- One fabric for everything, with fixed properties; no pins in the window.
- The 3D picture is worked out on the processor, without smoothing of edges;
  orbiting a large view costs about 4 ms a frame.
- The avatar must be one closed surface in cm, y up, facing +z.
- No file dialog: files are named by typing a path.
- Seams join whole edges, one to one.
- While seams are pulled shut the cloth slides on the body without friction.
  Seams that cannot close (the pieces do not fit, or cloth is caught on an
  arm) are given up 3 seconds after the thread is drawn in, and stay open.
- While a corner is dragged, the 3D view shows the mesh a frame or a few
  behind the outline (more with fine particles).

See `ROADMAP.md` for what comes next.
