// Unit tests of SceneOf.cpp: what of a session the 3D view shows.

#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/render/SceneOf.h"

using namespace clothcad;
using namespace clothcad::render;

namespace {

// A body made in memory: one rounded limb standing on the floor, 160 cm tall.
sim::Surface figure() {
    sim::Body body;
    body.parts.push_back({{0, 10, 0}, {0, 150, 0}, 10, 10});
    return sim::surfaceOf(body);
}

struct TwoPanels {
    Document doc;
    PanelId front, copy;
    TwoPanels() {
        front = doc.add(Panel{.name = "front"});
        doc.add(front, Rect{.width = 40_cm, .height = 60_cm});
        doc.arrange(front, Arrangement{.placed = true, .x = 0, .y = 100, .z = 30});
        copy = doc.add(Panel{.name = "copy"});
        doc.add(copy, Instance{.source = front});
        doc.arrange(copy, Arrangement{.placed = true, .x = 0, .y = 100, .z = -30});
    }
};

} // namespace

TEST_CASE("the scene has the body, every panel and the floor")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    session.useAvatar(figure(), "figure.obj");
    session.update();
    OrbitCamera camera;
    const Scene scene = sceneOf(session, camera, 1.5, {.selected = start.front});
    REQUIRE(scene.surfaces.size() == 3);
    EXPECT((!scene.surfaces[0].triangles.empty() && scene.surfaces[0].triangles.size() == session.bodySurface().triangles.size() * 3), "the body first, whole");
    const Surface& front = scene.surfaces[1];
    EXPECT((front.colour.red == 1.0f && front.colour.blue < 0.5f), "the selected panel is amber");
    EXPECT((scene.surfaces[2].colour.blue > scene.surfaces[2].colour.green), "an instance is violet");
    bool level = true;
    for (const Point& p : front.points) level = level && std::abs(p.z - 30.0f) < 1e-3f;
    EXPECT(level, "a panel stands where it is arranged");
    EXPECT((!scene.lines.empty() && scene.lines[0].points.front().y == 0.0f), "the floor is at y = 0");

    const Scene bare = sceneOf(session, camera, 1.5, {.body = false, .mesh = true});
    EXPECT((bare.surfaces.size() == 2 && bare.lines.size() == 3), "without the body, and with the mesh's sides as lines");
}

TEST_CASE("normals point out of the side a surface is wound towards")
{
    const std::vector<Point> square{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const std::vector<Point> normals = normalsOf(square, {0, 1, 2, 0, 2, 3});
    bool towardsZ = true;
    for (const Point& n : normals) towardsZ = towardsZ && std::abs(n.z - 1.0f) < 1e-6f;
    EXPECT(towardsZ, "counter-clockwise seen from +z: the normals point at +z");
}

TEST_CASE("the extent holds the body and the cloth")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    session.update();
    const auto cloth = extentOf(session);
    REQUIRE(cloth);
    EXPECT((cloth->first.y > 1.0 && cloth->first.z <= -30.0 && cloth->second.z >= 30.0), "with no body, only the cloth: it does not reach the floor");
    session.useAvatar(figure(), "figure.obj");
    session.update();
    const auto box = extentOf(session);
    REQUIRE(box);
    EXPECT((box->first.y < 1.0 && box->second.y > 150.0), "from the floor to the top of the head");
    EXPECT((box->first.z <= -30.0 && box->second.z >= 30.0), "and both panels");
}
