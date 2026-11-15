#pragma once
// Cloth in motion: the meshes' particles fall under gravity, hold their
// shape, are pulled together along seams and rest on the body and the floor.
//
//   sim::ClothMesh mesh = sim::meshOf(doc, shapes);
//   sim::Cloth cloth(mesh, sim::arranged(doc, mesh));
//   cloth.step(body);            // one sixtieth of a second later
//   cloth.positions()            // where every particle is now
//
// The method is XPBD with small steps: every step is cut into short
// sub-steps, and in each the particles move freely and are then corrected so
// that edges keep their length, folds resist bending, stitches close and
// nothing is inside the body. It is stable for any setting, and the same
// input always gives the same motion.
//
// Cloth is kept from passing through cloth by keeping its particles apart:
// no two may come closer than about six tenths of the mesh's particle
// distance, unless they are that close in the flat pattern or sewn together.
// Layers therefore rest that far apart, and cloth that is stretched open or
// moving very fast can still slip through.
//
// Lengths are cm, time is seconds, weight is grams.

#include <cstdint>
#include <vector>

#include "sim/Body.h"
#include "sim/Layout.h"
#include "sim/Mesh.h"

namespace clothcad::sim {

struct Fabric {
    double weight = 0.02;      // grams per square cm; 0.02 is a 200 g/m2 cotton
    double stretch = 1e-7;     // how easily it stretches: 0 not at all, larger is stretchier
    double bend = 5e-3;        // how easily it folds: smaller is stiffer, like card
    double friction = 0.35;    // against the body and the floor: 0 slides freely
    double thickness = 0.3_cm; // how far its particles stay from the body
    double airDrag = 1.5;      // how quickly motion dies away, per second
    bool collidesWithItself = true;  // whether cloth is kept from passing through cloth
};

class Cloth {
public:
    // `start` runs parallel to mesh.panels, as sim::arranged() gives it.
    // Panels whose list is missing or the wrong length are left out.
    Cloth(const ClothMesh& mesh, const Placement& start, Fabric fabric = {});

    // The cloth carried over to a pattern that has changed, so that it stays
    // draped instead of starting again. `mesh` and `arrangedNow` describe the
    // pattern as it is now; `before` is the cloth so far and `beforeMesh` the
    // mesh it was made from. Each particle takes the place the same spot of
    // its panel had before (panels are matched by id, spots by the panel's own
    // flat coordinates); cloth that is new, because a panel grew or was just
    // added, continues from the nearest cloth there was, or starts where it
    // is arranged. Panels listed in `placeAgain` start from their arrangement
    // whatever they were doing. Seams draw closed again from wherever their
    // two sides now are. Pins are not carried over.
    static Cloth continued(const ClothMesh& mesh, const Placement& arrangedNow, const Cloth& before, const ClothMesh& beforeMesh,
                           const std::vector<PanelId>& placeAgain = {});

    // Holds a particle where it is now. `panel` indexes the mesh's panels.
    void pin(uint32_t panel, uint32_t point);

    // Moves time on by `seconds` (a sixtieth by default; values are kept
    // between a 240th and a 30th).
    void step(const Body& body, double seconds = 1.0 / 60.0);

    const Placement& positions() const { return positions_; }
    double time() const { return time_; }
    // The speed of the fastest particle, in cm per second: near zero once
    // the cloth has come to rest.
    double fastest() const;
    // Whether any seam is still being drawn closed. Until they all are, the
    // cloth is held nearly weightless.
    bool sewing() const;
    // False if any position has stopped being a number.
    bool sound() const;

private:
    struct Link {
        uint32_t a, b;
        double rest;
    };
    struct Thread {
        uint32_t a, b;
        double left;  // how much thread is still to be drawn in
    };

    void solve(const Link& link, double compliance, double stepSquared);

    Fabric fabric_;
    std::vector<uint32_t> firstOf_;  // per panel: its first particle, or none
    std::vector<Vec3> at_, before_, speed_;
    std::vector<double> lightness_;  // one over a particle's mass; zero when pinned
    std::vector<Link> edges_, folds_;
    std::vector<Thread> threads_;
    std::vector<uint32_t> near_, firstNear_;  // per particle: the limbs within reach this step

    // Cloth against cloth.
    void findClosePairs(double seconds);
    double apart_ = 0.0;                 // how far apart particles are kept
    std::vector<uint32_t> panelOf_;      // per particle: its panel
    std::vector<Vec2> flat_;             // per particle: where it is in its panel's flat pattern
    std::vector<char> sewn_;             // per particle: whether a stitch holds it
    struct Pair {
        uint32_t a, b;
    };
    std::vector<Pair> close_;            // the pairs that could touch during this step
    Placement positions_;
    double time_ = 0.0;
};

} // namespace clothcad::sim
