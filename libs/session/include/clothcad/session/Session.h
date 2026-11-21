#pragma once
// One open document and everything worked out from it: the outlines, the
// cloth mesh, where the panels are in space, the body, and the cloth in
// motion. It knows nothing of windows: a window shows what it holds and
// hands it what the user did. The window and the tests both use it.
//
//   Session session(std::move(doc));
//   session.useAvatar(sim::loadObj(path).surface, path);
//   session.editor().change(...);       // every change of the document
//   session.simulate(true);
//   ...each frame:
//   session.update();
//   draw(session.shownMesh(), session.shownPositions(), session.bodySurface());
//
// What it hands out stays valid until the next call that changes it; the
// mesh is shared, so a view may keep it as long as it likes.

#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "clothcad/session/Editor.h"
#include "clothcad/session/Simulation.h"
#include "clothcad/sim/Body.h"
#include "clothcad/sim/Layout.h"
#include "clothcad/sim/Mesh.h"

namespace clothcad::session {

class Session {
public:
    explicit Session(Document document = {}, Threading threading = Threading::OwnThread);

    Editor& editor() { return editor_; }
    const Editor& editor() const { return editor_; }
    const Document& document() const { return editor_.document(); }
    // Another document, with no history and no cloth: opening a file.
    void open(Document document);

    // ---- The mesh and the arrangement ----
    void setParticleDistance(double cm);
    double particleDistance() const { return particleDistance_; }
    // The pattern as triangles. Made again only when the outlines or the
    // particle distance change. With Threading::OwnThread it is made in the
    // background, so while a corner is dragged it may be of outlines a few
    // frames old; it never names a panel the document no longer has.
    const std::shared_ptr<const sim::ClothMesh>& mesh() const { return mesh_; }
    std::uint64_t meshesMade() const { return meshesMade_; }
    // Where the mesh's particles are, arranged in space.
    const sim::Placement& arranged() const { return arranged_; }

    // ---- The body ----
    // An avatar's surface as the body (cm, y up, on the floor, facing +z), as
    // sim::loadObj reads it from `path`; the session reads no files itself.
    // The surface is there at once; working out how to collide with it takes
    // a moment and happens in the background. Until there is one there is no
    // body: the cloth falls to the floor.
    void useAvatar(sim::Surface surface, std::string path);
    bool bodyReady() const { return !preparing_.valid(); }
    const std::string& avatarPath() const { return avatarPath_; }  // where it was read from; empty: no body
    const sim::Surface& bodySurface() const { return bodySurface_; }
    // Whether the cloth meets the body. A hidden body is out of the simulation.
    void setBodyPresent(bool present) { bodyPresent_ = present; }

    // ---- The cloth in motion ----
    // Lets the cloth fall, or holds it where it is. It starts the first time,
    // from where the panels are arranged, once the body is ready.
    void simulate(bool on) { simulating_ = on; }
    bool simulating() const { return simulating_; }
    // Back to the arrangement; the cloth is gone until it is simulated again.
    void resetCloth();
    // Whether there is cloth to show: started and not reset.
    bool clothShown() const { return simulation_->exists() && cloth_.mesh != nullptr; }
    const Simulation::News& cloth() const { return cloth_; }
    // How the cloth runs; changing it resets the cloth.
    void setThreading(Threading threading);
    Threading threading() const { return threading_; }

    // ---- Pulling the cloth by hand ----
    // Takes hold of a particle of the cloth shown and leads it to `to`, as a
    // hand would. While it is held the cloth runs, even when it is not being
    // simulated. `point` indexes the panel's particles in cloth().mesh.
    // A panel carried through space meanwhile keeps the hold; a new mesh ends it.
    void grab(PanelId panel, std::uint32_t point, sim::Vec3 to);
    void pullTo(sim::Vec3 to);
    void letGo();
    bool pulling() const { return held_.has_value(); }

    // What to draw in space: the cloth when there is some, else the panels
    // where they are arranged. The positions belong to that mesh.
    const sim::ClothMesh& shownMesh() const { return clothShown() ? *cloth_.mesh : *mesh_; }
    const sim::Placement& shownPositions() const { return clothShown() ? cloth_.positions : arranged_; }
    // Goes up whenever anything a 3D view draws has changed.
    std::uint64_t sceneVersion() const { return sceneVersion_; }

    // Brings everything up to date with the document and picks up news from
    // the cloth; call it once a frame. With `meshWanted` false the mesh, the
    // arrangement and the cloth are left alone (nothing shows them).
    // Returns a problem to tell the user, if one came up.
    std::optional<std::string> update(bool meshWanted = true);

private:
    // Where a panel is placed, for telling whether it has been placed elsewhere since the cloth took it.
    // Where a panel stands in space, its anchor left out: a moved anchor is a
    // change of shape, not of place.
    struct Placed {
        PanelId panel;
        Arrangement arrangement;
        friend bool operator==(const Placed&, const Placed&) = default;
    };
    std::vector<Placed> placedNow() const;
    sim::Body bodyInSimulation() const { return bodyPresent_ ? body_ : sim::Body{}; }
    bool remesh(bool now);
    bool takeMeshIfMade(bool wait);
    bool fits(const sim::ClothMesh& mesh) const;

    Editor editor_;

    double particleDistance_ = 2.0;
    std::shared_ptr<const sim::ClothMesh> mesh_ = std::make_shared<const sim::ClothMesh>();
    std::uint64_t meshedShapes_ = 0;
    double meshedDistance_ = 0.0;
    std::uint64_t meshesMade_ = 0;
    sim::Placement arranged_;
    std::vector<Placed> arrangedFrom_;
    // A mesh being made in the background, and what it is made from.
    struct Meshing {
        std::future<std::shared_ptr<const sim::ClothMesh>> mesh;
        std::uint64_t shapes = 0;
        double distance = 0.0;
    };
    Meshing meshing_;
    Threading threading_;

    sim::Body body_;
    sim::Surface bodySurface_;
    std::string avatarPath_;
    std::future<sim::Body> preparing_;
    bool bodyPresent_ = true;
    struct BodyInUse {
        bool present = true;
        const sim::DistanceField* scanned = nullptr;
        std::size_t limbs = 0;
        friend bool operator==(const BodyInUse&, const BodyInUse&) = default;
    };
    BodyInUse bodyInUse_;

    std::unique_ptr<Simulation> simulation_;
    Simulation::News cloth_;
    bool simulating_ = false;
    std::vector<Placed> clothPlaced_;  // where each panel stood the last time the cloth was told
    struct Held {
        std::uint32_t panel, point;  // panel indexes cloth_.mesh's panels
    };
    std::optional<Held> held_;

    // The drape as it was when each state of the history was left, so that
    // undo and redo bring it back (keyed by Editor::step).
    struct Drape {
        std::shared_ptr<const sim::ClothMesh> mesh;
        sim::Placement positions;
        double time = 0.0;
        bool sewing = false;
    };
    std::map<std::uint64_t, Drape> drapes_;
    std::uint64_t shownStep_ = 0;

    std::uint64_t sceneVersion_ = 1;
};

} // namespace clothcad::session
