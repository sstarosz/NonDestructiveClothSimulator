#pragma once
// One panel's particles put where an arrangement says, for sim::arranged and
// for carrying a drape over to a new wrap.
//
//   const std::vector<Vec3> at = arrangedPanel(was, panelMesh);

#include <vector>

#include "twintailor/sim/Layout.h"

namespace twintailor::sim {

// A position for each of the mesh's points: the panel flat about its anchor,
// bent by the wrap, rolled, tilted, turned and put at the arrangement's place.
std::vector<Vec3> arrangedPanel(const Arrangement& in, const PanelMesh& mesh);

} // namespace twintailor::sim
