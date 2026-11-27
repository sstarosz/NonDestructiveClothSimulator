# Performance and benchmarks

## When to run them

- **Run the benchmarks** (CTest label `benchmark`, optimised build,
  `ctest --preset x64-win-msvc2026-benchmarks`) after any change to `sim`,
  `session` or the drawing, with Twintailor closed and the machine idle.
- **Write new baselines** (`--update`) only when the owner agrees, and say
  which numbers moved and why; never to make a failure go away.
- Before blaming a change for a failure, run the same benchmark on `main` on
  the same machine: the yardstick shows when the machine was busy.

Why they run on the owner's machine is
[decision 0017](decisions/0017-benchmarks-and-baselines.md).

## How they work

What a frame and a cloth step may cost is checked by two benchmark programs,
`twintailor_core_and_sim_benchmarks` (core, meshing, cloth) and
`twintailor_qt_window_benchmarks` (whole frames of the window, 1600 x 900,
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

    twintailor_core_and_sim_benchmarks tests/benchmarks/baselines/CoreAndSim.txt --update
    twintailor_qt_window_benchmarks    tests/benchmarks/baselines/QtWindow.txt --update

The budgets, and the times measured on 26 November 2026 (Windows 11, MSVC,
no screen, an idle machine):

| What | Limit | Now |
| --- | --- | --- |
| A frame with nothing changing, both views shown | 1 ms | 0.003 ms |
| A frame while dragging a panel on the table | 8 ms | 1.2-1.4 ms |
| A frame while dragging a corner, 3D shown, 1 cm particles | 16 ms | 4-6 ms |
| The 3D view orbited (drawn off screen and read back) | 60 ms | 5 ms |
| A frame that brings new cloth from its own thread, 3D shown (27 November) | 16 ms | 5 ms |
| A frame with a cloth step in it, 3D shown | 40 ms | 23 ms |
| One cloth step, 2,800 particles, on a body | 16 ms | 10 ms |
| One cloth step, the trousers (2,900 particles), on the avatar | 16 ms | 9-10 ms |
| Meshing a skirt at 2 cm | 25 ms | 8 ms |
| Reading an avatar's OBJ, 400,000 triangles (27 November) | 250 ms | 25 ms |
| Preparing an avatar of 400,000 triangles to collide with (27 November) | 1500 ms | 340 ms |
| A frame with a cloth step in it, 3D shown, an avatar of 1.4 million triangles (27 November) | 50 ms | 17 ms |

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
| Orbiting the 3D view at 4K, solid body / see-through | 120 ms | 40 / 41 ms |
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

- **A body is made ready to draw once.** Its triangles are handed to the
  graphics card when the avatar changes, not with every picture
  (`render::Shape`): a frame with the cloth moving costs the same however
  detailed the body is.
- **Nothing is worked out again unless it changed.** The mesh is made only
  when the outlines change; the 3D picture is drawn only when the camera, the
  view's size or what it shows has changed. An unchanged frame costs the same
  with or without the 3D view.
- **The window ticks once for every picture of its screen** (every 4 ms at
  240 Hz, 16 ms at 60 Hz; `FrameTimes::tickMilliseconds`), so a panel carried
  in 3D is shown at the screen's next picture. On a 240 Hz screen a fixed
  16 ms tick showed one picture in four. A tick with nothing new costs
  0.02 ms. The cloth makes at most 60 steps a second, to keep pace with the
  clock, so a running cloth is shown at 60 pictures a second on any screen.
- **The cloth runs on a thread of its own** (`libs/session` `Simulation`), so the
  interface never waits for a step; it picks up where the particles are
  whenever there is news.
- **A step asks the body only where cloth may touch it.** A particle that
  has not moved far enough since the step began to reach the body is not
  checked against it in the step's 60 rounds. How far is enough follows from
  how steeply the body's distance grid changes (`DistanceField::steepest`):
  on a clean avatar most of a garment is skipped, on overlapping surfaces
  nothing is, so the drape is the same either way. The search for cloth
  touching cloth keeps its table from step to step and looks through each
  slot of it once.
- **The mesh is made in the background** (`Session`). While a corner is
  dragged the 3D view shows the last mesh made and takes the next one when it
  is done, always made from the newest outlines; a frame never waits for it.
- **The 3D view is drawn by the graphics card** (`libs/render`, Vulkan), and
  its scene is made again only when what it shows has changed.
- **The table is painted only when what it shows changed.** A panel carried
  in space, or a change in a dock that the table does not show, does not paint
  it again (about 3 ms of each frame at 4K).
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
