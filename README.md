# Non-Destructive Cloth CAD 0.1

A pattern CAD for clothing in which nothing is ever baked in. Every panel is
a short list of operations (rectangle, curve, added point, set length, cut, mirror ...) whose values
can be numbers or formulas over named parameters. Change the waist and every
panel, copy, instance and seam that depends on it follows, and the cloth on
the body follows too.

## What 0.1 does

- **Patterns in 2D**: rectangles, circles, rings, points added on any edge
  (the Point tool) to shape a shoulder, a waist or an armhole, free curves, internal
  lines, cuts and splits, mirrors that keep both halves in step, instances
  and mirrored instances, moving single corners or groups of them, and
  scaling a panel along its own x and y (the scale gizmo changes the lengths
  of its rectangle, circle or ring, or a Scale layer), and giving any one
  edge its own length (the Set length layer): a trousers' waist and ankle,
  which a rectangle's top and bottom cannot say, or a T-shirt's shoulder.
  The Length tool (Draft tab): click an edge, then type its length in a bar
  on the table. A double-click on an edge's length label opens the same bar;
  on an edge nothing sets it adds a Set length layer as long as the edge is
  now. What is typed shows at once, with the outline as it was dashed beside
  it and an arrow for each end that moved. Enter (or the tick) keeps it,
  Escape (the cross, or a click elsewhere) takes all of it back, even a
  layer that was made for it; a length that cannot be is said under the field
  and Enter waits. Where the shape has a setting for the edge (a rectangle's
  width or height) a list in the bar says which is typed: "Edge only", a
  length of its own, so the top and bottom of a rectangle can differ, or the
  shape's setting, where the opposite edge follows (the tool starts on "Edge
  only", a double-click on the shape's setting). For an own length three
  anchors show on the edge, "Left", "Midpoint" and "Right" (Top and Bottom
  for an edge that runs up; in a row beside it where the edge is short on the
  screen): a click on one says which end stays where it is, and the end
  that stays is marked on the edge.
- **Parameters and formulas**: `waist / 4 + ease`, with limits per field;
  an edit that would break something is refused and says why.
- **Timeline**: every step of a panel can be changed, switched off, or looked
  back at.
- **Seams** between named edges, which follow the edges through every edit.
- **Instances come sewn**: an instance (or mirrored instance) is sewn as its
  source is, in the same undo step: a seam within the source is made within
  the instance, and a seam to a neighbour is made once the neighbour has an
  instance of the same kind too, whichever of the two is made first. Select a
  front and a back sewn together and press "Mirrored instance": the other leg
  comes with its seams, laid out on the table as the mirror image of the two.
  The centre seams, between a panel and its own instance, you sew by hand.
  For seams made on a source after its instance, select the instances and
  press "Sew as source" (Sew tab). The copies are ordinary seams that you can
  reverse or delete. The Trousers sample (File > Samples) is made this way:
  a front and a back for one leg, the other leg a mirrored instance of both.
- **3D**: panels arranged around a body (each sample opens on the avatar in
  `assets/`; Body > Avatar from OBJ… loads another),
  meshed and simulated: gravity, seams drawing closed,
  cloth against body, floor and cloth.
- **Front and back**: a panel's front is the side the table shows, drawn in
  the panel's colour; its back is a darker, cooler shade of it (grey cloth,
  the amber selection and violet instances alike). A panel that has not been
  turned in space has its front towards +z, where the body faces.
- **Seams in 3D**: while a seam is open, both its edges are lined in blue and
  a thin line runs from each point to the point it is sewn to, so you see which
  end meets which: the lines of a twisted seam cross. Once the seam has closed
  its lines are gone. Choosing a seam's row in the Outliner shows it in amber,
  open or closed, in 3D, and lights it on the table. The eye on a seam's row
  hides its lines; the eye on "Seams" hides or shows all of them.
- **Hide and deactivate panels**: each panel's row in the Outliner has an eye,
  which hides it in the 3D view only (it is still simulated), and a button that
  deactivates it, as in Marvelous Designer: it stays where it is, seen through,
  nothing touches it and its seams hold nothing, until it is activated again.
  Neither changes the pattern.
- **Smooth across seams**: where a seam has closed, the cloth on both sides is
  shaded as one piece.
