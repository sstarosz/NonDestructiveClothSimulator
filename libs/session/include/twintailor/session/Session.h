#pragma once
// One open document and everything worked out from it: the outlines, the
// cloth mesh, where the panels are in space, the body, and the cloth in
// motion. It knows nothing of windows: a window shows what it holds and
// hands it what the user did. The window and the tests both use it.
//
//   Session session(std::move(doc));
//   session.run(SetField{.field = waist, .value = 80_cm});  // a command (Commands.h), one undo step
//   session.undo();
//   session.simulate(true);
//   ...each frame:
//   session.update();
//   draw(session.shownMesh(), session.shownPositions(), session.bodySurface());
//
// The document is changed by commands run here, and in no other way: the
// editor that holds it is handed out for reading.
//
// What it hands out stays valid until the next call that changes it; the
// mesh is shared, so a view may keep it as long as it likes.

#include <cstdint>
#include <expected>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "twintailor/core/File.h"
#include "twintailor/session/Editor.h"
#include "twintailor/session/Simulation.h"
#include "twintailor/sim/Body.h"
#include "twintailor/sim/Export.h"
#include "twintailor/sim/Layout.h"
#include "twintailor/sim/Mesh.h"

namespace twintailor::session {

class Session {
public:
    explicit Session(Document document = {}, Threading threading = Threading::OwnThread);
    ~Session();  // a body still being worked out is given up

    // The outlines, the history and what else there is to read of the document.
    const Editor& editor() const { return editor_; }
    const Document& document() const { return editor_.document(); }

    // ---- Commands and edits (Commands.h) ----
    // As the editor's (Editor::run, begin, preview, commit, cancel). A pull of
    // the cloth is run only as a preview; the particle is let go when its edit
    // is over.
    template <DocumentChange C>
    std::expected<typename C::Done, std::string> run(const C& command) {
        auto done = editor_.run(command);
        letGoAfterThePull();
        return done;
    }
    Edit begin();
    template <DocumentChange C>
    std::expected<typename C::Done, std::string> run(Edit edit, const C& command) {
        return editor_.run(edit, command);
    }
    template <DocumentChange C>
    std::expected<typename C::Done, std::string> preview(Edit edit, const C& command) {
        return editor_.preview(edit, command);
    }
    // Takes hold of a particle of the cloth shown, or leads the one held, as a
    // hand would; `command.panel` and `.point` matter only as it takes hold.
    // While it is held the cloth runs, even when it is not being simulated.
    // False: there is nothing to take hold of, the hold is over (a new mesh, a
    // reset), or the edit is.
    bool preview(Edit edit, const PullCloth& command);
    // Takes the cloth off and stops it. False: there was no cloth, and there is
    // no undo step.
    bool run(const ResetCloth& command);
    void commit(Edit edit);
    // The document and the drape are as before the edit. Says whether there was
    // anything to take back.
    bool cancel(Edit edit);
    bool editing(Edit edit) const { return editor_.editing(edit); }
    bool pulling() const { return held_.has_value(); }
    // One step back or forth in the history, as the editor's: an open edit ends
    // first, and the drape comes back at the next update(). False: there was no step.
    bool undo();
    bool redo();
    // Another document, with no history: opening a file. The scene is as
    // `state` leaves it: its drape hangs again, paused. It starts saved.
    void open(Document document, SceneState state = {});

    // ---- Saving ----
    // How the scene is now, for a file: what is hidden and frozen, the body's
    // looks and the cloth as it hangs. None of it is an undo step.
    SceneState state() const;
    // Whether the document or that scene differs from what was last saved:
    // a change that is undone, or a panel hidden and shown again, is none.
    bool unsaved() const;
    // The document and the scene as they are now have been written to a file.
    void markSaved();
    // No file holds them: a recovered copy.
    void markUnsaved() { editor_.markUnsaved(); }

    // ---- The mesh and the arrangement ----
    // The pattern as triangles, each panel as fine as its own particle
    // distance says. Made again only when the outlines (or a distance, which
    // is part of the document) change. With Threading::OwnThread it is made in the
    // background, so while a corner is dragged it may be of outlines a few
    // frames old; it never names a panel the document no longer has.
    const std::shared_ptr<const sim::ClothMesh>& mesh() const { return mesh_; }
    std::uint64_t meshesMade() const { return meshesMade_; }
    // Where the mesh's particles are, arranged in space.
    const sim::Placement& arranged() const { return arranged_; }

