#pragma once
// Runs the cloth so that the interface never waits for it. The cloth lives
// on a thread of its own; the interface hands it changes and picks up where
// every particle is, whenever there is news.
//
//   Simulation simulation;
//   simulation.start(mesh, arranged, body);
//   simulation.run(true);
//   ...each frame:
//   if (simulation.fetch(news)) draw(*news.mesh, news.positions);
//
// Every function here is for the thread that owns the Simulation. With
// Threading::OneStepPerAdvance nothing runs in the background: advance()
// takes one step, so the motion repeats exactly, frame for frame. Tests use that.

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "clothcad/sim/Cloth.h"

namespace clothcad::session {

enum class Threading {
    OwnThread,          // the cloth steps on its own, up to sixty times a second
    OneStepPerAdvance,  // the cloth steps once each time advance() is called
};

class Simulation {
public:
    explicit Simulation(Threading threading = Threading::OwnThread);
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    // A new cloth, starting where it is arranged.
    void start(std::shared_ptr<const sim::ClothMesh> mesh, sim::Placement arranged, sim::Body body);
    // A new cloth that is one draped before, as restore() puts it back: where
    // there is no cloth to put it back into.
    void resume(std::shared_ptr<const sim::ClothMesh> mesh, sim::Placement positions, double time, bool sewing, sim::Body body);
    // The pattern changed, or panels were carried through space: the cloth is
    // carried over to the new mesh and stays draped; the panels in `moved` go
    // where they were carried, drape and all.
    void follow(std::shared_ptr<const sim::ClothMesh> mesh, sim::Placement arranged, std::vector<sim::Moved> moved);
    // Takes hold of a particle and leads it to `to` (see sim::Cloth::hold);
    // called again, leads it somewhere else. A new mesh ends the hold. While
    // it is held the cloth runs, though run(false) was said.
    void hold(std::uint32_t panel, std::uint32_t point, sim::Vec3 to);
    void letGo();
    // The cloth as it was before (undo and redo): `positions` on `mesh`,
    // at rest, `time` seconds on; `sewing` as it was then.
    void restore(std::shared_ptr<const sim::ClothMesh> mesh, sim::Placement positions, double time, bool sewing);
    void setBody(sim::Body body);
    // The panels that take no part (see sim::Cloth::leaveOut); the others do.
    // Kept for every cloth from now on, until told otherwise.
    void leaveOut(std::vector<PanelId> panels);
    // Lets time pass, or holds the cloth still (unless a particle is held).
    void run(bool on);
    // Forgets the cloth.
    void stop();
    bool exists() const { return exists_; }
    Threading threading() const { return threading_; }

    // With Threading::OneStepPerAdvance: one step, if running. Otherwise nothing.
    void advance();
    // Waits until the cloth has taken everything handed to it, so that fetch()
    // hands out news of it. It does not wait for a step.
    void waitForChanges();

    struct News {
        std::uint64_t version = 0;                  // goes up with every piece of news
        std::uint64_t cloth = 0;                    // goes up with every start() and stop()
        std::shared_ptr<const sim::ClothMesh> mesh;  // what the positions are positions of; empty when there is no cloth
        sim::Placement positions;
        double time = 0.0;                          // seconds of cloth time so far
        bool sewing = false;
        std::vector<sim::OpenSeam> open;            // the seams that could not close (sim::Cloth::openSeams)
        bool failed = false;                        // the cloth stopped being numbers and was dropped; should never happen
        double stepMilliseconds = 0.0;              // how long the latest step took to work out
    };
    // Copies the latest news into `into` if it is newer than what `into`
    // holds; returns whether it did. News of a cloth from before the latest
    // start() or stop(), still on its way, is never handed out.
    bool fetch(News& into);

private:
    using Change = std::function<void()>;
    void hand(Change change);  // runs it at once, or on the cloth's thread before its next step
    void publish();
    void leaveThemOut();
    bool stepOnce();
    void work();

    const Threading threading_;
    bool exists_ = false;
    std::uint64_t asked_ = 0;  // which cloth was asked for last: counted by start() and stop()

    // The cloth's own things: touched only by whoever runs the cloth.
    std::optional<sim::Cloth> cloth_;
    std::shared_ptr<const sim::ClothMesh> mesh_;
    sim::Body body_;
    std::vector<PanelId> leftOut_;
    struct Held {
        std::uint32_t panel, point;
        sim::Vec3 to;
    };
    std::optional<Held> held_;
    bool failed_ = false;
    double stepMilliseconds_ = 0.0;
    std::uint64_t made_ = 0;  // which of the asked cloths this is

    // Shared between the two threads, under the lock.
    std::mutex lock_;
    std::condition_variable news_;
    std::vector<Change> changes_;
    bool running_ = false;
    bool quit_ = false;
    News latest_;
    std::thread thread_;
};

} // namespace clothcad::session
