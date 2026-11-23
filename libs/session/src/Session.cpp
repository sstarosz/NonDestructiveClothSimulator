#include "clothcad/session/Session.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "clothcad/core/Drawing.h"

namespace clothcad::session {

namespace {
constexpr std::size_t keptDrapes = 64;  // about 70 kB each at 2,800 particles
}

Session::Session(Document document, Threading threading)
    : editor_(std::move(document)), threading_(threading), simulation_(std::make_unique<Simulation>(threading)) {}

void Session::open(Document document) {
    resetCloth();
    if (meshing_.mesh.valid()) meshing_.mesh.wait();  // of the old document; dropped
    meshing_ = {};
    drapes_.clear();
    editor_.replace(std::move(document));
}

// ---- The body --------------------------------------------------------------------

void Session::useAvatar(sim::Surface surface, std::string path) {
    resetCloth();
    bodySurface_ = std::move(surface);
    body_ = sim::Body{};
    avatarPath_ = std::move(path);
    // The background work has its own copy of the surface: nothing is shared with it.
    preparing_ = std::async(std::launch::async, [surface = bodySurface_] { return sim::bodyFrom(surface); });
    ++sceneVersion_;
}

// ---- The cloth ---------------------------------------------------------------------

void Session::resetCloth() {
    held_.reset();
    simulation_->stop();
    simulation_->fetch(cloth_);
    simulating_ = false;
    clothPlaced_.clear();
    ++sceneVersion_;
}

void Session::grab(PanelId panel, std::uint32_t point, sim::Vec3 to) {
    if (!clothShown()) return;
    const std::vector<sim::PanelMesh>& panels = cloth_.mesh->panels;
    const auto found = std::find_if(panels.begin(), panels.end(), [&](const sim::PanelMesh& p) { return p.panel == panel; });
    if (found == panels.end() || point >= found->points.size()) return;
    held_ = Held{static_cast<std::uint32_t>(found - panels.begin()), point};
    simulation_->hold(held_->panel, point, to);
}

void Session::pullTo(sim::Vec3 to) {
    if (held_) simulation_->hold(held_->panel, held_->point, to);
}

void Session::letGo() {
    if (!held_) return;
    held_.reset();
    simulation_->letGo();
}

void Session::setThreading(Threading threading) {
    resetCloth();
    threading_ = threading;
    simulation_ = std::make_unique<Simulation>(threading);
    cloth_ = {};
}

// ---- The mesh ------------------------------------------------------------------------

// The mesh follows the outlines: it is made again only when they were
// evaluated again. Where a panel is, on the
// table or in space, does not change its mesh. On its own thread a mesh takes
// 8 to 50 ms to make, which a frame cannot wait for while a corner is dragged:
// the last one made is shown until the next is done, and the next is always
// made from the newest outlines. Returns whether the mesh changed.
bool Session::remesh(bool now) {
    const bool wait = now || threading_ == Threading::OneStepPerAdvance;
    bool changed = takeMeshIfMade(wait);
    // A panel taken away must leave the mesh at once: everything that shows
    // the mesh looks its panels up in the document.
    if (!fits(*mesh_)) {
        if (meshing_.mesh.valid()) changed |= takeMeshIfMade(true);
        if (!fits(*mesh_)) {
            mesh_ = std::make_shared<const sim::ClothMesh>(sim::meshOf(document(), editor_.shapes()));
            meshedShapes_ = editor_.shapesVersion();
            ++meshesMade_;
            changed = true;
        }
    }
    if (!meshing_.mesh.valid() && meshedShapes_ != editor_.shapesVersion()) {
        // The work has its own copies; nothing is shared with it but the outlines, which never change.
        meshing_.shapes = editor_.shapesVersion();
        meshing_.mesh = std::async(wait ? std::launch::deferred : std::launch::async,
                                   [doc = document(), shapes = editor_.sharedShapes()] {
                                       return std::make_shared<const sim::ClothMesh>(sim::meshOf(doc, *shapes));
                                   });
        changed |= takeMeshIfMade(wait);
    }
    return changed;
}

bool Session::takeMeshIfMade(bool wait) {
    if (!meshing_.mesh.valid()) return false;
    if (!wait && meshing_.mesh.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    std::shared_ptr<const sim::ClothMesh> made = meshing_.mesh.get();
    // Made before a panel was taken away: of no use; the next one is made at once.
    if (!fits(*made)) return false;
    mesh_ = std::move(made);
    meshedShapes_ = meshing_.shapes;
    ++meshesMade_;
    return true;
}

bool Session::fits(const sim::ClothMesh& mesh) const {
    return std::all_of(mesh.panels.begin(), mesh.panels.end(), [&](const sim::PanelMesh& panel) { return document().contains(panel.panel); });
}

std::vector<Session::Placed> Session::placedNow() const {
    std::vector<Placed> placed;
    const Document& doc = document();
    for (const sim::PanelMesh& panelMesh : mesh_->panels) {
        if (!doc.contains(panelMesh.panel)) continue;
        // The table places a panel that has no place in space of its own yet.
        Arrangement where = sim::arrangementOf(doc, panelMesh);
        where.anchor.reset();
        where.placed = true;  // standing where the table has it, or given that same place: no move
        placed.push_back({panelMesh.panel, where});
    }
    return placed;
}

std::optional<std::string> Session::update(bool meshWanted) {
    std::optional<std::string> problem;
    if (preparing_.valid() && preparing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) body_ = preparing_.get();
    if (!bodyReady()) simulating_ = false;
    if (!meshWanted) {
        simulation_->run(false);
        return problem;
    }

    // Undo and redo bring the drape back: the cloth is kept for each state of
    // the history as it was left, and put back when that state comes back.
    std::optional<Drape> drapeBack;
    if (editor_.step() != shownStep_) {
        if (clothShown()) {
            drapes_[shownStep_] = Drape{cloth_.mesh, cloth_.positions, cloth_.time, cloth_.sewing};
            while (drapes_.size() > keptDrapes) drapes_.erase(drapes_.begin());  // the oldest first
        }
        if (const auto kept = drapes_.find(editor_.step()); kept != drapes_.end() && simulation_->exists()) drapeBack = kept->second;
        shownStep_ = editor_.step();
    }
    // A drape brought back is put on a mesh of the same outlines: made now, not in the background.
    const bool remeshed = remesh(drapeBack.has_value());
    // Where its particles are arranged is worked out again only when the mesh or a placement changed.
    std::vector<Placed> now = placedNow();
    if (remeshed || now != arrangedFrom_) {
        arranged_ = sim::arranged(document(), *mesh_);
        arrangedFrom_ = now;
        ++sceneVersion_;
    }

    if (drapeBack) {
        held_.reset();
        simulation_->restore(drapeBack->mesh, std::move(drapeBack->positions), drapeBack->time, drapeBack->sewing);
        clothPlaced_ = now;  // placed as it was then
    }

    // The cloth follows the pattern: when the mesh has changed it is carried
    // over and stays draped; a panel placed somewhere else is carried there,
    // drape and all. Nothing here starts a panel again.
    if (simulation_->exists()) {
        std::vector<sim::Moved> moved;
        for (const Placed& placed : now) {
            const auto was = std::find_if(clothPlaced_.begin(), clothPlaced_.end(), [&](const Placed& p) { return p.panel == placed.panel; });
            if (was != clothPlaced_.end() && !(*was == placed)) moved.push_back({placed.panel, sim::Motion{.before = was->arrangement, .after = placed.arrangement}});
        }
        if (remeshed || !moved.empty()) {
            if (remeshed) held_.reset();  // the simulation lets go too: its particles are numbered anew
            simulation_->follow(mesh_, arranged_, std::move(moved));
            clothPlaced_ = std::move(now);
        }
    }
    // The body the cloth meets: none while it is hidden.
    const BodyInUse bodyNow{bodyPresent_, body_.scanned.get(), body_.parts.size()};
    if (simulation_->exists() && !(bodyNow == bodyInUse_)) {
        simulation_->setBody(bodyInSimulation());
        bodyInUse_ = bodyNow;
    }
    if (simulating_ && !simulation_->exists()) {
        simulation_->start(mesh_, arranged_, bodyInSimulation());
        clothPlaced_ = placedNow();
        bodyInUse_ = bodyNow;
    }
    simulation_->run(simulating_ || held_.has_value());
    simulation_->advance();
    if (simulation_->fetch(cloth_)) {
        ++sceneVersion_;
        if (cloth_.failed) {  // should never happen; if it does, stop instead of drawing nonsense
            resetCloth();
            problem = "The simulation went wrong and was reset.";
        }
    }
    return problem;
}

} // namespace clothcad::session
