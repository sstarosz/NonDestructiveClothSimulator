# Roadmap after 0.1

Small steps, each useful on its own. Within a group they are in the order I
would do them. Sizes: S is an evening, M a weekend, L more than that.

## 0.1.1 - consistency, before 0.2

Agreed on 16 November. In this order; each step is something to try.

| # | Step | State |
| --- | --- | --- |
| 1 | Structure: libraries under `libs/` with public and private headers, C++23, vcpkg, Catch2 with one test file per source file, integration tests and benchmarks in CTest, a `session` library that owns the document, undo and redo, the mesh, the body and the cloth's thread, clear ownership | done |
| 2 | Fixes: a hole where several panels share a seam corner; dragging a corner lags. The hole came from the body's friction holding cloth while seams pulled in, not from the shared corner: while sewing, nothing rubs now (band and skirt on the mannequin: widest stitch 3.3 cm before, 0.4 cm now). The lag came from making the mesh in the frame (64 ms a frame at 1 cm particles): it is made in the background now (4 ms a frame) | done |
| 3 | Behaviour: a new panel gets a place in space once, then the 2D table and the 3D arrangement are independent; growth and shrinking keep sewn edges where they are (a longer skirt grows at the hem); dragging in 3D while simulating pulls the cloth, as in Marvelous Designer, and never restarts a panel; undo and redo bring the drape back too | |
| 4 | Adding points on an edge, so that shirts can be made | |
| 5 | The Qt 6.12 window, after a mockup is approved, with our own Vulkan renderer for the 3D view (volk, VMA); the ImGui window goes once Qt covers everything | |

## 0.2 - make what is there comfortable

| # | Step | Size | Why |
| --- | --- | --- | --- |
| 1 | Fabric settings in the window (weight, stretch, bend, friction), saved in the file | S | The solver has them; nothing shows them |
| 2 | Pins: click particles in 3D to hold them | S | Holding a piece while the rest drapes |
| 3 | Native file dialogs for open, save and avatar | S | Typing paths is the roughest part of the interface |
| 4 | The avatar's path saved in the document | S | A file should reopen on the body it was made for |
| 5 | Simulation settings: particle distance per document, quality (sub-steps) | S | Fine cloth and fast preview are different needs |
| 6 | Redo, and undo that restores the drape | M | Undo exists; redo does not |
| 7 | Rotate handle for a panel in 3D | M | Turn, tilt and roll are only number fields now |
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
- Cloth that hardly resists being pushed shorter, so it buckles as real cloth
  does and a longer edge gathers onto a shorter one. Tried in 0.1.1: seams
  close tighter still (0.06 cm), but every cloth step took about a quarter
  longer, because buckling cloth moves and touches itself more.
- Symmetric base with the symmetry broken later, the base kept in step.

## Done after 0.1

- Move gizmo in 3D on the selected panel: an arrow per axis and a centre square. It stands at the panel's arranged centre, so while the cloth is draped it can be away from where the cloth hangs. It is drawn over everything and has no plane handles; turning is step 7.
- Performance: an unchanged frame costs 0.1 ms with or without the 3D view; the cloth runs on its own thread; benchmarks with baselines catch regressions.

## Things to keep true

- The core has no dependencies and never draws.
- Every change goes through a verb of the document; a document is a value.
- Evaluation, meshing and simulation never throw; problems are reported
  where they are.
- Every feature arrives with a test that fails without it.
- Nothing is worked out per frame that has not changed; the benchmarks guard it.
