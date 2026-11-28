#include "twintailor/session/Session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "twintailor/core/Drawing.h"
#include "twintailor/core/File.h"

namespace twintailor::session {

namespace {
constexpr std::size_t keptDrapes = 64;  // about 70 kB each at 2,800 particles

template <class Id>
bool listed(const std::vector<Id>& list, Id id) { return std::find(list.begin(), list.end(), id) != list.end(); }

// Puts `id` into the list or takes it out; whether that changed anything.
template <class Id>
bool keep(std::vector<Id>& list, Id id, bool in) {
    if (listed(list, id) == in) return false;
    if (in) list.push_back(id);
    else std::erase(list, id);
    return true;
}
}  // namespace

Session::Session(Document document, Threading threading)
    : editor_(std::move(document)), threading_(threading), simulation_(std::make_unique<Simulation>(threading)) {
    followAvatar();
}

void Session::keepLog(std::string file, std::string heading) { log_.emplace(std::move(file), std::move(heading)); }

void Session::note(std::string_view what) {
    if (log_) log_->note(what);
}

void Session::endLog() {
    if (!log_) return;
    const std::string file = log_->file();
    log_.reset();
    std::error_code ignored;
    std::filesystem::remove(filePath(file), ignored);
}

void Session::noteCommand(const std::type_info& command) { note(plainName(command)); }

void Session::notePreview(Edit edit, const std::type_info& command) {
    const std::pair<std::uint64_t, std::type_index> shown{edit.number, command};
    if (!log_ || previewed_ == shown) return;
    previewed_ = shown;
    log_->note(std::string(plainName(command)) + ", previewed");
}

void Session::open(Document document, SceneState state) {
    note("Open a pattern");
    dropCloth();
    if (meshing_.mesh.valid()) meshing_.mesh.wait();  // of the old document; dropped
    meshing_ = {};
    drapes_.clear();
    preview_.reset();
    editor_.replace(std::move(document));
    const auto gone = [&](auto id) { return !this->document().contains(id); };
    hidden_ = std::move(state.hidden);
    inactive_ = std::move(state.frozen);
    hiddenSeams_ = std::move(state.hiddenSeams);
    std::erase_if(hidden_, gone);
    std::erase_if(inactive_, gone);
    std::erase_if(hiddenSeams_, gone);
    bodyPresent_ = !state.bodyHidden;
    bodySeeThrough_ = state.bodySeeThrough;
    simulation_->leaveOut(inactive_);
    followAvatar();
    shownStep_ = editor_.step();
    if (state.drape) hang(*state.drape);
    markSaved();
    ++sceneVersion_;
}

// ---- Saving ------------------------------------------------------------------------

SceneState Session::state() const {
    SceneState state{.frozen = inactive_, .hidden = hidden_, .hiddenSeams = hiddenSeams_, .bodyHidden = !bodyPresent_, .bodySeeThrough = bodySeeThrough_,
                     .drape = std::nullopt};
    const auto gone = [&](auto id) { return !document().contains(id); };
    std::erase_if(state.frozen, gone);  // a panel just removed, not yet forgotten
    std::erase_if(state.hidden, gone);
    std::erase_if(state.hiddenSeams, gone);
    if (!clothShown()) return state;
    SavedDrape drape{.time = cloth_.time, .sewing = cloth_.sewing, .panels = {}};
    const std::vector<sim::PanelMesh>& panels = cloth_.mesh->panels;
    for (std::size_t p = 0; p < panels.size() && p < cloth_.positions.size(); ++p) {
        const sim::PanelMesh& panel = panels[p];
        const std::vector<Vec3>& at = cloth_.positions[p];
        if (panel.triangles.empty() || at.size() != panel.points.size() || gone(panel.panel)) continue;
        drape.panels.push_back({.panel = panel.panel, .anchor = panel.anchor, .flat = panel.points, .at = at, .triangles = panel.triangles});
    }
    if (!drape.panels.empty()) state.drape = std::move(drape);
    return state;
}

// Hangs a drape from a file on the document's mesh, paused. Where a panel is
// meshed as it was when it was saved, every particle is exactly where it was;
// where it is not (a later version may mesh otherwise), the cloth is carried
// over as it is after a change of the pattern.
void Session::hang(const SavedDrape& drape) {
    mesh_ = std::make_shared<const sim::ClothMesh>(sim::meshOf(document(), editor_.shapes()));
    meshedShapes_ = editor_.shapesVersion();
    ++meshesMade_;
    arranged_ = sim::arranged(document(), *mesh_);
    arrangedFrom_ = placedNow();

    sim::ClothMesh then;
    sim::Placement thenAt;
    for (const DrapedPanel& draped : drape.panels) {
        if (draped.triangles.empty() || draped.at.size() != draped.flat.size()) continue;
        sim::PanelMesh panel;
        panel.panel = draped.panel;
        panel.points = draped.flat;
        panel.triangles = draped.triangles;
        panel.anchor = draped.anchor;
        then.panels.push_back(std::move(panel));
        thenAt.push_back(draped.at);
    }
    if (then.panels.empty()) return;

    sim::Placement positions = arranged_;
    positions.resize(mesh_->panels.size());
    std::vector<std::size_t> otherwise;  // panels meshed differently now
    for (std::size_t p = 0; p < mesh_->panels.size(); ++p) {
        const sim::PanelMesh& now = mesh_->panels[p];
        for (std::size_t o = 0; o < then.panels.size(); ++o) {
            const sim::PanelMesh& was = then.panels[o];
            if (!(was.panel == now.panel)) continue;
            const bool same = was.triangles == now.triangles && was.points.size() == now.points.size() &&
                              std::equal(was.points.begin(), was.points.end(), now.points.begin(),
                                         [](const Vec2& a, const Vec2& b) { return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9; });
            if (same) positions[p] = thenAt[o];
            else otherwise.push_back(p);
            break;
        }
    }
    if (!otherwise.empty()) {
        const sim::Cloth old = sim::Cloth::resumed(then, thenAt, drape.time, true);
        const sim::Cloth carried = sim::Cloth::continued(*mesh_, arranged_, old, then);
        for (std::size_t p : otherwise) positions[p] = carried.positions()[p];
    }

    simulation_->resume(mesh_, positions, drape.time, drape.sewing, bodyInSimulation());
    bodyInUse_ = {bodyPresent_, body_.scanned.get(), body_.parts.size()};
    clothPlaced_ = arrangedFrom_;
    // Shown at once, before the cloth's thread has taken it.
    cloth_.mesh = mesh_;
    cloth_.positions = std::move(positions);
    cloth_.time = drape.time;
    cloth_.sewing = drape.sewing;
    cloth_.failed = false;
}

bool Session::clothAsSaved() const {
    if (!clothShown() || !saved_.cloth) return clothShown() == saved_.cloth;
    if (cloth_.time != saved_.time || cloth_.positions.size() != saved_.positions.size()) return false;
    for (std::size_t p = 0; p < saved_.positions.size(); ++p) {
        if (cloth_.positions[p].size() != saved_.positions[p].size()) return false;
        for (std::size_t i = 0; i < saved_.positions[p].size(); ++i) {
            // Far below what can be seen: a drape carried over to the same mesh comes out this near.
            const Vec3 off = cloth_.positions[p][i] - saved_.positions[p][i];
            if (std::abs(off.x) > 1e-6 || std::abs(off.y) > 1e-6 || std::abs(off.z) > 1e-6) return false;
        }
    }
    return true;
}

bool Session::unsaved() const {
    const auto same = [](const auto& a, const auto& b) { return std::is_permutation(a.begin(), a.end(), b.begin(), b.end()); };
    return editor_.unsaved() || bodyPresent_ != saved_.bodyPresent || bodySeeThrough_ != saved_.bodySeeThrough || !same(inactive_, saved_.frozen) ||
           !same(hidden_, saved_.hidden) || !same(hiddenSeams_, saved_.hiddenSeams) || clothShown() != saved_.cloth || clothMoved_;
}

void Session::markSaved() {
    editor_.markSaved();
    saved_ = {inactive_, hidden_, hiddenSeams_, bodyPresent_, bodySeeThrough_, clothShown(), 0.0, {}};
    if (saved_.cloth) {
        saved_.time = cloth_.time;
        saved_.positions = cloth_.positions;
    }
    clothMoved_ = false;
}

// ---- Panels hidden and deactivated, seams hidden ---------------------------------

void Session::setPanelShown(PanelId panel, bool shown) {
    if (!document().contains(panel) || !keep(hidden_, panel, !shown)) return;
    note(shown ? "Show a panel" : "Hide a panel");
    ++sceneVersion_;
}

bool Session::panelShown(PanelId panel) const { return !listed(hidden_, panel); }

void Session::setPanelActive(PanelId panel, bool active) {
    if (!document().contains(panel) || !keep(inactive_, panel, !active)) return;
    note(active ? "Activate a panel" : "Deactivate a panel");
    if (!active && held_ && cloth_.mesh->panels[held_->panel].panel == panel) release();
    simulation_->leaveOut(inactive_);
    ++sceneVersion_;
}

bool Session::panelActive(PanelId panel) const { return !listed(inactive_, panel); }

void Session::setSeamShown(SeamId seam, bool shown) {
    if (!document().contains(seam) || !keep(hiddenSeams_, seam, !shown)) return;
    note(shown ? "Show a seam" : "Hide a seam");
    ++sceneVersion_;
}

bool Session::seamShown(SeamId seam) const { return !listed(hiddenSeams_, seam); }

// A panel or seam that is gone takes with it what was set for it: its id may be given out again.
void Session::forgetTheGone() {
    const auto gone = [&](auto id) { return !document().contains(id); };
    if (std::erase_if(hidden_, gone) > 0) ++sceneVersion_;
    if (std::erase_if(hiddenSeams_, gone) > 0) ++sceneVersion_;
    if (std::erase_if(inactive_, gone) > 0) {
        simulation_->leaveOut(inactive_);
        ++sceneVersion_;
    }
}

// ---- The body --------------------------------------------------------------------

void Session::setBodyPresent(bool present) {
    if (bodyPresent_ == present) return;
    note(present ? "Show the body" : "Hide the body");
    bodyPresent_ = present;
    ++sceneVersion_;
}

void Session::setBodySeeThrough(bool seeThrough) {
    if (bodySeeThrough_ == seeThrough) return;
    note(seeThrough ? "See through the body" : "Make the body solid");
    bodySeeThrough_ = seeThrough;
    ++sceneVersion_;
}

Session::~Session() {
    if (progress_) progress_->stop = true;  // the future waits for its work, which now ends soon
}

std::optional<double> Session::bodyProgress() const {
    if (!preparing_.valid() || !progress_) return std::nullopt;
    return static_cast<double>(progress_->done.load());
}

void Session::previewBody(std::shared_ptr<const sim::Surface> body) {
    if (preview_ == body) return;
    preview_ = std::move(body);
    ++sceneVersion_;
}

void Session::followAvatar() {
    const std::shared_ptr<const Avatar>& wanted = document().avatar();
    if (wanted == bodyOf_) return;
    // The same shape read again (a sample opened after another) is the body that is there already.
    const bool same = wanted && bodyOf_ && wanted->points == bodyOf_->points && wanted->triangles == bodyOf_->triangles;
    bodyOf_ = wanted;
    if (same) return;
    bodyProblem_.reset();
    // The cloth starts again on another body; the drape on the one before is kept for undo.
    keepDrape();
    dropCloth();
    body_ = sim::Body{};
    bodySurface_ = std::make_shared<const sim::Surface>(wanted ? sim::surfaceOf(*wanted) : sim::Surface{});
    // A body still being worked out is given up first: replacing its future waits for the work to end.
    if (progress_) progress_->stop = true;
    progress_ = wanted ? std::make_shared<sim::Progress>() : nullptr;
    // The background work holds the surface too, which never changes; and how it gets on.
    preparing_ = wanted ? std::async(std::launch::async,
                                     [surface = bodySurface_, progress = progress_] { return sim::bodyFrom(*surface, progress.get()); })
                        : std::future<sim::Body>{};
    ++sceneVersion_;
}

// ---- The cloth ---------------------------------------------------------------------

bool Session::run(const ResetCloth& command) {
    noteCommand(typeid(command));
    // The cloth kept is that of the state being left: what has not been followed or heard of yet comes first.
    followBody();
    if (editor_.step() != shownStep_) followShapes(false);
    simulation_->waitForChanges();
    if (std::optional<std::string> problem = takeNews()) toTell_ = std::move(problem);
    const bool there = clothShown();
    if (there) {
        keepDrape();
        const Edit reset = begin();
        editor_.addStep(reset);
        editor_.commit(reset);  // not commit(): the log has the Reset already
        shownStep_ = editor_.step();
        drapes_[shownStep_] = std::nullopt;
    }
    dropCloth();
    return there;
}

void Session::dropCloth() {
    held_.reset();
    pull_.reset();
    simulation_->stop();
    cloth_.mesh.reset();  // a cloth started next must not be taken for this one
    cloth_.positions.clear();
    cloth_.open.clear();
    simulating_ = false;
    clothMoved_ = false;
    clothPlaced_.clear();
    ++sceneVersion_;
}

bool Session::preview(Edit edit, const PullCloth& pull) {
    if (!editor_.editing(edit)) return false;
    if (pull_ == edit) {
        if (held_) simulation_->hold(held_->panel, held_->point, pull.to);
        return held_.has_value();
    }
    // While a body is being worked out there is nothing under the cloth yet: pulled, it would fall through.
    if (!clothShown() || !panelActive(pull.panel) || !bodyReady()) return false;
    if (editor_.step() != shownStep_) followShapes(false);  // a change not followed yet comes first
    const std::vector<sim::PanelMesh>& panels = cloth_.mesh->panels;
    const auto found = std::find_if(panels.begin(), panels.end(), [&](const sim::PanelMesh& p) { return p.panel == pull.panel; });
    if (found == panels.end() || pull.point >= found->points.size()) return false;
    noteCommand(typeid(pull));
    // The pull is an undo step: the drape as it is now is kept for the state before it.
    keepDrape();
    editor_.addStep(edit);
    shownStep_ = editor_.step();
    pull_ = edit;
    held_ = Held{static_cast<std::uint32_t>(found - panels.begin()), pull.point};
    simulation_->hold(held_->panel, held_->point, pull.to);
    return true;
}

void Session::release() {
    if (!held_) return;
    held_.reset();
    simulation_->letGo();
}

void Session::letGoAfterThePull() {
    if (!pull_ || editor_.editing(*pull_)) return;
    pull_.reset();
    release();
}

Edit Session::begin() {
    const Edit edit = editor_.begin();
    letGoAfterThePull();
    return edit;
}

void Session::commit(Edit edit) {
    if (editor_.editing(edit) && editor_.unfinished()) note("Commit");
    editor_.commit(edit);
    letGoAfterThePull();
}

bool Session::cancel(Edit edit) {
    if (editor_.editing(edit) && editor_.unfinished()) note("Cancel");
    const bool taken = editor_.cancel(edit);  // the drape of the state before comes back at the next update
    letGoAfterThePull();
    return taken;
}

bool Session::undo() {
    if (editor_.canUndo()) note("Undo");
    const bool moved = editor_.undo();
    letGoAfterThePull();
    return moved;
}

bool Session::redo() {
    if (editor_.canRedo()) note("Redo");
    const bool moved = editor_.redo();
    letGoAfterThePull();
    return moved;
}

void Session::setThreading(Threading threading) {
    dropCloth();
    threading_ = threading;
    simulation_ = std::make_unique<Simulation>(threading);
    simulation_->leaveOut(inactive_);
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
            meshFailed_.reset();
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
    std::shared_ptr<const sim::ClothMesh> made;
    try {
        made = meshing_.mesh.get();
    } catch (const std::exception& problem) {
        // Not tried again until the outlines change: each try could take as long, and fail the same.
        meshedShapes_ = meshing_.shapes;
        meshFailed_ = meshing_.shapes;
        toTell_ = "The cloth mesh could not be made (" + std::string(problem.what()) +
                  "), so the one before is shown. Undo the last change, or save your work and start Twintailor again.";
        return false;
    }
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

// The cloth as it hangs now, kept for the state of the history it is of.
void Session::keepDrape() {
    if (!clothShown()) return;
    drapes_[shownStep_] = Drape{cloth_.mesh, cloth_.positions, cloth_.time, cloth_.sewing, inactive_};
    while (drapes_.size() > keptDrapes) drapes_.erase(drapes_.begin());  // the oldest first
}

std::optional<std::string> Session::update(bool meshWanted) {
    followBody();
    std::optional<std::string> news;
    if (meshWanted) {
        followShapes(false);
        simulation_->run(simulating_ && bodyReady());  // and while the cloth is held: the simulation sees to that
        simulation_->advance();
        news = takeNews();
    } else {
        simulation_->run(false);
    }
    if (!news) news = std::exchange(toTell_, std::nullopt);  // anything else is told next time
    if (news) {
        note("A problem to tell:");  // two lines: nothing is put together while memory may be short
        note(*news);
    }
    return news;
}

void Session::followBody() {
    followAvatar();
    if (preparing_.valid() && preparing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            body_ = preparing_.get();
        } catch (const std::exception& problem) {
            bodyProblem_ = "The avatar could not be made ready for the cloth (" + std::string(problem.what()) +
                           "), so the cloth waits. Save your work and start Twintailor again.";
            toTell_ = bodyProblem_;
        }
    }
    forgetTheGone();
}

void Session::followShapes(bool now) {
    // Undo and redo bring the drape back: the cloth is kept for each state of
    // the history as it was left, and put back when that state comes back. A
    // state a Reset left with no cloth comes back with none; one that nothing is
    // kept for keeps the cloth there is.
    std::optional<Drape> drapeBack;
    if (editor_.step() != shownStep_) {
        keepDrape();
        if (const auto kept = drapes_.find(editor_.step()); kept != drapes_.end()) {
            if (kept->second) drapeBack = kept->second;
            else dropCloth();
        }
        shownStep_ = editor_.step();
    }
    // A drape brought back is put on a mesh of the same outlines: made now, not in the background.
    const bool remeshed = remesh(now || drapeBack.has_value());
    // Where its particles are arranged is worked out again only when the mesh or a placement changed.
    std::vector<Placed> placed = placedNow();
    if (remeshed || placed != arrangedFrom_) {
        arranged_ = sim::arranged(document(), *mesh_);
        arrangedFrom_ = placed;
        ++sceneVersion_;
    }

    if (drapeBack) {
        held_.reset();
        // With other panels taking part than took part in it, its seams may stand open: drawn in again.
        const bool others = !std::is_permutation(inactive_.begin(), inactive_.end(), drapeBack->inactive.begin(), drapeBack->inactive.end());
        if (simulation_->exists()) {
            simulation_->restore(drapeBack->mesh, std::move(drapeBack->positions), drapeBack->time, drapeBack->sewing || others);
        } else {  // a Reset or another body ended the cloth
            simulation_->resume(drapeBack->mesh, std::move(drapeBack->positions), drapeBack->time, drapeBack->sewing || others, bodyInSimulation());
            bodyInUse_ = {bodyPresent_, body_.scanned.get(), body_.parts.size()};
        }
        clothPlaced_ = placed;  // placed as it was then
    }

    // The cloth follows the pattern: when the mesh has changed it is carried
    // over and stays draped; a panel placed somewhere else is carried there,
    // drape and all. Nothing here starts a panel again.
    if (simulation_->exists()) {
        std::vector<sim::Moved> moved;
        for (const Placed& panel : placed) {
            const auto was = std::find_if(clothPlaced_.begin(), clothPlaced_.end(), [&](const Placed& p) { return p.panel == panel.panel; });
            if (was != clothPlaced_.end() && !(*was == panel)) moved.push_back({panel.panel, sim::Motion{.before = was->arrangement, .after = panel.arrangement}});
        }
        if (remeshed || !moved.empty()) {
            if (remeshed) held_.reset();  // the simulation lets go too: its particles are numbered anew
            simulation_->follow(mesh_, arranged_, std::move(moved));
            clothPlaced_ = std::move(placed);
        }
    }
    // The body the cloth meets: none while it is hidden.
    const BodyInUse bodyNow{bodyPresent_, body_.scanned.get(), body_.parts.size()};
    if (simulation_->exists() && !(bodyNow == bodyInUse_)) {
        simulation_->setBody(bodyInSimulation());
        bodyInUse_ = bodyNow;
    }
    if (simulating_ && bodyReady() && !simulation_->exists()) {
        simulation_->start(mesh_, arranged_, bodyInSimulation());
        clothPlaced_ = placedNow();
        bodyInUse_ = bodyNow;
    }
}

std::optional<std::string> Session::takeNews() {
    if (!simulation_->fetch(cloth_)) return std::nullopt;
    ++sceneVersion_;
    clothMoved_ = saved_.cloth && !clothAsSaved();
    if (!cloth_.failed) return std::nullopt;
    const std::string error = cloth_.problem;
    dropCloth();  // should never happen; if it does, stop instead of drawing nonsense
    if (error.empty()) return "The simulation went wrong and was reset.";
    return "The simulation stopped (" + error + ") and was reset. Save your work before you play it again.";
}

// ---- Export --------------------------------------------------------------------------

bool Session::hasGarment() const {
    const Document& doc = document();
    const std::vector<PanelResult>& panels = editor_.shapes().panels;
    // The panels meshOf() makes cloth of.
    return std::any_of(panels.begin(), panels.end(), [&](const PanelResult& panel) {
        return doc.contains(panel.panel) && !doc.get(panel.panel).construction && !panel.pattern.edges.empty();
    });
}

std::expected<sim::Obj, std::string> Session::obj() {
    note("Export OBJ");
    followBody();
    followShapes(true);
    if (meshFailed_ == editor_.shapesVersion()) {
        toTell_.reset();  // said here
        return std::unexpected("The cloth mesh could not be made, so there is nothing to export of the pattern as it is. Undo the last change and export again.");
    }
    simulation_->waitForChanges();
    if (const std::optional<std::string> problem = takeNews()) return std::unexpected(*problem + " Export again.");
    sim::Obj obj = sim::objOf(document(), shownMesh(), shownPositions(), {.leftOut = hidden_});
    if (obj.panels > 0) return obj;
    if (obj.leftOut > 0) return std::unexpected("There is nothing to export: the panels with cloth are hidden. Show one in the Outliner and export again.");
    return std::unexpected("There is nothing to export. Draw a panel on the table, or put right the one that has no cloth, and export again.");
}

} // namespace twintailor::session
