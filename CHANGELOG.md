# Changelog

## 0.1.0

The first release of Twintailor: a pattern CAD for clothing in which nothing
is baked in. Draw a pattern, sew it, drape it on your own character, change any
measurement while it hangs, and take it into Blender or Maya.

### Start

Windows 10 or 11 (x64) and a graphics card with a Vulkan 1.1 driver. Unpack
the zip (right-click, Extract All) and open `twintailor.exe` in it. There is no
installer. You do not need the zip whose name ends in `-symbols`: it is for
finding the cause of a crash.

The program is not signed, so Windows warns about an unknown publisher:
"Windows protected your PC". Choose "More info", then "Run anyway".

A first garment, step by step:
<https://github.com/sstarosz/Twintailor#a-first-garment-trousers>

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
- **Undo** takes back every change of the pattern, and also a pull of the cloth
  or a Reset of it: the drape comes back as it hung.
- **Files**: one file holds the whole scene, the avatar and the drape
  included. You are asked before unsaved work is lost. Files saved by 0.1 open
  in every later version.
- **After a crash**: unsaved work is copied after every change, and the next
  start offers the copy. After an error it cannot go on from, the program
  copies your work, says so and closes.
- **Crash reports**: after a run that did not close properly, the program keeps
  a report in `%LOCALAPPDATA%\Twintailor\crash-reports`, with the last things
  it did, the pattern as last copied and, for some crashes, a dump. The report
  stays on your computer: nothing is sent. It holds your work, and a dump may
  hold your user name too, so look before you share one.
- **Export OBJ**: the garment as it hangs, in the units and axes of the
  avatar's file, one group per panel, with the pattern as UVs.
- Eight sample garments (File > Samples), from a skirt to a T-shirt with
  trousers.

### Known limits

- The undo history is not saved in the file and does not come back after a
  crash, and nor does the motion of cloth that was playing or being pulled.
- Undo keeps the drape of the last 64 steps. Reset drops the whole cloth, not
  one panel's.
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

All of them: <https://github.com/sstarosz/Twintailor/blob/main/docs/known-limits.md>