    // ---- The body ----
    // The body is the document's avatar (Document::setAvatar); the session reads
    // no files. Its surface is there as soon as the session has seen it: at once
    // for a document that is opened, at the next update() after a change.
    // Working out how to collide with it takes a moment and happens in the
    // background; until then, and with no avatar, there is nothing to collide
    // with and the cloth falls to the floor. Another body ends the cloth; undo
    // and redo bring back the drape that hung on the body they come back to.
    bool bodyReady() const { return !preparing_.valid() && !bodyProblem_ && bodyOf_ == document().avatar(); }
    // Why there will be no body to collide with: working it out failed, out of
    // memory most likely. The cloth waits until the document has another avatar.
    const std::optional<std::string>& bodyProblem() const { return bodyProblem_; }
    // How far working out the body has got, from 0 to 1; empty while none is
    // being worked out. Another avatar gives the work up at once and starts
    // again: nothing waits for a body that is no longer wanted.
    std::optional<double> bodyProgress() const;
    const sim::Surface& bodySurface() const { return *bodySurface_; }
    // Whether the cloth meets the body. A hidden body is out of the simulation.
    void setBodyPresent(bool present);
    bool bodyPresent() const { return bodyPresent_; }
    // Drawn so that cloth inside it shows; the simulation is the same.
    void setBodySeeThrough(bool seeThrough);
    bool bodySeeThrough() const { return bodySeeThrough_; }
    // A body shown in place of the avatar's while another is being chosen. It is
    // only drawn: the document, the cloth and what it collides with stay as they
    // are. Null: the avatar's own again. Opening a document ends it.
    void previewBody(std::shared_ptr<const sim::Surface> body);
    bool previewingBody() const { return preview_ != nullptr; }
    // What to draw as the body: the one being chosen, else bodySurface(). As a
    // snapshot, which a view may keep: another body is another one.
    const std::shared_ptr<const sim::Surface>& sharedBody() const { return preview_ ? preview_ : bodySurface_; }
    const sim::Surface& shownBody() const { return *sharedBody(); }

    // ---- Panels hidden and deactivated, seams hidden ----
    // None of this changes the document, is an undo step or starts a drape
    // again; it is saved with the file (state()). It is forgotten for a panel or
    // seam that is gone.
    // A hidden panel is not drawn in 3D; it takes part in the cloth as before.
    void setPanelShown(PanelId panel, bool shown);
    bool panelShown(PanelId panel) const;
    const std::vector<PanelId>& hiddenPanels() const { return hidden_; }
    // A deactivated panel takes no part in the cloth: it stays where it is,
    // nothing touches it and its seams hold nothing (see sim::Cloth::leaveOut).
    void setPanelActive(PanelId panel, bool active);
    bool panelActive(PanelId panel) const;
    const std::vector<PanelId>& inactivePanels() const { return inactive_; }
    // A hidden seam has no lines in 3D; it sews as before.
    void setSeamShown(SeamId seam, bool shown);
    bool seamShown(SeamId seam) const;
    const std::vector<SeamId>& hiddenSeams() const { return hiddenSeams_; }

    // ---- The cloth in motion ----
    // Lets the cloth fall, or holds it where it is. It starts the first time,
    // from where the panels are arranged. While a body is being worked out the
    // cloth waits, and what was asked is kept: it runs once the body is ready.
    void simulate(bool on) { simulating_ = on; }
    bool simulating() const { return simulating_; }
    // Whether there is cloth to show: started and not reset.
    bool clothShown() const { return simulation_->exists() && cloth_.mesh != nullptr; }
    const Simulation::News& cloth() const { return cloth_; }
    // How the cloth runs; changing it resets the cloth.
    void setThreading(Threading threading);
    Threading threading() const { return threading_; }

    // What to draw in space: the cloth when there is some, else the panels
    // where they are arranged. The positions belong to that mesh.
    const sim::ClothMesh& shownMesh() const { return clothShown() ? *cloth_.mesh : *mesh_; }
    const sim::Placement& shownPositions() const { return clothShown() ? cloth_.positions : arranged_; }
    // Goes up whenever anything a 3D view draws has changed.
    std::uint64_t sceneVersion() const { return sceneVersion_; }

    // ---- Export ----
    // Whether the document has a panel cut from cloth, one there is something to export of.
    bool hasGarment() const;
    // The garment as the text of an OBJ file (sim::objOf): the cloth as it
    // hangs, or with none the panels where they are arranged; hidden panels are
    // left out, deactivated ones are written as they stand. It first waits for
    // the mesh and the cloth to catch up with the document, so that what is
    // written is the pattern as it is now, also when nothing shows it in space.
    // The cloth is not stepped. On failure, why, and what to do.
    std::expected<sim::Obj, std::string> obj();

