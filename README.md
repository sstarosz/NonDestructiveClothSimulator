# Non-Destructive Cloth CAD 0.1

A pattern CAD for clothing in which nothing is ever baked in. Every panel is
a short list of operations (rectangle, curve, added point, cut, mirror ...) whose values
can be numbers or formulas over named parameters. Change the waist and every
panel, copy, instance and seam that depends on it follows, and the cloth on
the body follows too.

## What 0.1 does

- **Patterns in 2D**: rectangles, circles, rings, points added on any edge
  (the Point tool) to shape a shoulder, a waist or an armhole, free curves, internal
  lines, cuts and splits, mirrors that keep both halves in step, instances
  and mirrored instances, moving single corners or groups of them, and
  scaling a panel along its own x and y (a Scale layer, made by the scale
  gizmo).
- **Parameters and formulas**: `waist / 4 + ease`, with limits per field;
  an edit that would break something is refused and says why.
- **Timeline**: every step of a panel can be changed, switched off, or looked
  back at.
- **Seams** between named edges, which follow the edges through every edit.
- **3D**: panels arranged around a body (each sample opens on the avatar in
  `assets/`; Body > Avatar from OBJ… loads another),
  meshed and simulated: gravity, seams drawing closed,
  cloth against body, floor and cloth.
- **Gizmos as in Maya**, in the 3D view and on the table alike: Q selects
  only, W moves (an arrow per axis, a square per plane, the middle across
  the view), E turns (a ring per axis, one about the line of sight, and
  between them a ball that turns it freely, as Maya's trackball; on the
  table one ring), R scales along the panel's own x and y, or evenly by
  the square between them or the middle; its z is grey, since a panel has no
  thickness. In 3D they are drawn in flat colours, in front of everything.
  Ctrl: whole centimetres, steps of 15°, steps of 10 %.
  Moving and turning only place a panel; scaling changes its shape, as a
  Scale layer that can be edited, switched off or undone like any other.
- **Editing on the table**, and in 3D on the selected panel: Delete removes
  the selected panel, or, with corners selected, the ones added with the
  Point tool; Ctrl+C remembers a panel, Ctrl+V pastes a copy with its middle
  under the pointer (a copy follows the same parameters), Ctrl+D duplicates
  it beside the original. Each is one undo step. The table's origin is drawn
  with its grid: the x axis in red, the y axis in green. The keys work while
  the table (or the 3D view) has the focus, so typing in a field or in the
  Outliner is left alone.
- **Selecting corners**: with a panel chosen, a box dragged on empty table
  selects the corners of that panel inside it (Shift adds them to the ones
  already selected; Shift-click on a corner adds or takes it out); a click on
  empty table chooses nothing.
- **A corner along one axis**: Shift held while a corner (or the selected
  corners) is dragged keeps it on the table's x or y, whichever the pointer
  moved furthest along first; a dashed line in that axis's colour shows it.
  The other coordinate does not change at all. With Ctrl as well, the free
  one is put on the grid.
- **Live link**: edit the pattern while the cloth is draped and it stays
  draped.
