#include "clothcad/session/Session.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "clothcad/core/Drawing.h"

namespace clothcad::session {

Session::Session(Document document, Threading threading)
    : editor_(std::move(document)), threading_(threading), simulation_(std::make_unique<Simulation>(threading)) {}

void Session::open(Document document) {
    resetCloth();
    if (meshing_.mesh.valid()) meshing_.mesh.wait();  // of the old document; dropped
    meshing_ = {};
    editor_.replace(std::move(document));
}

void Session::setParticleDistance(double cm) { particleDistance_ = std::max(cm, 0.05); }

// ---- The body --------------------------------------------------------------------

void Session::useMannequin(sim::MannequinSize size) {
    resetCloth();
    preparing_ = {};
    avatarPath_.clear();
    body_ = sim::mannequin(size);
    bodySurface_ = sim::surfaceOf(body_);
    ++sceneVersion_;
}

std::expected<void, std::string> Session::loadAvatar(const std::string& path) {
    sim::LoadedSurface loaded = sim::loadObj(path);
    if (!loaded.problem.empty()) return std::unexpected(std::move(loaded.problem));
    resetCloth();
    bodySurface_ = std::move(loaded.surface);
    body_ = sim::Body{};
    avatarPath_ = path;
    // The background work has its own copy of the surface: nothing is shared with it.
    preparing_ = std::async(std::launch::async, [surface = bodySurface_] { return sim::bodyFrom(surface); });
    ++sceneVersion_;
    return {};
}

// ---- The cloth ---------------------------------------------------------------------

void Session::resetCloth() {
    simulation_->stop();
    simulation_->fetch(cloth_);
    simulating_ = false;
    clothPlaced_.clear();
    ++sceneVersion_;
}

void Session::setThreading(Threading threading) {
    resetCloth();
    threading_ = threading;
    simulation_ = std::make_unique<Simulation>(threading);
    cloth_ = {};
}

// ---- The mesh ------------------------------------------------------------------------

// The mesh follows the outlines: it is made again only when they were
// evaluated again or the particle distance changed. Where a panel is, on the
// table or in space, does not change its mesh. On its own thread a mesh takes
// 8 to 50 ms to make, which a frame cannot wait for while a corner is dragged:
// the last one made is shown until the next is done, and the next is always
// made from the newest outlines. Returns whether the mesh changed.
bool Session::remesh() {
    const bool wait = threading_ == Threading::OneStepPerAdvance;
    bool changed = takeMeshIfMade(wait);
    // A panel taken away must leave the mesh at once: everything that shows
    // the mesh looks its panels up in the document.
    if (!fits(*mesh_)) {
        if (meshing_.mesh.valid()) changed |= takeMeshIfMade(true);
        if (!fits(*mesh_)) {
            mesh_ = std::make_shared<const sim::ClothMesh>(sim::meshOf(document(), editor_.shapes(), {.particleDistance = particleDistance_}));
            meshedShapes_ = editor_.shapesVersion();
            meshedDistance_ = particleDistance_;
            ++meshesMade_;
            changed = true;
        }
    }
    if (!meshing_.mesh.valid() && (meshedShapes_ != editor_.shapesVersion() || meshedDistance_ != particleDistance_)) {
        // The work has its own copies; nothing is shared with it but the outlines, which never change.
        meshing_.shapes = editor_.shapesVersion();
        meshing_.distance = particleDistance_;
        meshing_.mesh = std::async(wait ? std::launch::deferred : std::launch::async,
                                   [doc = document(), shapes = editor_.sharedShapes(), distance = particleDistance_] {
                                       return std::make_shared<const sim::ClothMesh>(sim::meshOf(doc, *shapes, {.particleDistance = distance}));
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
    meshedDistance_ = meshing_.distance;
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
        const Panel& panel = doc.get(panelMesh.panel);
        // The table only places a panel that has not been arranged in space.
        const bool onTable = !panel.arrangement.placed;
        placed.push_back({panelMesh.panel, panel.arrangement, onTable ? panel.x : 0.0, onTable ? panel.y : 0.0, onTable ? panel.angle : 0.0});
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

    const bool remeshed = remesh();
    // Where its particles are arranged is worked out again only when the mesh or a placement changed.
    std::vector<Placed> now = placedNow();
    if (remeshed || now != arrangedFrom_) {
        arranged_ = sim::arranged(document(), *mesh_);
        arrangedFrom_ = now;
        ++sceneVersion_;
    }

    // The cloth follows the pattern: when the mesh has changed it is carried
    // over and stays draped; a panel placed somewhere else starts again from there.
    if (simulation_->exists()) {
        std::vector<PanelId> placeAgain;
        for (const Placed& placed : now) {
            const auto was = std::find_if(clothPlaced_.begin(), clothPlaced_.end(), [&](const Placed& p) { return p.panel == placed.panel; });
            if (was != clothPlaced_.end() && !(*was == placed)) placeAgain.push_back(placed.panel);
        }
        if (remeshed || !placeAgain.empty()) {
            simulation_->follow(mesh_, arranged_, std::move(placeAgain));
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
    simulation_->run(simulating_);
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