    // Brings everything up to date with the document and picks up news from
    // the cloth; call it once a frame. With `meshWanted` false the mesh, the
    // arrangement and the cloth are left alone (nothing shows them).
    // Returns a problem to tell the user, if one came up: work in the
    // background that failed (the body, the mesh, the cloth) comes out here,
    // once, and the rest goes on.
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
    void followAvatar();
    void keepDrape();
    void dropCloth();  // stops the cloth and forgets it: no undo step
    void hang(const SavedDrape& drape);
    bool clothAsSaved() const;
    bool remesh(bool now);
    bool takeMeshIfMade(bool wait);
    bool fits(const sim::ClothMesh& mesh) const;
    // The parts of update(): the body and what is gone; the mesh, the
    // arrangement and the cloth, waiting for the mesh if `now`; the news.
    void followBody();
    void followShapes(bool now);
    std::optional<std::string> takeNews();

    Editor editor_;

    std::shared_ptr<const sim::ClothMesh> mesh_ = std::make_shared<const sim::ClothMesh>();
    std::uint64_t meshedShapes_ = 0;
    std::uint64_t meshesMade_ = 0;
    sim::Placement arranged_;
    std::vector<Placed> arrangedFrom_;
    // A mesh being made in the background, and what it is made from.
    struct Meshing {
        std::future<std::shared_ptr<const sim::ClothMesh>> mesh;
        std::uint64_t shapes = 0;
    };
    Meshing meshing_;
    Threading threading_;

    std::shared_ptr<const Avatar> bodyOf_;  // the avatar the body is of
    sim::Body body_;
    std::shared_ptr<const sim::Surface> bodySurface_ = std::make_shared<const sim::Surface>();
    std::shared_ptr<const sim::Surface> preview_;  // shown instead of it
    std::future<sim::Body> preparing_;
    std::shared_ptr<sim::Progress> progress_;  // of the body being worked out; the work holds it too
    std::optional<std::string> bodyProblem_;   // why that work failed
    std::optional<std::string> toTell_;        // a problem update() has still to hand out
    std::optional<std::uint64_t> meshFailed_;  // the outlines no mesh could be made of
    bool bodyPresent_ = true;
    bool bodySeeThrough_ = false;
    struct BodyInUse {
        bool present = true;
        const sim::DistanceField* scanned = nullptr;
        std::size_t limbs = 0;
        friend bool operator==(const BodyInUse&, const BodyInUse&) = default;
    };
    BodyInUse bodyInUse_;

    void forgetTheGone();
    std::vector<PanelId> hidden_, inactive_;
    std::vector<SeamId> hiddenSeams_;

    std::unique_ptr<Simulation> simulation_;
    Simulation::News cloth_;
    bool simulating_ = false;
    std::vector<Placed> clothPlaced_;  // where each panel stood the last time the cloth was told
    struct Held {
        std::uint32_t panel, point;  // panel indexes cloth_.mesh's panels
    };
    std::optional<Held> held_;
    std::optional<Edit> pull_;  // the edit the cloth is being pulled in
    void release();             // lets go of the held particle
    void letGoAfterThePull();   // the same, once that edit is over

    // The cloth as it was when each state of the history was left, so that undo
    // and redo bring it back (keyed by Editor::step). Empty: a Reset left that
    // state with no cloth.
    struct Drape {
        std::shared_ptr<const sim::ClothMesh> mesh;
        sim::Placement positions;
        double time = 0.0;
        bool sewing = false;
        std::vector<PanelId> inactive;  // the panels that took no part in it
    };
    std::map<std::uint64_t, std::optional<Drape>> drapes_;
    std::uint64_t shownStep_ = 0;

    std::uint64_t sceneVersion_ = 1;

    // The scene as it was last saved, to tell whether it has changed since.
    struct Saved {
        std::vector<PanelId> frozen, hidden;
        std::vector<SeamId> hiddenSeams;
        bool bodyPresent = true, bodySeeThrough = false;
        bool cloth = false;  // whether there was cloth
        double time = 0.0;
        sim::Placement positions;
    };
    Saved saved_;
    bool clothMoved_ = false;  // the cloth differs from the saved one: worked out when news of it comes
};

} // namespace twintailor::session