- **Gizmos as in Maya**, in the 3D view and on the table alike: Q selects
  only, W moves (an arrow per axis, a square per plane, the middle across
  the view), E turns (a ring per axis, one about the line of sight, and
  between them a ball that turns it freely, as Maya's trackball; on the
  table one ring), R scales along the panel's own x and y, or evenly by
  the square between them or the middle; its z is grey, since a panel has no
  thickness. In 3D they are drawn in flat colours, in front of everything.
  Ctrl: whole centimetres, steps of 15°, steps of 10 %.
  Moving and turning only place a panel; scaling changes its shape. A panel
  that starts with a rectangle, a circle or a ring of plain numbers is scaled
  by those numbers, with no layer added; if a formula sets a length, or the
  panel starts with an instance, the scale is a Scale layer, which can be
  edited, switched off or undone like any other. A Scale layer you add by hand
  stays yours: the gizmo changes it. Whichever way, the middle of the panel's
  box stays where it was on the table. Several selected panels scale together
  (evenly) about the middle of the group.
- **Editing on the table**, and in 3D on the selected panels: Delete removes
  the selected panels, or, with corners selected, the ones added with the
  Point tool; Ctrl+C remembers the panels, Ctrl+V pastes copies with their
  middle under the pointer, each in its place in the group (a copy follows the
  same parameters), Ctrl+D duplicates the selected panels just right of them.
  Each is one undo step. The table's origin is drawn
  with its grid: the x axis in red, the y axis in green. The keys work while
  the table (or the 3D view) has the focus, so typing in a field or in the
  Outliner is left alone.
- **Selecting several panels**: Shift- or Ctrl-click adds a panel to the
  selection or, on a selected one, takes it out; a box dragged on empty table
  selects the panels it touches (Shift or Ctrl adds them to the ones
  selected). Dragging one of the selected panels, or their move gizmo, moves
  them all, and the rotate gizmo turns them all about the middle of the group;
  each is one undo step. A click on one of them selects it alone. The last one
  added is the one the docks show; the others are drawn as selected, and the
  gizmo stands at the middle of the group.
- **Selecting corners**: with one panel chosen, a box dragged on empty table
  that touches only that panel selects its corners inside it (Shift adds them
  to the ones already selected; Shift-click on a corner adds or takes it out);
  a click on empty table, or a box over nothing, chooses nothing.
- **Along one axis**: Shift held while a corner (or the selected corners), a
  curve handle, or a panel (or the selected panels, by the panel or by the
  middle of the move gizmo) is dragged keeps it on the table's x or y,
  whichever the pointer moved furthest along first; a dashed line in that
  axis's colour shows it. The other coordinate does not change at all. With
  Ctrl as well, the free one is put on the grid.
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
still means something on another. That does not make up for another compiler:
code that one compiler makes faster than another is not measured fairly, so
baselines are written with MSVC, as the owner builds. In a build that is not
optimised the benchmarks do nothing and pass.

Run them on an idle machine, with the program closed: a cloth running in it
slows every row (on 26 November nine rows of the window's benchmark failed
while it ran, and none once it was closed). The benchmarks preset runs a
benchmark that fails up to three times, and it fails only if every run does:
a real slowdown fails each time, a moment of load on the machine rarely does.

After a change that makes something faster, or a deliberate trade, write new
baselines from an optimised build and commit them with the change:

    clothcad_core_and_sim_benchmarks tests/benchmarks/baselines/CoreAndSim.txt --update
    clothcad_qt_window_benchmarks    tests/benchmarks/baselines/QtWindow.txt --update

The budgets, and the times measured on 26 November 2026 (Windows 11, MSVC,
no screen, an idle machine):

| What | Limit | Now |
| --- | --- | --- |
| A frame with nothing changing, both views shown | 1 ms | 0.003 ms |
| A frame while dragging a panel on the table | 8 ms | 1.2-1.4 ms |
| A frame while dragging a corner, 3D shown, 1 cm particles | 16 ms | 4-6 ms |
| The 3D view orbited (drawn off screen and read back) | 60 ms | 5 ms |
| A frame while the cloth runs on its own thread, 3D shown | 16 ms | 2 ms |
| A frame with a cloth step in it, 3D shown | 40 ms | 30 ms |
| One cloth step, 2,800 particles, on a body | 16 ms | 10 ms |
| One cloth step, the trousers (2,900 particles), on the avatar | 16 ms | 12 ms |
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
worst frame, because what a drag feels like is its slow frames.

