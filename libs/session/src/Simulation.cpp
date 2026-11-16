#include "clothcad/session/Simulation.h"

#include <chrono>
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

void Simulation::hand(Change change) {
    if (threading_ == Threading::OneStepPerAdvance) {
        change();
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
    hand([this, mesh = std::move(mesh), arranged = std::move(arranged), body = std::move(body)]() mutable {
        body_ = std::move(body);
        cloth_.emplace(*mesh, arranged);
        mesh_ = std::move(mesh);
        failed_ = false;
        publish();
    });
}

void Simulation::follow(std::shared_ptr<const sim::ClothMesh> mesh, sim::Placement arranged, std::vector<PanelId> placeAgain) {
    hand([this, mesh = std::move(mesh), arranged = std::move(arranged), placeAgain = std::move(placeAgain)]() mutable {
        if (!cloth_ || !mesh_) return;
        cloth_.emplace(sim::Cloth::continued(*mesh, arranged, *cloth_, *mesh_, placeAgain));
        mesh_ = std::move(mesh);
        publish();
    });
}

void Simulation::setBody(sim::Body body) {
    hand([this, body = std::move(body)]() mutable { body_ = std::move(body); });
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
    hand([this] {
        cloth_.reset();
        mesh_.reset();
        publish();
    });
}

// Tells the interface where everything is now.
void Simulation::publish() {
    const std::lock_guard<std::mutex> hold(lock_);
    ++latest_.version;
    latest_.failed = failed_;
    latest_.stepMilliseconds = stepMilliseconds_;
    if (cloth_) {
        latest_.mesh = mesh_;
        latest_.positions = cloth_->positions();
        latest_.time = cloth_->time();
        latest_.sewing = cloth_->sewing();
    } else {
        latest_.mesh.reset();
        latest_.positions.clear();
        latest_.time = 0.0;
        latest_.sewing = false;
    }
}

bool Simulation::stepOnce() {
    if (!cloth_) return false;
    const auto began = std::chrono::steady_clock::now();
    cloth_->step(body_);
    stepMilliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    if (!cloth_->sound()) {
        cloth_.reset();
        mesh_.reset();
        failed_ = true;
    }
    publish();
    return true;
}

void Simulation::advance() {
    if (threading_ == Threading::OwnThread) return;
    bool running;
    {
        const std::lock_guard<std::mutex> hold(lock_);
        running = running_;
    }
    if (running) stepOnce();
}

bool Simulation::fetch(News& into) {
    const std::lock_guard<std::mutex> hold(lock_);
    if (latest_.version == into.version) return false;
    into = latest_;
    return true;
}

// The cloth's thread: take what the interface handed over, then a step, at
// most sixty of them a second so that the cloth's time keeps pace with the clock.
void Simulation::work() {
    auto next = std::chrono::steady_clock::now();
    while (true) {
        std::vector<Change> changes;
        bool running;
        {
            std::unique_lock<std::mutex> hold(lock_);
            news_.wait(hold, [&] { return quit_ || !changes_.empty() || running_; });
            if (quit_) return;
            changes.swap(changes_);
            running = running_;
        }
        for (Change& change : changes) change();
        if (!running || !stepOnce()) {
            if (running) {
                // Nothing to step yet: wait for a change instead of spinning.
                std::unique_lock<std::mutex> hold(lock_);
                news_.wait_for(hold, std::chrono::milliseconds(5), [&] { return quit_ || !changes_.empty(); });
            }
            next = std::chrono::steady_clock::now();
            continue;
        }
        next += std::chrono::microseconds(16667);
        const auto now = std::chrono::steady_clock::now();
        if (next < now) next = now;  // running behind: no catching up, just carry on
        else {
            std::unique_lock<std::mutex> hold(lock_);
            news_.wait_until(hold, next, [&] { return quit_ || !changes_.empty(); });
        }
    }
}

} // namespace clothcad::session