- **Files**: save and open (`.clothcad`, plain text), undo.
- **The window** is laid out as Maya is: a row of actions under the menu
  (undo, redo, play and pause, reset, cloth mesh), a shelf of tools to make
  things (Draft, Sew, Simulate), a toolbox at the left edge (Select, Move,
  Rotate, Scale, and the view layouts), the 3D
  view and the table side by side (or stacked, or either one alone), and dockable panels
  that fold and stack as tabs (tabs on top): the Outliner (which also shows
  or hides the body, with its eye) and Parameters on the left, the Attribute
  Editor on the right (a panel's operations as layers, and how fine its cloth is).
  Its 3D view is drawn by the graphics card, with Vulkan.

## Building

Needs CMake 3.25 or newer, a C++23 compiler (Visual Studio 2022 17.10 or
newer, GCC 12, Clang 17) and vcpkg; the libraries in `vcpkg.json` (Catch2,
CDT) are built by vcpkg the first time. The window also needs Qt 6 (6.12 on
Windows: the presets point `CMAKE_PREFIX_PATH` at it) and the Vulkan SDK
(its headers, with Vulkan-Hpp, and `glslangValidator` for the shaders);
CMake finds the SDK through the `VULKAN_SDK` variable its installer sets.
Without them, everything but the window is built. The program is
`clothcad`; on Windows Qt's libraries are copied next to it when it is built,
and the avatars (the OBJ files in `assets/`) go in an `assets/` folder beside
it, where the window looks for them.

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
window, for machines without a display. A warning stops the build;
`-DCLOTHCAD_WARNINGS_AS_ERRORS=OFF` lets it through, only for trying
something out with another compiler.

A package to hand out, with the program, Qt's libraries, the compiler's
runtime and the avatars in `<folder>/bin`:

    cmake --install out/build/x64-win-msvc2026 --config Release --prefix <folder>

### Continuous integration

Every pull request and every push to `main` is built and tested by GitHub
Actions (`.github/workflows/ci.yml`): Windows, MSVC, Release, Qt 6.12 and
the Vulkan SDK, then the unit and integration tests.
To run the same tests locally:

    ctest --preset x64-win-msvc2026-ci

The runner has no graphics card, so the renderer's tests are skipped there,
and the benchmarks are not run there (shared machines are too noisy for
their limits): run them on your own machine when `sim`, `session` or the
drawing changed.

## Performance

What a frame and a cloth step may cost is checked by two benchmark programs,
`clothcad_core_and_sim_benchmarks` (core, meshing, cloth) and
`clothcad_qt_window_benchmarks` (whole frames of the window, 1600 x 900,
without a screen: there the 3D view is drawn off screen and read back, which
costs more than showing it). Each measurement is held to a limit in
milliseconds and to its own baseline (`tests/benchmarks/baselines/`): a test fails
if it gets more than 1.4 times slower than the baseline. Times are measured
against a fixed piece of arithmetic first, so a baseline taken on one machine
still means something on another. In a build that is not optimised the
benchmarks do nothing and pass.

After a change that makes something faster, or a deliberate trade, write new
baselines from an optimised build and commit them with the change:

    clothcad_core_and_sim_benchmarks tests/benchmarks/baselines/CoreAndSim.txt --update
    clothcad_qt_window_benchmarks    tests/benchmarks/baselines/QtWindow.txt --update

The budgets, and the times measured. This first table is from a Linux machine
(GCC 12, Vulkan in software, no screen); the window's baselines in
`QtWindow.txt` were written later on Windows 11 with MSVC, also without a screen:

| What | Limit | Now |
| --- | --- | --- |
| A frame with nothing changing, both views shown | 1 ms | 0.1 ms |
| A frame while dragging a panel on the table | 8 ms | 4 ms |
| A frame while dragging a corner, 3D shown, 1 cm particles | 16 ms | 10 ms |
| The 3D view orbited (drawn off screen and read back) | 60 ms | 4 ms |
| A frame while the cloth runs on its own thread, 3D shown | 16 ms | 4 ms |
| A frame with a cloth step in it, 3D shown | 40 ms | 30 ms |
| One cloth step, 2,800 particles, on a body | 16 ms | 11-15 ms |
| Meshing a skirt at 2 cm | 25 ms | 8 ms |

What working at 4K and on a T-shirt costs, per frame, mean of 40 frames spaced as a 60 Hz screen
would (Windows 11, MSVC, no screen, an idle machine; the 3D view's picture is
read back here, which the real window does not do):

| What | Limit | Now |
| --- | --- | --- |
| The table paints itself, 3840 x 2160 | 8 ms | 3-4 ms |
| Dragging a panel on the table at 4K | 16 ms | 4-6 ms |
| Dragging a corner at 4K, 3D shown | 16 ms | 4-5 ms |
| Dragging a corner at 4K, table only | 16 ms | 7 ms |
| Zooming the table at 4K (its floor is drawn again every frame) | 16 ms | 12 ms |
| Dragging a corner at 4K, a panel filling the table | 16 ms | 9 ms |
| Carrying a T-shirt panel in 3D with the gizmo | 24 ms | 7 ms |
| Dragging a length parameter on the T-shirt | 16 ms | 8-9 ms |

A drag also reports its slowest 1 frame in 20 (held to twice the limit) and its
worst frame, because what a drag feels like is its slow frames. Not measured
yet: the real window (#56).

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
- **The 3D view is drawn by the graphics card** (`libs/render`, Vulkan), and
  its scene is made again only when what it shows has changed.
- **The table keeps its floor.** The background and grid are drawn once into
  a picture and copied until the view moves (`TableView::paintFloor`): at 4K
  the grid's lines cost more than everything else on the table together. The
  mesh's thousands of lines go in one batch, without smoothing; only outlines
  and labels are smoothed, and a long thick outline is drawn side by side
  with square caps, because Qt strokes it as a polygon or with round caps
  twenty times slower.
- **The docks are set in place.** After every change, each step of a drag
  included, the window shows the document again. A seam's card, a layer's
  summary, an Outliner row and a field are made or set again only when what
  they say changed, not when only a number moved.

`-DCLOTHCAD_WINDOW=OFF` builds everything but the window program;
`libs/core` alone has no dependencies.

## A first garment

1. Rectangle tool: draw two panels of about 46 x 55 cm.
2. Sew tool: click the right edge of one and the left edge of the other;
   then the two remaining side edges.
3. Select the first panel. In the Attribute Editor, open "In space" and set
   Position to 0, 95, 24 and Wrap to 110. For the second: Position 0, 95, -24,
   Rotation 0, 180, 0 (the middle number turns it about y), Wrap 110.
4. Press Play in the action row (or Space; Space again pauses).
5. Change a width while it hangs.

## How the code is laid out

| Folder | What is in it | Depends on |
| --- | --- | --- |
| `libs/core/` | The document (parameters, panels, operations, seams), formulas, evaluation to 2D outlines, files | nothing |
| `libs/sim/` | Meshing, arrangement in space, bodies, the cloth solver | `core`, CDT |
| `libs/session/` | A document being edited, with undo and redo, and everything worked out from it; the cloth on its own thread; what a gesture on the table or in 3D changes (`Drafting`), and the tool in use | `core`, `sim` |
| `libs/render/` | The 3D view's drawing with Vulkan, through Vulkan-Hpp: `Scene` (plain data), `OrbitCamera`, `Renderer` (into a picture or a window), `sceneOf` a session, `Picking` and the move, rotate and scale `Gizmo`s. Built when the Vulkan SDK is found | `session`, Vulkan headers |
| `apps/qt/` | The window (`clothcad`), laid out as Maya is: shelf, dockable Outliner, Parameters, Attribute Editor (operations as layers), 3D view and table. Built when Qt 6 and the Vulkan SDK are found | `session`, `render`, Qt 6 |
| `examples/` | Six small garments as programs, the sixth a T-shirt body | `core` |
| `tests/` | Integration tests and benchmarks; each library's unit tests are in its own `tests/` and read and write no files (bodies are made in memory); tests on the project's avatar are in `tests/integration/AvatarTests.cpp` | everything |

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
- The avatar must be one closed surface in cm, y up, facing +z.
- Seams join whole edges, one to one.
- Delete on a corner works only for corners added with the Point tool; any
  other corner is part of a shape, and the help line names the layer to
  change or remove. Copy and paste work on one panel at a time (several need
  selecting more than one). Deleting a panel that an instance or a seam uses
  is allowed: they show a problem until it is put right, and undo brings the
  panel back. In 3D, Delete takes the whole panel and Paste has no place on
  the table, so the copy goes beside the original.
- A panel grows away from its sewn edges that kept their length. If every
  sewn edge changed (only side seams, and the panel got longer) it grows about
  the middle of those edges; a turn of the outline itself is not followed.
- While seams are pulled shut the cloth slides on the body without friction.
  Seams that cannot close (the pieces do not fit, or cloth is caught on an
  arm) are given up 3 seconds after the thread is drawn in, and stay open.
- Where three or more panel corners are sewn into one point (a band's side
  seam meeting a skirt), the stitches there pull against each other and that
  corner stays up to about 0.8 cm open. Pulling such a corner shut at once
  was tried and taken back: it held a band that a seam had pulled through
  the body in that wrong place, instead of letting it slide free.
- While a corner is dragged, the 3D view shows the mesh a frame or a few
  behind the outline (more with fine particles).
- A new panel stands where the table has it only within ±100 cm across and
  0-200 cm up around the body; further out it wraps round, a step further
  forward each time, and may then stand in front of another panel.
- Pulling the cloth holds one particle; the cloth runs while it is held and
  stops again when it is let go, if it was paused.
- Moving a draped panel's place carries its drape rigidly; a change of wrap
  is not applied to cloth that is already draped (press Reset for that).
- Undo and redo bring back the drape of the last 64 states.
- The move and rotate gizmos work along the axes of space (or of the table),
  not the panel's own; only the scale gizmo follows the panel's own axes.
  In 3D the gizmo stands at the panel's anchor, which can be away from where
  draped cloth hangs. The scale gizmo's z handle does nothing; seen from the
  front it points at the eye and is too short to show.
- A Scale layer scales about the middle of the outline's box as it is at
  that layer. On a symmetric panel it scales the drawn half, before the
  mirror: the whole stays symmetric, but its middle moves on the table. An
  arc stretched more one way than the other stays a circular arc between its
  moved ends, and a whole circle can only be scaled evenly. A Scale set by a
  formula is not dragged (the gizmo says so); type it in the Attribute
  Editor.
- Q, W, E and R replace the Rectangle tool's old key R; Rectangle is chosen
  from the shelf.
- A point added on a sewn edge splits it: the seam then sews only the part
  before the point, and the part after it must be sewn on its own.
- The body is always an avatar read from an OBJ file. Without the avatar
  file beside the program there is no body, and the cloth falls to the floor.
- The avatar's armpits pinch a top without sleeves (sample 6) at its
  underarm points: a few edges there stretch up to about 30 %.
- The 3D view draws without smoothing of edges (no multisampling) and one
  picture at a time (the processor waits for each before the next). Drawing
  into a window is checked only by hand: the tests draw off screen.

See `ROADMAP.md` for what comes next.
