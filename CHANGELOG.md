# Changelog

## 0.1.0

The first release of Non-Destructive Cloth CAD: a pattern CAD for clothing in
which nothing is baked in. Draw a pattern, sew it, drape it on your own
character, change any measurement while it hangs, and take it into Blender or
Maya.

### Start

Windows 10 or 11 (x64) and a graphics card with a Vulkan 1.1 driver. Unpack
the zip (right-click, Extract All) and open `bin\clothcad.exe`. There is no
installer.

The program is not signed, so Windows warns about an unknown publisher:
"Windows protected your PC". Choose "More info", then "Run anyway".

A first garment, step by step:
<https://github.com/sstarosz/NonDestructiveClothSimulator#using-cloth-cad>

### What it does

- **Patterns on a table**: rectangles, circles, rings, added points, curves,
  internal lines, cuts and splits, mirrors, and the length of any edge.
  Every step stays a layer you can change or switch off.
- **Parameters and formulas** (`waist / 4 + ease`): change a measurement and
  every panel, copy, instance and seam that depends on it follows.
- **Seams** between edges, and instances that come sewn, so a second leg or
  sleeve follows the first.
- **3D**: panels placed around an avatar read from an OBJ file (in mm, cm, m
  or inches, scaled to the height you want, with any axis up, and shown as you
  choose), and a cloth simulation that stays draped while you edit the pattern.
  Pull the cloth, move panels with gizmos as in Maya, make the body
  see-through.
- **Files**: one file holds the whole scene, the avatar and the drape
  included. You are asked before unsaved work is lost, and offered a recovery
  copy after a crash. After an error it cannot go on from, the program copies
  your work, says so and closes. Files saved by 0.1 open in every later
  version.
- **Export OBJ**: the garment as it hangs, in the units and axes of the
  avatar's file, one group per panel, with the pattern as UVs.
- Seven sample garments (File > Samples), from a skirt to trousers.

### Known limits

- The avatar must be one closed surface. One of millions of triangles makes
  the pattern's file large (80 MB and more).
- One fabric for everything. No pins.
- Seams join whole edges, one to one. There are no darts or pleats.
- Cloth keeps clear of cloth by about 1 cm, and cloth pulled open or moving
  very fast can slip through.
- Pieces that do not fit mostly stretch until the seam closes, so not every
  misfit is reported.
- The exported OBJ is the cloth alone: no thickness, no seam allowance, no
  materials.
- A seam is chosen by its row in the Outliner, not on the table or in 3D.
- Without a Vulkan driver the table works, and the 3D view says what is
  missing.

All of them: <https://github.com/sstarosz/NonDestructiveClothSimulator#known-limits-of-01>
