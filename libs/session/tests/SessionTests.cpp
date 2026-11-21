// Unit tests of Session.cpp: what is worked out from the document, and kept
// up to date with it.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/session/Session.h"

using namespace clothcad;
using namespace clothcad::session;

namespace {

struct Skirt {
    Document doc;
    Param length;
    PanelId panel;
    OpRef<Rect> rect;
    Skirt() {
        length = doc.add(Parameter{.name = "length", .value = 40});
        panel = doc.add(Panel{.name = "skirt"});
        rect = doc.add(panel, Rect{.width = 40_cm, .height = length});
        doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 140, .z = 40});
    }
};

void frames(Session& session, int count) {
    for (int i = 0; i < count; ++i) session.update();
}

} // namespace

TEST_CASE("the mesh is made again only when the outlines change")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    const auto made = session.meshesMade();
    const auto mesh = session.mesh();
    EXPECT((mesh && mesh->panels.size() == 1u), "the panel is meshed");

    REQUIRE(session.editor().change([&](Document& doc) { doc.place(start.panel, 50, 50); }, {.touches = Touches::PlacementOnly}));
    session.update();
    EXPECT(session.meshesMade() == made, "moving the panel on the table does not mesh it again");

    REQUIRE(session.editor().change([&](Document& doc) { doc.set(start.length, Expr(50)); }));
    session.update();
    EXPECT(session.meshesMade() == made + 1, "changing its length does");
    EXPECT((mesh->panels.size() == 1u), "a view that kept the old mesh still has it whole");

    session.setParticleDistance(3.0);
    session.update();
    EXPECT(session.meshesMade() == made + 2, "so does a new particle distance");
}

TEST_CASE("on its own thread the mesh is made in the background and catches up")
{
    Skirt start;
    Session session(start.doc, Threading::OwnThread);
    const auto meshed = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            session.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    const auto height = [&] {
        double low = 1e9, high = -1e9;
        for (const auto& p : session.mesh()->panels.front().points) low = std::min(low, p.y), high = std::max(high, p.y);
        return high - low;
    };
    for (int i = 0; i < 400 && session.meshesMade() == 0; ++i) meshed(1);
    REQUIRE(session.mesh()->panels.size() == 1u);

    // A drag: the length changes every frame, faster than meshes are made.
    for (int frame = 0; frame < 20; ++frame) {
        REQUIRE(session.editor().change([&](Document& doc) { doc.set(start.length, Expr(41 + frame)); }));
        session.update();
    }
    for (int i = 0; i < 400 && std::abs(height() - 60.0) > 1e-9; ++i) meshed(1);
    EXPECT_NEAR(height(), 60.0, "after the drag the mesh is of the newest outline");
    const auto made = session.meshesMade();
    meshed(10);
    EXPECT((session.meshesMade() == made), "and then nothing more is made");

    // A panel taken away leaves the mesh in the same update, though meshes are made in the background.
    REQUIRE(session.editor().change([&](Document& doc) {
        const PanelId added = doc.add(Panel{.name = "second"});
        doc.add(added, Rect{.width = 10_cm, .height = 10_cm});
    }));
    for (int i = 0; i < 400 && session.mesh()->panels.size() < 2u; ++i) meshed(1);
    REQUIRE(session.mesh()->panels.size() == 2u);
    REQUIRE(session.editor().change([&](Document& doc) { doc.remove(start.panel); }));
    session.update();
    EXPECT((session.mesh()->panels.size() == 1u && session.mesh()->panels.front().panel != start.panel), "a removed panel is out of the mesh at once");
}

TEST_CASE("the scene version goes up only when something to draw changed")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    const auto version = session.sceneVersion();
    frames(session, 5);
    EXPECT(session.sceneVersion() == version, "nothing changed, nothing to draw again");
    REQUIRE(session.editor().change([&](Document& doc) { doc.arrange(start.panel, Arrangement{.placed = true, .x = 10, .y = 140, .z = 40}); },
                                    {.touches = Touches::PlacementOnly}));
    session.update();
    EXPECT(session.sceneVersion() > version, "arranging the panel elsewhere is something to draw");
}

TEST_CASE("simulating moves the cloth, and an edit keeps it draped")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.update();
    EXPECT(!session.clothShown(), "before simulating, the panels are shown where they are arranged");
    session.simulate(true);
    frames(session, 40);
    EXPECT(session.clothShown(), "simulating, the cloth is shown");
    const double time = session.cloth().time;
    EXPECT(time > 0.6, "and it has moved on in time");

    REQUIRE(session.editor().change([&](Document& doc) { doc.set(start.length, Expr(45)); }));
    session.update();
    EXPECT(session.cloth().time >= time, "an edit does not start the cloth again");
    EXPECT(session.shownMesh().panels.front().points.size() == session.shownPositions().front().size(), "what is shown fits together");

    session.resetCloth();
    session.update();
    EXPECT((!session.clothShown() && !session.simulating()), "reset, the panels are back where they are arranged");
}

