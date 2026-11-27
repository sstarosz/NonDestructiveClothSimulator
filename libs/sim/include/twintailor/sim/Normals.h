#pragma once
// Which way the cloth faces at each particle, smooth across closed seams: what
// the 3D view shades it with and an exported file keeps.
//
//   std::vector<std::vector<sim::Vec3>> normals = sim::clothNormals(mesh, positions);
//   normals[i][k]      // at particle k of mesh.panels[i], of length 1
//
// A normal points out of the panel's front, the side its triangles run
// counter-clockwise on.

#include <vector>

#include "twintailor/sim/Layout.h"
#include "twintailor/sim/Mesh.h"

namespace twintailor::sim {

// cm: a stitch this short has brought its two sides together.
inline constexpr double joinedWithin = 0.5;

// For every particle, the faces around it added up, weighted by their area.
// Particles a closed stitch (see joinedWithin) pairs share theirs, so that
// cloth is shaded across a seam as it is inside a panel; where three seams
// meet, all of them do. Across a reversed seam the other panel's front faces
// the other way, so its share is taken, and given back, turned round. A
// particle keeps its own where the shared one would face away from it (cloth
// folded flat at a seam). One with no face around it points up. A panel the
// positions do not fit has an empty list.
std::vector<std::vector<Vec3>> clothNormals(const ClothMesh& mesh, const Placement& positions);

} // namespace twintailor::sim
