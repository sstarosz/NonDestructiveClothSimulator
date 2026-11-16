// Unit tests of Session.cpp: what is worked out from the document, and kept
// up to date with it.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    session.useMannequin();
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
    const char* path = "clothcad_session_cube.obj";
    {
        std::FILE* file = std::fopen(path, "w");
        std::fputs("v -10 0 -10\nv 10 0 -10\nv 10 20 -10\nv -10 20 -10\nv -10 0 10\nv 10 0 10\nv 10 20 10\nv -10 20 10\n"
                   "f 5 6 7 8\nf 2 1 4 3\nf 6 2 3 7\nf 1 5 8 4\nf 8 7 3 4\nf 1 2 6 5\n",
                   file);
        std::fclose(file);
    }
    Session session;
    EXPECT(!session.loadAvatar("no/such/avatar.obj").has_value(), "a missing file is reported");
    EXPECT(session.avatarPath().empty(), "and the mannequin stays");
    REQUIRE(session.loadAvatar(path).has_value());
    EXPECT((session.bodySurface().triangles.size() == 12u && session.avatarPath() == path), "the surface is there at once");
    for (int i = 0; i < 500 && !session.bodyReady(); ++i) {
        session.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT(session.bodyReady(), "and ready to collide with soon after");
    std::remove(path);
}