TEST_CASE("opening another document ends the cloth and the history")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 5);
    REQUIRE(session.editor().change([&](Document& doc) { doc.set(start.length, Expr(45)); }));
    session.open(Document{});
    session.update();
    EXPECT((!session.clothShown() && !session.editor().canUndo() && session.document().panels().empty()), "all of it is gone");
}

TEST_CASE("an avatar is shown at once and can be collided with a moment later")
{
    // A surface made in memory, as sim::loadObj would hand it over: a ball.
    sim::Body ball;
    ball.parts.push_back({{0, 100, 0}, {0, 100, 0}, 15, 15});
    const sim::Surface surface = sim::surfaceOf(ball);
    Session session;
    EXPECT((session.avatarPath().empty() && session.bodySurface().triangles.empty() && session.bodyReady()), "a new session has no body");
    const auto version = session.sceneVersion();
    session.useAvatar(surface, "ball.obj");
    EXPECT((session.bodySurface().triangles.size() == surface.triangles.size() && session.avatarPath() == "ball.obj"), "the surface is there at once");
    EXPECT((session.sceneVersion() > version), "and is something new to draw");
    for (int i = 0; i < 500 && !session.bodyReady(); ++i) {
        session.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT(session.bodyReady(), "and ready to collide with soon after");
}

TEST_CASE("a draped panel moved in space takes its drape along; nothing starts again")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 60);
    session.simulate(false);
    session.update();
    const sim::Placement draped = session.cloth().positions;
    const double time = session.cloth().time;

    Arrangement higher = session.document().get(start.panel).arrangement;
    higher.y += 20.0;
    REQUIRE(session.editor().change([&](Document& doc) { doc.arrange(start.panel, higher); }, {.touches = Touches::PlacementOnly}));
    session.update();
    double off = 0.0;
    for (std::size_t i = 0; i < draped.front().size(); ++i) off = std::max(off, sim::length(session.cloth().positions.front()[i] - (draped.front()[i] + sim::Vec3{0, 20, 0})));
    EXPECT((off < 1e-9), "every particle is 20 cm higher: the drape went with it");
    EXPECT((session.cloth().time == time), "and the cloth did not start again");
}

TEST_CASE("cloth pulled by hand runs while it is held, even when paused")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 30);
    session.simulate(false);
    session.update();
    const double time = session.cloth().time;
    frames(session, 5);
    REQUIRE(session.cloth().time == time);

    const sim::Vec3 corner = session.cloth().positions.front()[0];
    session.grab(start.panel, 0, corner + sim::Vec3{0, 0, 15});
    EXPECT((session.pulling()), "it is held");
    frames(session, 30);
    EXPECT((session.cloth().time > time), "held, the cloth runs though it is paused");
    EXPECT((sim::length(session.cloth().positions.front()[0] - (corner + sim::Vec3{0, 0, 15})) < 1e-6), "and the held particle is where the hand is");
    session.pullTo(corner + sim::Vec3{0, 10, 15});
    frames(session, 30);
    EXPECT((sim::length(session.cloth().positions.front()[0] - (corner + sim::Vec3{0, 10, 15})) < 1e-6), "the hand moves on, and the particle follows");
    session.letGo();
    frames(session, 2);
    const double after = session.cloth().time;
    frames(session, 5);
    EXPECT((!session.pulling() && session.cloth().time == after), "let go, the cloth is paused again");
}

// How far apart two placements are at most; infinite if they do not fit together.
static double apart(const sim::Placement& a, const sim::Placement& b) {
    if (a.size() != b.size()) return 1e300;
    double most = 0.0;
    for (std::size_t p = 0; p < a.size(); ++p) {
        if (a[p].size() != b[p].size()) return 1e300;
        for (std::size_t i = 0; i < a[p].size(); ++i) most = std::max(most, sim::length(a[p][i] - b[p][i]));
    }
    return most;
}

TEST_CASE("undo and redo bring the drape back")
{
    Skirt start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    frames(session, 60);
    session.simulate(false);
    session.update();
    const sim::Placement before = session.cloth().positions;
    const double timeBefore = session.cloth().time;

    // A change, then the cloth runs on and drapes differently.
    REQUIRE(session.editor().change([&](Document& doc) { doc.set(start.length, Expr(55)); }));
    session.simulate(true);
    frames(session, 60);
    session.simulate(false);
    session.update();
    const sim::Placement after = session.cloth().positions;

    REQUIRE(session.editor().undo());
    session.update();
    EXPECT((apart(session.cloth().positions, before) == 0.0 && session.cloth().time == timeBefore), "undo puts the drape back exactly as it was before the change");
    REQUIRE(session.editor().redo());
    session.update();
    EXPECT((apart(session.cloth().positions, after) == 0.0), "redo brings back the drape as it was left");

    // Simulating on after an undo works as ever.
    REQUIRE(session.editor().undo());
    session.simulate(true);
    frames(session, 10);
    EXPECT((session.cloth().time > timeBefore && session.shownMesh().panels.front().points.size() == session.shownPositions().front().size()),
           "and the cloth runs on from there");
}
