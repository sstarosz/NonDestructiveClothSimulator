# Roadmap after 0.1

Small steps, each useful on its own. Within a group they are in the order I
would do them. Sizes: S is an evening, M a weekend, L more than that.

## 0.1.1 - consistency, before 0.2

Agreed on 16 November. In this order; each step is something to try.

| # | Step | State |
| --- | --- | --- |
| 1 | Structure: libraries under `libs/` with public and private headers, C++23, vcpkg, Catch2 with one test file per source file, integration tests and benchmarks in CTest, a `session` library that owns the document, undo and redo, the mesh, the body and the cloth's thread, clear ownership | done |
| 2 | Fixes: a hole where several panels share a seam corner; dragging a corner lags. The hole came from the body's friction holding cloth while seams pulled in, not from the shared corner: while sewing, nothing rubs now (band and skirt on the mannequin: widest stitch 3.3 cm before, 0.4 cm now). The lag came from making the mesh in the frame (64 ms a frame at 1 cm particles): it is made in the background now (4 ms a frame) | done |
| 3 | Behaviour: a new panel gets a place in space once (wrapped into ±100 cm across and 0-200 cm up around the body, a step further forward each time round), then the table and space are independent; panels grow away from their seams, on the table and in space alike (an arrangement's anchor moves with the sewn edges that kept their length; a shape dragged by hand keeps its own coordinates instead); dragging draped cloth in 3D pulls it, as in Marvelous Designer, and the cloth runs while it is held; the gizmo carries a draped panel, drape and all; nothing starts a panel again except Reset; undo and redo bring the drape back (the last 64 states) | done |
| 4 | Adding points on an edge, so that shirts can be made: the AddPoint operation and the Point tool (a click on the mirrored side of a symmetric panel adds the point on the drawn side); the new corner is dragged like any other; sample and example 6 are a T-shirt body | done |
| 5 | The Qt 6.12 window, laid out as approved (Maya-like: shelf, dockable panels, 3D left and table right, operations as layers, units everywhere). In pieces: (a) the frame: shelf, docks, help line, layouts, Outliner, Parameters, Attribute Editor with layers and the panel's particle distance, the body's eye in the Outliner, the table showing and selecting (done); (b) the table's gestures, through the shared `session::Drafting`: Select (move, corners, box select, handles, turning knob, length by double-click), every drawing tool, choosing a layer's edge, corner or line on the table, the cloth mesh over the panels (done); (c) the 3D view: our own Vulkan renderer with Vulkan-Hpp (`libs/render`), tested off screen: drawing the body, cloth and floor with a Maya camera (done); a click selects a panel, the move gizmo (an arrow per axis and a middle square, drawn in front of everything, `render/Gizmo`) or a drag on the panel carries it as one undo step, and dragging draped cloth pulls it (done); (d) the ImGui window compared feature by feature (what it had and the Qt window lacked was added: lengths on the table, seam ends linked, placing from the table, the grid's step, axes in 3D), its tests moved to the Qt window and `session::Drafting`, then removed with Dear ImGui and GLFW; the program is `clothcad` again | done |

## 0.2 - make what is there comfortable

| # | Step | Size | Why |
| --- | --- | --- | --- |
| 1 | Fabric settings in the window (weight, stretch, bend, friction), saved in the file | S | The solver has them; nothing shows them |
| 2 | Pins: click particles in 3D to hold them | S | Holding a piece while the rest drapes |
| 3 | Native file dialogs for open, save and avatar | S | Done with the Qt window |
| 4 | The avatar's path saved in the document | S | A file should reopen on the body it was made for |
| 5 | Simulation settings: quality (sub-steps). The particle distance is now a property of each panel | S | Fine cloth and fast preview are different needs |
| 6 | Show which way a panel will grow before a parameter changes it (an arrow on the edges that move) | S | Growth follows the seams; seeing it first avoids surprises |
| 7 | Rotate handle for a panel in 3D | M | Done: Maya's gizmos in both views (Q select, W move, E rotate, R scale) |
| 8 | Arrangement points on the body (front, back, sleeve ...) | M | One click to place a panel instead of seven numbers |

## 0.3 - patterns

| # | Step | Size | Why |
| --- | --- | --- | --- |
| 9 | Seams along part of an edge, and one edge to several | M | Sleeves into armholes, collars, gathers in one place |
| 10 | Internal lines with free points, several segments, curves | M | Darts, pleats, pockets |
| 11 | Darts and pleats as operations | M | Shaping that a flat pattern cannot do |
| 12 | Curves of several segments per edge | M | Armholes and necklines need more than one cubic |
| 13 | Mirror across an internal line | S | Fold lines inside a panel |
| 14 | Positions along an edge in cm, not only as a fraction | S | "3 cm from the corner" stays 3 cm when the edge grows |
| 15 | Seam allowance and export of outlines (SVG, DXF, PDF at 1:1) | M | Getting a pattern out to paper or a cutter |
| 16 | Grading: a table of sizes for the parameters | M | The parametric model makes this nearly free |

## 0.4 - simulation quality

| # | Step | Size | Why |
| --- | --- | --- | --- |
| 17 | Cloth against cloth with triangles, not only particles | L | Thin layers, no slipping through |
| 18 | A fabric per panel, and different stretch along and across the weave | M | Linings, knits, bias cuts |
| 19 | Bending from the angle between triangles | M | Stiffness that does not depend on the mesh |
| 20 | The solver on several threads (it already runs beside the interface) | M | Finer meshes at the same speed: a step of 2,800 particles takes 11-15 ms of the 16 ms a frame allows |
| 21 | Friction between layers of cloth | S | Layers that stay where they are put |
| 22 | Stretch shown as colour on the cloth | S | Seeing where a garment is tight |

## 0.5 - picture and bodies

| # | Step | Size | Why |
| --- | --- | --- | --- |
| 23 | The 3D picture drawn by the graphics card (now 0.1.1 step 5: Vulkan) | M | Smooth edges, and orbiting a large view in well under the 4 ms it takes now |
| 24 | Fabric colour and a texture per panel | M | Seeing the garment, not grey cloth |
| 25 | An avatar from measurements; poses | L | Fit for a person, not for one mesh |
| 26 | Bodies that are not one closed surface | M | Scans and game characters |
| 27 | Export of the draped garment (OBJ) | S | Using it elsewhere |

## Later

- Wrapping patterns onto the body (tights, socks, shoes): flattening a 3D
  surface into panels. This is its own project.
- A scripting layer (Python) over the document's verbs.
- Seams along part of an edge, from one point to another, so that adding a
  point to a sewn edge does not halve its seam.
- Cloth that hardly resists being pushed shorter, so it buckles as real cloth
  does and a longer edge gathers onto a shorter one. Tried in 0.1.1: seams
  close tighter still (0.06 cm), but every cloth step took about a quarter
  longer, because buckling cloth moves and touches itself more.
- Symmetric base with the symmetry broken later, the base kept in step.

## Done after 0.1

- Move gizmo in 3D on the selected panel: an arrow per axis and a centre square. It stands at the panel's arranged centre, so while the cloth is draped it can be away from where the cloth hangs. It is drawn over everything.
- Gizmos as in Maya, on Q, W, E and R, in the 3D view (`render/Gizmo`) and on the table (`session/TableGizmo`): Q selects only; W moves (arrows, a square per plane, the middle across the view); E turns (a ring per axis showing only what is not behind the gizmo, a ring about the line of sight, and between them a ball that turns freely, as Maya's trackball; one ring on the table, in place of the turning knob); R scales along the panel's own x and y, or evenly (the square between them, the middle; its z is grey: a panel has no thickness), as a new `Scale` operation at the end of the panel (the last one is changed again by the next drag). Moving and turning in space only place a panel (`sim::turnedAbout` works out turn, tilt and roll again). Keys typed in the 3D view's own window reach the shelf's shortcuts. In 3D the gizmos are drawn flat (`Surface::lit`), without light and shade. Rectangle has no key any more.
- Performance: an unchanged frame costs 0.1 ms with or without the 3D view; the cloth runs on its own thread; benchmarks with baselines catch regressions.

## Things to keep true

- The core has no dependencies and never draws.
- Every change goes through a verb of the document; a document is a value.
- Evaluation, meshing and simulation never throw; problems are reported
  where they are.
- Every feature arrives with a test that fails without it.
- Nothing is worked out per frame that has not changed; the benchmarks guard it.
