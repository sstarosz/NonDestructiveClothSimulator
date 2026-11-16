#pragma once
// Where each cloth particle is in space.
//
//   sim::ClothMesh cloth = sim::meshOf(doc, shapes);
//   sim::Placement placed = sim::arranged(doc, cloth);
//   placed[i][k]      // particle k of cloth.panels[i]
//
// A Placement runs parallel to ClothMesh::panels: one list of positions per
// panel, one position per point. It is what the 3D view draws and what the
// simulation starts from.

#include <vector>

#include "clothcad/sim/Mesh.h"
#include "clothcad/sim/Vec3.h"

namespace clothcad::sim {

using Placement = std::vector<std::vector<Vec3>>;

// How far in front of the body a panel stands until it has been arranged.
inline constexpr double unarrangedDistance = 45.0_cm;

// The arrangement a panel's mesh is drawn with. For a panel that has not
// been arranged, this is where the 2D table has it, standing upright in
// front of the body; `placed` is then false.
Arrangement arrangementOf(const Document& doc, const PanelMesh& mesh);

// Every particle where its panel's arrangement puts it: the panel is bent
// by its wrap, rolled, tilted and turned about its centre, and its centre
// put at the arrangement's position.
Placement arranged(const Document& doc, const ClothMesh& cloth);

} // namespace clothcad::sim
