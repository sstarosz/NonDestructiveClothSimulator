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

// A panel stands where the table has it only within this space around the
// body. A table that has grown wider or taller (pieces put aside, grouped)
// wraps round into it, each time round a step further forward, so that new
// pieces stand near the body and not in the plane of the first ones.
inline constexpr double standingAcross = 100.0_cm;  // either side of the body
inline constexpr double standingLowest = 0.0_cm, standingHighest = 200.0_cm;
inline constexpr double standingStepForward = 15.0_cm;

// The arrangement a panel's mesh is drawn with. For a panel that has not
// been arranged, this is where the 2D table has it, standing upright in
// front of the body; `placed` is then false.
Arrangement arrangementOf(const Document& doc, const PanelMesh& mesh);

// The place in space a panel gets once, when it is new: where the 2D table
// has it now (wrapped into the space around the body, see standingAcross),
// standing upright in front of the body. `placed` is true, so
// from then on the table no longer moves it. Nothing (placed false) for an
// outline with no corners yet.
//
//   doc.arrange(id, sim::firstPlace(doc.get(id), shapes[id]));
Arrangement firstPlace(const Panel& panel, const Pattern& outline);

// The spot of a panel that its arrangement places: the anchor it was given,
// else the middle of its outline's box.
Vec2 anchorOf(const Arrangement& arrangement, const Pattern& outline);

// Where a panel's anchor goes when its outline changes from `before` to
// `after`, so that its sewn edges stay where they are in space. It moves with
// the sewn edges whose length did not change (a skirt's waist, when the skirt
// is made longer: it grows at the hem); if every sewn edge changed, with all
// of them; a panel sewn to nothing grows about the middle of its box.
Vec2 anchorAfterChange(const Document& doc, PanelId panel, const Pattern& before, const Pattern& after, Vec2 anchorBefore);

// The mirror image of an arrangement across x = 0, the body's middle, for a
// panel whose outline is the other's mirrored (Instance::mirrored): every
// spot (x, y) of the other has its image at (-x, y) here, and stands in space
// where the mirror image of that spot does.
//
//   doc.arrange(leftLeg, sim::mirrored(doc.get(rightLeg).arrangement));
Arrangement mirrored(const Arrangement& arrangement);

// A whole draped panel carried through space, as when its arrangement is
// changed while it hangs: turned about where its anchor stood, then taken to
// where the anchor stands now. Its drape is kept; its wrap, which only says
// how a panel starts, is not followed.
//
//   const sim::Motion motion{.before = was, .after = now};
//   Vec3 there = motion(here);
struct Motion {
    Arrangement before, after;
    Vec3 operator()(Vec3 point) const;      // where a point of the panel goes
    Vec3 turned(Vec3 direction) const;      // how a direction (a speed) turns
};

// Which way a direction of the panel's own flat coordinates (x and y as on
// the table, z out of its front) points in space once it is arranged, by its
// turn, tilt and roll. Its wrap is not followed: this is the panel at its anchor.
Vec3 inSpace(const Arrangement& arrangement, Vec3 own);

// The arrangement turned `degrees` about `axis` (a direction in space; the
// turn is counter-clockwise looking down the axis at its tip), about its
// anchor: where the anchor stands stays. The turn, tilt and roll are worked
// out again to give that orientation; at a tilt of exactly ±90° the roll
// becomes part of the turn.
//
//   doc.arrange(panel, sim::turnedAbout(was, {0, 1, 0}, 30.0));  // as a turn of 30°
Arrangement turnedAbout(const Arrangement& arrangement, Vec3 axis, double degrees);

// A panel and how it was moved.
struct Moved {
    PanelId panel;
    Motion motion;
};

// Every particle where its panel's arrangement puts it: the panel is bent
// by its wrap, rolled, tilted and turned about its centre, and its centre
// put at the arrangement's position.
Placement arranged(const Document& doc, const ClothMesh& cloth);

} // namespace clothcad::sim