**In the real window**, View > Frame times shows the same in the status bar,
four times a second, over the last 60 of each: how far apart the 3D view's
and the table's pictures are (a gap of more than a quarter second is a pause,
not a frame), and how long a change of the document, the window's tick, the
table's painting and the 3D view's drawing take, then the cloth's latest step,
its particles and whether it runs, sews or is paused. These are the numbers to
read on the 4K screen while dragging; the benchmarks run without a screen and
read the 3D picture back, which the real window does not do.

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
| `examples/` | Seven small garments as programs, the sixth a T-shirt body, the seventh trousers | `core` |
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
  change or remove. Deleting a panel that an instance or a seam uses
  is allowed: they show a problem until it is put right, and undo brings the
  panel back. In 3D, Delete takes whole panels and Paste has no place on
  the table, so the copies go beside the originals.
- Several selected panels are selected, moved, turned, scaled evenly, copied,
  pasted, duplicated and deleted together, but the docks, corners and lengths
  work on one panel only: the shown one, or the single panel selected. They
  cannot be stretched one way together (the help line says so). The 3D view
  and the Outliner show the shown panel only. The instance buttons and "Sew as
  source" work on all the selected panels. A paste or Ctrl+D does not copy
  seams, and a copy or an instance of a turned panel is not turned.
- An instance's seams are copied when it is made, or by "Sew as source"; they
  do not follow later changes: a seam removed, reversed or moved on the source
  stays as it was on the instances. A plain and a mirrored instance are never
  sewn together this way. An instance's seam is found only towards instances
  of the panels its own source is sewn to (a back right that follows the front
  left, not the front right, is not found). Of several instances still free,
  the newest is taken. An edge of an instance that you have sewn by hand to
  something else gets the source's seam as well. Ticking "Mirrored" on one of
  two sewn instances later twists their seams (reverse them). A panel made an
  instance by adding the layer, or given another source, is not sewn by itself:
  press "Sew as source". An instance stands in space where the table has it,
  not mirrored from its source's place.
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
- Set length moves the two corners of an edge along the edge until it has
  the length; the edges beside it slant. It keeps both ends moving evenly, or
  the start or the end where it is (the anchors on the table, or the layer's
  "Keeps" list; on a half panel the table keeps the corner on the mirror
  itself). It works on straight edges and circular
  arcs (an arc keeps its angle); a free curve, a whole circle and a full
  ring's closing slit cannot be given a length, and the layer says so. Lines
  drawn before it keep their places, as they do for a moved corner. The ends
  of an edge whose length a formula sets cannot be dragged. A Set length
  added from the list starts at 20 cm, so choosing its edge makes the edge
  jump; the Length tool and a double-click start from the edge's length.
  On a rectangle, a double-click on its top types the width, which the
  bottom shares, until "Edge only" is chosen. Scaling a
  panel that has a Set length adds a Scale layer instead of changing the
  rectangle's numbers.
- A Scale layer scales about the middle of the outline's box as it is at
  that layer. On a symmetric panel it scales the drawn half, before the
  mirror: the whole stays symmetric, and the gizmo moves the panel so that its
  middle stays where it was on the table. An
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
  The avatar that comes with the program was generated by AI, and its mesh
  is uneven.
- The avatar's armpits pinch a top without sleeves (sample 6) at its
  underarm points: a few edges there stretch up to about 30 %.
- Which panels and seams are hidden or deactivated is not saved in the file
  and is not an undo step; a panel or seam deleted and brought back by undo is
  shown and active.
  A panel activated again where cloth has moved into it is pushed apart from it
  at once, which can jolt.
- Cloth does not resist folding across a seam as it does inside a panel, and a
  pressed seam is shaded round like any other fold.
- A seam is chosen only by its row in the Outliner, not by clicking it on the
  table or in 3D. Where the graphics card draws no wide lines, the seams' lines
  in 3D are one pixel wide.
- The 3D view draws without smoothing of edges (no multisampling) and one
  picture at a time (the processor waits for each before the next). Drawing
  into a window is checked only by hand: the tests draw off screen.

See `ROADMAP.md` for what comes next.
