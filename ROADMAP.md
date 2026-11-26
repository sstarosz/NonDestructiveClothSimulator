# Roadmap

Small steps, each useful on its own. Within a group they are in the order I
would do them. Sizes: S is an evening, M a weekend, L more than that. A
number like #54 is a GitHub issue, and the milestones "0.1", "0.2" and
"0.x" (after 0.2) on GitHub hold the issues of each version. GitHub says what is open; this file
says why and in which order.

## Where it stands (26 November 2026)

Patterns, formulas, seams, instances, the drape on an avatar, and the Qt
window with its Vulkan 3D view all work. A skirt, a T-shirt body and a pair of
trousers can be made. Of the list from making trousers (#41 to #54), what is
left is the speed of the window and of the cloth, and seeing the body inside
the cloth.

Nothing has been released. There is no package to download and no licence.
A garment cannot be taken out of the program, and closing the window loses
unsaved work without asking.

## 0.1 - the first release

**The aim:** someone who is not me downloads a zip, makes a garment on their
own avatar, and takes it into Blender or Maya without losing work. Everything
else waits for 0.2: each feature added before 0.1 delays what other people's
use of it would teach.

In pull requests, in this order:

| PR | What | Issues | Size | Why this order |
| --- | --- | --- | --- | --- |
| 1 | **Trousers sample, benchmarks to trust.** A seventh sample and example: front, back, a mirrored instance, waist and ankle by Set length. The benchmarks pass on an unchanged `main` on my machine, or the reason is known | #88, #57, #68 | S | Everything after it is measured and tried on the trousers |
| 2 | **Real frame times.** A View switch that shows, in the real window, the time between shown frames, the time of `tick` and of painting, the cloth step and the particle count | #56, #28 (closes #41) | S | Decides what PRs 3 and 4 work on |
| 3 | **Table repaint**, only if PR 2 shows the table is slow: repaint only what changed, square caps for the last thick lines | #58, #59 | M | Fixes the slow part the numbers name, not a guess |
| 4 | **A faster cloth at 2 cm.** The trousers' step time in the benchmarks, then the cheapest gains first: a preview quality with fewer sub-steps, a default particle distance from the panel's size. The solver on several threads is 0.2 | #54 | M | The cloth's speed is felt on every garment |
| 5 | **Fitting the trousers.** A see-through body (View menu and the body's row); a mirrored instance stands in space mirrored from its source, so the second leg drapes without arranging it by hand | #53, #69 | S | The last of the trousers list |
| 6 | **Fixes from 25 November.** Scaling a rectangle in 3D scales it about the gizmo, as on the table; a change of wrap shows after the cloth has run; with the cloth paused, a drag in 3D moves the panel instead of pulling it and running the cloth | #94, #93, #92 | M | Bugs met in ordinary work, better gone before anyone else meets them |
| 7 | **Never lose work.** Ask before unsaved changes are lost (closing, New, Open, a sample), with "*" in the title. The avatar's path is saved in the file, so a file opens on the body it was made for. A file written by 0.1 is kept in `FileTests` and must open in every later version | #87, #26 (the avatar), #86 | S | Other people's work is at stake from now on. The file holds the avatar before its format is promised |
| 8 | **Export OBJ.** The garment as it hangs (flat if it was not simulated), in the avatar's units so it lines up with the character, one group per panel, the pattern as UVs, normals smooth across closed seams | #85 | M | Without it the garment cannot be used anywhere else |
| 9 | **A first user is not left stuck.** Say when a seam could not close, on its card and its lines in 3D, and what to do. Sew the way the clicks say, so that a seam is not reversed by surprise. Parameter names show their start. A copy keeps the turn of the panel it copies | #14, #11, #75, #62 | M | A seam that silently stays open is the worst first impression |
| 10 | **The release.** The MIT licence, and the notices of what the zip ships (Qt under LGPL 3, CDT under MPL 2.0, the MSVC runtime, the avatar). Help > About and `clothcad --version`. A tag `v0.1.0` makes CI build and test, install into a folder, start the installed program once, zip it, and draft a GitHub Release; I publish it. `CHANGELOG.md`, and a README part for users: Windows 10 or 11 (x64), a graphics card with Vulkan 1.1, how to start, a first garment on the trousers sample | #84, #83, #82, #81 | M | Last, so the first zip has everything. The `LICENSE` file goes in at once, though: the code is public already, and without a licence nobody may use it |

**Before the tag**, checked by hand on the 4K display:

- Alt pressed during a number drag does not open the menu bar (#6).
- Space and the tool keys typed into a field, a parameter's name and the
  Outliner's rename box do only that (#33, which also gets its test).
- The window looks right after the Cloth tab change (#30).
- The zip, unpacked on a Windows PC that never built the project, starts,
  opens every sample, and drapes the trousers on the shipped avatar and on
  another OBJ. The exported OBJ opens in Blender with its UVs.
- README "Known limits" is read through once more, and each one is still true.

**0.1 is out when** all of the above is done, the tests pass in CI, the
benchmarks pass on my machine, and the Release is published.

## 0.2 - comfortable to work with

**The aim:** a garment like the trousers or a T-shirt is made without detours.
Seams are worked on the table, a panel goes onto the body in one click, and
the cloth is quick and can be held and tuned. What users of 0.1 report comes
first and may push steps out.

### Seams on the table

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 1 | A click on a seam, on the table or its lines in 3D, chooses it; Delete removes it; the Attribute Editor shows its card | M | #80, #79, #78 |
| 2 | Tell seams apart: uneven seams in orange on the table and in 3D, rows that name their edges, a card's edges lit while it is hovered | M | #10, #12, #9 |
| 3 | Move one side of a seam to another edge | S | #13 |
| 4 | Copies come sewn: a paste or duplicate brings the seams among the copied panels; Sew as source leaves an edge that is sewn elsewhere alone | S | #72, #70 |

### Placing and fitting

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 5 | Alignment points on the avatar (chest, waist, hips, sides, shoulders, arms, legs): one click puts a panel there instead of seven numbers | M | #90 |
| 6 | Pins: click particles in 3D to hold them while the rest drapes | S | |
| 7 | Several panels selected in the 3D view and the Outliner, as on the table | M | #89 |
| 8 | Hide and deactivate the selected panels with a key; the table shows a deactivated panel faded; pulling reaches the cloth behind it | S | #77, #76 |
| 9 | A copy or an instance of a draped panel starts with its source's drape, mirrored for a mirrored instance | M | #91 |
| 10 | Stretch shown as colour on the cloth, to see where a garment is tight | S | |
| 11 | Avatars that are not one closed surface (game characters, scans), since the point is to dress one's own character | M | |
| 12 | Show which way a panel will grow before a parameter changes it (an arrow on the edges that move) | S | |

### A cloth that is pleasant to work with

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 13 | The solver on several threads: finer meshes at the same speed (after #54 in 0.1) | M | |
| 14 | Fabric in the window: presets (cotton, denim, jersey, silk) and weight, stretch, bend and friction, saved in the file | M | |
| 15 | Cloth resists folding across a seam as it does inside a panel | S | #74 |
| 16 | The particle distance of all, or the selected, panels at once; dragging it does not mesh again on every move | S | #24, #23 |

### The window

| # | Step | Size | Issues |
| --- | --- | --- | --- |
| 17 | A copy of the document saved every few minutes and offered after a crash; recent files in the File menu | S | |
| 18 | The window opens as it was left; one key folds the docks away | S | #37, #27 |
| 19 | The shelf's Sew and Simulate tabs get what belongs to them (seam actions, fabric, Reset), or go | S | #32 |
| 20 | Number fields drag past the screen's edge; long names end in "…"; Delete on a shape's corner opens that layer | S | #5, #35, #61 |

### Cleanups, done with the step that touches the same code

- With step 14 (new fields): typed names for the fields (#19), plain
  numbers in `ValueField` (#21), the limits of a panel's numbers in one
  place (#22).
- With step 19: the views stop including `Shelf.h` (#38).
- With 0.1's PR 2 or step 13: `setThreading` goes (#29).
- With any window step: the Attribute Editor tested with every section open
  (#36), a picture of the window left by the tests (#34).

## After 0.2

Sorted for clothes for 3D characters. The order between these groups is
decided when 0.2 is out; their issues are in the milestone "0.x".

### Patterns and seams

| Step | Size | Issues | Why |
| --- | --- | --- | --- |
| Seams along part of an edge, from one point to another, and one edge to several | M | #15 | Sleeves into armholes, collars, gathers in one place; a point added to a sewn edge no longer halves its seam |
| An instance's seams worked out from its source, not copied once. With the step above, since both change what a seam is | L | #71 | Nothing baked in |
| Internal lines with free points, several segments, curves | M | | Darts, pleats, pockets |
| Darts and pleats as operations | M | | Shaping that a flat pattern cannot do |
| Curves of several segments per edge | M | | Armholes and necklines need more than one cubic |
| Mirror across an internal line | S | | Fold lines inside a panel |
| Positions along an edge in cm, not only as a fraction | S | | "3 cm from the corner" stays 3 cm when the edge grows |

### Simulation quality

| Step | Size | Issues | Why |
| --- | --- | --- | --- |
| Cloth against cloth with triangles, not only particles | L | | Thin layers, no slipping through |
| A fabric per panel, and different stretch along and across the weave | M | | Linings, knits, bias cuts |
| Bending from the angle between triangles | M | | Stiffness that does not depend on the mesh |
| Friction between layers of cloth | S | | Layers that stay where they are put |
| Gathers: cloth that hardly resists being pushed shorter, so a longer edge gathers onto a shorter one | M | #16 | Tried in 0.1.1: seams closed tighter (0.06 cm), but every cloth step took about a quarter longer, because buckling cloth moves and touches itself more |
| A corner where three seams meet closes | M | #17 | A band's side seam meeting a skirt |
| Even triangles where panels of different particle distances are sewn | S | #25 | |

### Picture, bodies and output

| Step | Size | Issues | Why |
| --- | --- | --- | --- |
| Fabric colour and a texture per panel | M | | Seeing the garment, not grey cloth |
| Smooth edges in the 3D view (multisampling) | S | | |
| A better avatar: the one shipped with 0.1 was generated by AI and its topology is poor | M | | Even triangles to collide with, and a body worth showing |
| An avatar from measurements; poses | L | | Fit for a person, not for one mesh |
| Seam allowance and export of outlines (SVG, DXF, PDF at 1:1) | M | | Getting a pattern out to paper or a cutter |
| Grading: a table of sizes for the parameters | M | | The parametric model makes this nearly free |
| An installer, and a signed program, so Windows does not warn | M | | A zip and an unknown publisher are enough for 0.1 |

## Later

- Wrapping patterns onto the body (tights, socks, shoes): flattening a 3D
  surface into panels. This is its own project.
- A scripting layer (Python) over the document's verbs.
- Symmetric base with the symmetry broken later, the base kept in step.

## Done before the first release

### 0.1.1 - consistency

Agreed on 16 November. In this order; each step is something to try.

| # | Step | State |
| --- | --- | --- |
| 1 | Structure: libraries under `libs/` with public and private headers, C++23, vcpkg, Catch2 with one test file per source file, integration tests and benchmarks in CTest, a `session` library that owns the document, undo and redo, the mesh, the body and the cloth's thread, clear ownership | done |
| 2 | Fixes: a hole where several panels share a seam corner; dragging a corner lags. The hole came from the body's friction holding cloth while seams pulled in, not from the shared corner: while sewing, nothing rubs now (band and skirt on the mannequin: widest stitch 3.3 cm before, 0.4 cm now). The lag came from making the mesh in the frame (64 ms a frame at 1 cm particles): it is made in the background now (4 ms a frame) | done |
| 3 | Behaviour: a new panel gets a place in space once (wrapped into ±100 cm across and 0-200 cm up around the body, a step further forward each time round), then the table and space are independent; panels grow away from their seams, on the table and in space alike (an arrangement's anchor moves with the sewn edges that kept their length; a shape dragged by hand keeps its own coordinates instead); dragging draped cloth in 3D pulls it, as in Marvelous Designer, and the cloth runs while it is held; the gizmo carries a draped panel, drape and all; nothing starts a panel again except Reset; undo and redo bring the drape back (the last 64 states) | done |
| 4 | Adding points on an edge, so that shirts can be made: the AddPoint operation and the Point tool (a click on the mirrored side of a symmetric panel adds the point on the drawn side); the new corner is dragged like any other; sample and example 6 are a T-shirt body | done |
| 5 | The Qt 6.12 window, laid out as approved (Maya-like: shelf, dockable panels, 3D left and table right, operations as layers, units everywhere). In pieces: (a) the frame: shelf, docks, help line, layouts, Outliner, Parameters, Attribute Editor with layers and the panel's particle distance, the body's eye in the Outliner, the table showing and selecting (done); (b) the table's gestures, through the shared `session::Drafting`: Select (move, corners, box select, handles, turning knob, length by double-click), every drawing tool, choosing a layer's edge, corner or line on the table, the cloth mesh over the panels (done); (c) the 3D view: our own Vulkan renderer with Vulkan-Hpp (`libs/render`), tested off screen: drawing the body, cloth and floor with a Maya camera (done); a click selects a panel, the move gizmo (an arrow per axis and a middle square, drawn in front of everything, `render/Gizmo`) or a drag on the panel carries it as one undo step, and dragging draped cloth pulls it (done); (d) the ImGui window compared feature by feature (what it had and the Qt window lacked was added: lengths on the table, seam ends linked, placing from the table, the grid's step, axes in 3D), its tests moved to the Qt window and `session::Drafting`, then removed with Dear ImGui and GLFW; the program is `clothcad` again | done |

### After that

- Move gizmo in 3D on the selected panel: an arrow per axis and a centre square. It stands at the panel's arranged centre, so while the cloth is draped it can be away from where the cloth hangs. It is drawn over everything.
- Gizmos as in Maya, on Q, W, E and R, in the 3D view (`render/Gizmo`) and on the table (`session/TableGizmo`): Q selects only; W moves (arrows, a square per plane, the middle across the view); E turns (a ring per axis showing only what is not behind the gizmo, a ring about the line of sight, and between them a ball that turns freely, as Maya's trackball; one ring on the table, in place of the turning knob); R scales along the panel's own x and y, or evenly (the square between them, the middle; its z is grey: a panel has no thickness), as a new `Scale` operation at the end of the panel (the last one is changed again by the next drag). Moving and turning in space only place a panel (`sim::turnedAbout` works out turn, tilt and roll again). Keys typed in the 3D view's own window reach the shelf's shortcuts. In 3D the gizmos are drawn flat (`Surface::lit`), without light and shade. Rectangle has no key any more.
- Performance: an unchanged frame costs 0.1 ms with or without the 3D view; the cloth runs on its own thread; benchmarks with baselines catch regressions.
- Native file dialogs for open, save and the avatar.
- The trousers list (#42 to #52): delete, copy and paste on the table, the origin on the grid; a corner or panels held to one axis, several panels selected and worked on together; Set length, the Length tool and the length bar; instances that come sewn, and Sew as source.
- Legibility in 3D: a panel's back is a darker, cooler shade of its front; an open seam is lined along both edges, with a line from each point to the one it is sewn to, a seam chosen in the Outliner is shown in amber there and on the table, and each seam's lines can be hidden; each panel can be hidden (it stays in the simulation) or deactivated (it stands still, seen through, and nothing touches it), neither saved in the file; the two sides of a closed seam are shaded as one cloth.

## Things to keep true

- The core has no dependencies and never draws.
- Every change goes through a verb of the document; a document is a value.
- Evaluation, meshing and simulation never throw; problems are reported
  where they are.
- Every feature arrives with a test that fails without it.
- Nothing is worked out per frame that has not changed; the benchmarks guard it.
