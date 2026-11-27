#pragma once
// A garment as a file another program reads: the text of a Wavefront OBJ file.
// Nothing is written to disk here.
//
//   sim::Obj obj = sim::objOf(doc, cloth, positions);
//   file << obj.text;
//
// Each panel is a group (`g`) named after it, with its own particles: `v` where
// they are in space, `vt` their place in the flat pattern, and `vn` the cloth's
// normals (sim::clothNormals), smooth across closed seams. Faces run
// counter-clockwise seen from the panel's front.
//
// `v` and `vn` are in the units and axes of the avatar's file (the document's
// Avatar::readAs undone), so that garment and body line up where both are
// opened; cm, y up, with no avatar. The first line says the unit.
//
// The `vt` are the pattern as it lies on the table (each panel turned and moved
// as it is there), all of it in the square from 0 to 1, with one scale for both
// directions. Panels left out keep their place in that square, so leaving one
// out moves none of the others'.

#include <cstddef>
#include <string>
#include <vector>

#include "clothcad/sim/Layout.h"
#include "clothcad/sim/Mesh.h"

namespace clothcad::sim {

struct ObjOptions {
    std::vector<PanelId> leftOut{};  // panels that are not written
};

struct Obj {
    std::string text;
    std::size_t panels = 0;    // how many are in the text
    std::size_t leftOut = 0;   // how many ObjOptions::leftOut kept out of it
    std::size_t unmeshed = 0;  // how many are not in it because they have no cloth (PanelMesh::problem)
};

// `positions` run parallel to the mesh's panels. A panel the document no longer
// has, or one the positions do not fit, is not written and not counted.
Obj objOf(const Document& doc, const ClothMesh& mesh, const Placement& positions, const ObjOptions& options = {});

} // namespace clothcad::sim
