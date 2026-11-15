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
- **Live link**: edit the pattern while the cloth is draped and it stays
  draped.
- **Files**: save and open (`.clothcad`, plain text), undo.

## Building

Needs CMake 3.20 or newer, a C++20 compiler, git and a network connection
the first time (Dear ImGui, GLFW and CDT are downloaded by CMake).

    cmake -S . -B build
    cmake --build build
    ctest --test-dir build        # core tests, sim tests and the five examples

`-DBUILD_APP=OFF` builds without the window; `-DBUILD_SIM=OFF` builds the
core alone, which has no dependencies.

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
| `core/` | The document (parameters, panels, operations, seams), formulas, evaluation to 2D outlines, files | nothing |
| `sim/` | Meshing, arrangement in space, the body, the cloth solver | `core`, CDT |
| `app/` | The window: 2D table, 3D view, details | `core`, `sim`, Dear ImGui, GLFW |
| `examples/` | Five small garments as console programs | `core` |
| `tests/` | `core_tests`, `sim_tests` | `core`, `sim` |

The core never draws and the window never computes: `evaluate(doc)` turns a
document into outlines, `sim::meshOf` turns outlines into triangles, and
`sim::Cloth` moves them. Each of those recomputes from scratch and never
throws.

## Known limits of 0.1

- Cloth keeps clear of cloth by keeping particles apart, so layers rest
  about 1 cm apart, and cloth pulled open or moving very fast can slip
  through.
- One fabric for everything, with fixed properties; no pins in the window.
- The 3D picture is worked out on the processor, without smoothing of edges.
- The avatar must be one closed surface in cm, y up, facing +z.
- No file dialog: files are named by typing a path.
- Seams join whole edges, one to one.

See `ROADMAP.md` for what comes next.
