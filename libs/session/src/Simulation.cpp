#include "clothcad/session/Simulation.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <utility>

namespace clothcad::session {

Simulation::Simulation(Threading threading) : threading_(threading) {
    if (threading_ == Threading::OwnThread) thread_ = std::thread([this] { work(); });
}

Simulation::~Simulation() {
    if (threading_ != Threading::OwnThread) return;
    {
        const std::lock_guard<std::mutex> hold(lock_);
        quit_ = true;
    }
    news_.notify_all();
    thread_.join();
}

template <class Work>
void Simulation::carryOut(const Work& work) {
    try {
        work();
    } catch (const std::exception& problem) {
        drop(problem.what());
    }
}

void Simulation::drop(const char* why) {
    cloth_.reset();
    mesh_.reset();
    held_.reset();
    failed_ = true;
    problem_ = why;
    publish();  // with no cloth, nothing large is copied
}

void Simulation::hand(Change change) {
    if (threading_ == Threading::OneStepPerAdvance) {
        carryOut(change);
        return;
    }
    {
        const std::lock_guard<std::mutex> hold(lock_);
        changes_.push_back(std::move(change));
    }
    news_.notify_all();
}

void Simulation::start(std::shared_ptr<const sim::ClothMesh> mesh, sim::Placement arranged, sim::Body body) {
    exists_ = true;
    hand([this, asked = ++asked_, mesh = std::move(mesh), arranged = std::move(arranged), body = std::move(body)]() mutable {
        made_ = asked;
        body_ = std::move(body);
        cloth_.emplace(*mesh, arranged);
        held_.reset();
        mesh_ = std::move(mesh);
        leaveThemOut();
        failed_ = false;
        problem_.clear();
        publish();
    });
}

void Simulation::resume(std::shared_ptr<const sim::ClothMesh> mesh, sim::Placement positions, double time, bool sewing, sim::Body body) {
    exists_ = true;
    hand([this, asked = ++asked_, mesh = std::move(mesh), positions = std::move(positions), time, sewing, body = std::move(body)]() mutable {
        made_ = asked;
        body_ = std::move(body);
        cloth_.emplace(sim::Cloth::resumed(*mesh, positions, time, !sewing));
        held_.reset();
        mesh_ = std::move(mesh);
        leaveThemOut();
        failed_ = false;
        problem_.clear();
        publish();
    });
}

void Simulation::follow(std::shared_ptr<const sim::ClothMesh> mesh, sim::Placement arranged, std::vector<sim::Moved> moved) {
    hand([this, mesh = std::move(mesh), arranged = std::move(arranged), moved = std::move(moved)]() mutable {
        if (!cloth_ || !mesh_) return;
        cloth_.emplace(sim::Cloth::continued(*mesh, arranged, *cloth_, *mesh_, moved));
        // The same mesh, its panels only carried: the same particle can be held still.
        if (held_ && mesh == mesh_) cloth_->hold(held_->panel, held_->point, held_->to);
        else held_.reset();
        mesh_ = std::move(mesh);
        leaveThemOut();
        publish();
    });
}

void Simulation::hold(std::uint32_t panel, std::uint32_t point, sim::Vec3 to) {
    hand([this, panel, point, to] {
        if (!cloth_) return;
        held_ = Held{panel, point, to};
        cloth_->hold(panel, point, to);
    });
}

void Simulation::letGo() {
    hand([this] {
        held_.reset();
        if (cloth_) cloth_->letGo();
    });
}

void Simulation::restore(std::shared_ptr<const sim::ClothMesh> mesh, sim::Placement positions, double time, bool sewing) {
    hand([this, mesh = std::move(mesh), positions = std::move(positions), time, sewing]() mutable {
        if (!cloth_) return;
        cloth_.emplace(sim::Cloth::resumed(*mesh, positions, time, !sewing));
        mesh_ = std::move(mesh);
        leaveThemOut();
        held_.reset();
        failed_ = false;
        problem_.clear();
        publish();
    });
}

void Simulation::setBody(sim::Body body) {
    hand([this, body = std::move(body)]() mutable { body_ = std::move(body); });
}

void Simulation::leaveOut(std::vector<PanelId> panels) {
    hand([this, panels = std::move(panels)]() mutable {
        leftOut_ = std::move(panels);
        if (!cloth_) return;
        if (held_ && mesh_ && std::find(leftOut_.begin(), leftOut_.end(), mesh_->panels[held_->panel].panel) != leftOut_.end()) held_.reset();
        leaveThemOut();
        publish();
    });
}

// Tells the cloth which of its mesh's panels take no part.
void Simulation::leaveThemOut() {
    if (!cloth_ || !mesh_) return;
    std::vector<std::uint32_t> panels;
    for (std::size_t i = 0; i < mesh_->panels.size(); ++i)
        if (std::find(leftOut_.begin(), leftOut_.end(), mesh_->panels[i].panel) != leftOut_.end()) panels.push_back(static_cast<std::uint32_t>(i));
    cloth_->leaveOut(panels);
}

void Simulation::run(bool on) {
    {
        const std::lock_guard<std::mutex> hold(lock_);
        if (running_ == on) return;
        running_ = on;
    }
    news_.notify_all();
}

void Simulation::stop() {
    exists_ = false;
    hand([this, asked = ++asked_] {
        made_ = asked;
        cloth_.reset();
        held_.reset();
        mesh_.reset();
        failed_ = false;  // said, if it had: this is news of there being no cloth
        problem_.clear();
        publish();
    });
}

// Tells the interface where everything is now.
void Simulation::publish() {
    const std::lock_guard<std::mutex> hold(lock_);
    latest_.cloth = 0;  // of no cloth, so not handed out, until all of it is here: copying may fail
    latest_.failed = failed_;
    latest_.problem = problem_;
    latest_.stepMilliseconds = stepMilliseconds_;
    if (cloth_) {
        latest_.mesh = mesh_;
        latest_.positions = cloth_->positions();
        latest_.time = cloth_->time();
        latest_.sewing = cloth_->sewing();
        latest_.open = cloth_->openSeams();
    } else {
        latest_.mesh.reset();
        latest_.positions.clear();
        latest_.time = 0.0;
        latest_.sewing = false;
        latest_.open.clear();
    }
    latest_.cloth = made_;
    ++latest_.version;
}

bool Simulation::stepOnce() {
    if (!cloth_) return false;
    carryOut([this] {
        const auto began = std::chrono::steady_clock::now();
        cloth_->step(body_);
        stepMilliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        if (!cloth_->sound()) {
            cloth_.reset();
            mesh_.reset();
            failed_ = true;
            problem_.clear();
        }
        publish();
    });
    return true;
}

void Simulation::advance() {
    if (threading_ == Threading::OwnThread) return;
    bool running;
    {
        const std::lock_guard<std::mutex> hold(lock_);
        running = running_;
    }
    if (running || held_) stepOnce();  // a held cloth runs, told to or not
}

void Simulation::waitForChanges() {
    if (threading_ == Threading::OneStepPerAdvance) return;  // taken as they were handed over
    // Changes are taken in the order they came: when this one runs, so has all before it.
    const auto taken = std::make_shared<std::promise<void>>();
    std::future<void> done = taken->get_future();
    hand([taken] { taken->set_value(); });
    done.wait();
}

bool Simulation::fetch(News& into) {
    const std::lock_guard<std::mutex> hold(lock_);
    if (latest_.version == into.version || latest_.cloth != asked_) return false;
    into = latest_;
    return true;
}

// The cloth's thread: take what the interface handed over, then a step, at
// most sixty of them a second so that the cloth's time keeps pace with the
// clock. A held cloth runs, told to or not, so that a paused cloth moves as
// soon as it is taken hold of. What is handed over between two steps is taken
// at once, but the next step still waits its turn: a hand that moves every
// few milliseconds must not make the cloth run ahead of the clock (#107).
void Simulation::work() {
    auto next = std::chrono::steady_clock::now();
    while (true) {
        std::vector<Change> changes;
        bool running;
        {
            std::unique_lock<std::mutex> hold(lock_);
            news_.wait(hold, [&] { return quit_ || !changes_.empty() || running_ || held_; });
            if (quit_) return;
            changes.swap(changes_);
            running = running_;
        }
        for (const Change& change : changes) carryOut(change);
        running = running || held_;
        if (!running) {
            next = std::chrono::steady_clock::now();  // started again, the first step comes at once
            continue;
        }
        if (std::chrono::steady_clock::now() < next) {
            std::unique_lock<std::mutex> hold(lock_);
            // Something handed over first: it is taken, and the step waits on.
            // Otherwise the step is due (or as near as the clock's waits get).
            if (news_.wait_until(hold, next, [&] { return quit_ || !changes_.empty(); })) continue;
        }
        if (!stepOnce()) {
            // Nothing to step yet: wait for a change instead of spinning.
            std::unique_lock<std::mutex> hold(lock_);
            news_.wait_for(hold, std::chrono::milliseconds(5), [&] { return quit_ || !changes_.empty(); });
            next = std::chrono::steady_clock::now();
            continue;
        }
        next += std::chrono::microseconds(16667);
        const auto now = std::chrono::steady_clock::now();
        if (next < now) next = now;  // running behind: no catching up, just carry on
    }
}

} // namespace clothcad::session
