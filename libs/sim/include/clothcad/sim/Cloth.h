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

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "clothcad/sim/Body.h"
#include "clothcad/sim/Layout.h"
#include "clothcad/sim/Mesh.h"

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

// A seam the cloth gave up on and that is open still.
struct OpenSeam {
    SeamId seam;
    double widest = 0.0;  // its longest stitch: how far apart it stayed, in cm
    friend bool operator==(const OpenSeam&, const OpenSeam&) = default;
};

class Cloth {
public:
    // `start` runs parallel to mesh.panels, as sim::arranged() gives it.
    // Panels whose list is missing or the wrong length are left out.
    Cloth(const ClothMesh& mesh, const Placement& start, Fabric fabric = {});

    // A cloth draped before, back as it was (undo brings a drape back): its
    // particles where `positions` puts them, at rest, `time` seconds on. With
    // `sewn` its seams count as closed already; else they are drawn in again.
    static Cloth resumed(const ClothMesh& mesh, const Placement& positions, double time, bool sewn, Fabric fabric = {});

    // The cloth carried over to a pattern that has changed, so that it stays
    // draped instead of starting again. `mesh` and `arrangedNow` describe the
    // pattern as it is now; `before` is the cloth so far and `beforeMesh` the
    // mesh it was made from. Each particle takes the place the same spot of
    // its panel had before (panels are matched by id, spots by the panel's own
    // flat coordinates measured from its anchor); cloth that is new, because a panel grew or was just
    // added, continues from the nearest cloth there was, or starts where it
    // is arranged. Panels listed in `moved` were carried through space: their
    // cloth goes with them, drape and all. One whose wrap changed is bent too:
    // each particle goes where `arrangedNow` puts its spot, as far from it (and
    // turned with it) as it hung from the old arrangement. Seams draw closed again from
    // wherever their two sides now are. Pins and a hold are not carried over.
    static Cloth continued(const ClothMesh& mesh, const Placement& arrangedNow, const Cloth& before, const ClothMesh& beforeMesh,
                           const std::vector<Moved>& moved = {});

    // Holds a particle where it is now. `panel` indexes the mesh's panels.
    void pin(uint32_t panel, uint32_t point);

    // Takes hold of a particle and leads it to `to`, as a hand pulling the
    // cloth: it goes there at no more than leadSpeed, and the cloth follows.
    // One particle at a time; holding another lets go of the first.
    // `panel` indexes the mesh's panels; a particle that is not there is ignored.
    void hold(uint32_t panel, uint32_t point, Vec3 to);
    void letGo();
    bool holding() const { return held_.has_value(); }

    // Which panels take no part, by their index in the mesh; all the others
    // do. A panel left out stays exactly where it is: it does not move,
    // nothing touches it and its seams hold nothing. Taken back, it carries on
    // from there, and its seams are drawn closed again. A new cloth leaves
    // none out. A hold on a panel that is left out ends.
    void leaveOut(const std::vector<uint32_t>& panels);

    // Moves time on by `seconds` (a sixtieth by default; values are kept
    // between a 240th and a 30th).
    void step(const Body& body, double seconds = 1.0 / 60.0);

    const Placement& positions() const { return positions_; }
    double time() const { return time_; }
    // The speed of the fastest particle, in cm per second: near zero once
    // the cloth has come to rest.
    double fastest() const;
    // Whether the seams are still closing. Until they have, the cloth is held
    // nearly weightless and slides freely on the body, so that it meets where
    // it was arranged and nothing holds a seam open. Sewing ends when every
    // stitch has closed, or a few seconds after its thread was drawn in if
    // one cannot close (the pieces do not fit).
    bool sewing() const { return !seamsClosed_; }
    // The seams that could not close: those wider than joinedWithin when
    // sewing gave up on them, each listed for as long as it stays that wide
    // (it can close later: its threads keep pulling). Empty while sewing.
    // Seams are drawn in, and listed, anew by continued() and when a panel is
    // taken back into the cloth.
    const std::vector<OpenSeam>& openSeams() const { return open_; }
    // False if any position has stopped being a number.
    bool sound() const;

private:
    struct Link {
        uint32_t a, b;
        double rest;
        double shareA = 0.0, shareB = 0.0;  // how much of a correction each end takes; set at the start of every step
    };
    struct Thread {
        uint32_t a, b;
        double left;  // how much thread is still to be drawn in
        SeamId seam;
        bool holds = true;  // false while a panel it sews is left out
    };

    void share(std::vector<Link>& links, double compliance, double stepSquared) const;
    void solve(const std::vector<Link>& links);

    Fabric fabric_;
    std::vector<uint32_t> firstOf_;  // per panel: its first particle, or none
    std::vector<Vec3> at_, before_, speed_;
    std::vector<double> lightness_;  // one over a particle's mass; zero when it stands still: pinned, held or left out
    std::vector<double> own_;        // per particle: its lightness while it takes part and is not held
    std::vector<char> out_;          // per panel: whether it is left out
    std::vector<Link> edges_, folds_;
    std::vector<Thread> threads_;
    std::vector<uint32_t> near_, firstNear_;  // per particle: the limbs within reach this step
    std::vector<char> nearScan_;              // per particle: whether the scanned surface is within reach this step
    std::vector<double> clearance_;           // per particle: how far the body within reach was when this step began
    std::vector<Vec3> stepFrom_;              // per particle: where it was when this step began

    // Cloth against cloth.
    void findClosePairs(double seconds);
    double apart_ = 0.0;                 // how far apart particles are kept
    std::vector<uint32_t> panelOf_;      // per particle: its panel
    std::vector<Vec2> flat_;             // per particle: where it is in its panel's flat pattern
    std::vector<char> alone_;            // per particle: cloth against cloth passes it by (a stitch holds it, or its panel is left out)
    void findAlone();
    struct Pair {
        uint32_t a, b;
    };
    std::vector<Pair> close_;            // the pairs that could touch during this step
    // The search for them, kept from step to step: each particle's cell, the
    // first of each slot of the hashed table, the particles by slot, and the
    // last particle (plus one) whose search looked through each slot.
    std::vector<std::array<std::int64_t, 3>> cellOf_;
    std::vector<uint32_t> slotFirst_, bySlot_, lookedBy_;
    Placement positions_;
    double time_ = 0.0;
    struct Held {
        uint32_t particle;
        double lightness;  // what it had before it was taken hold of
        Vec3 to;
    };
    std::optional<Held> held_;
    bool seamsClosed_ = false;
    double drawnInAt_ = -1.0;  // when the last thread was drawn in; below zero while some still are
    void checkTheSeams();
    std::vector<OpenSeam> open_;
    void listOpenSeams();     // when sewing ends: those that are open now
    void measureOpenSeams();  // the listed ones again; those that have closed leave the list
};

} // namespace clothcad::sim
