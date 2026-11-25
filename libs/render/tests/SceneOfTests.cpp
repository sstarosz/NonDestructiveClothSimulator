// Unit tests of SceneOf.cpp: what of a session the 3D view shows.

#include <algorithm>
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

TEST_CASE("every panel has a darker, cooler back; the body has none")
{
    TwoPanels start;
    session::Session session(start.doc, session::Threading::OneStepPerAdvance);
    session.useAvatar(figure(), "figure.obj");
    session.update();
    OrbitCamera camera;
    const auto coolerAndDarker = [](const Surface& s) {
        if (!s.back) return false;
        const Colour front = s.colour, back = *s.back;
        return back.red < front.red && back.green < front.green && back.blue < front.blue && back.blue / back.red > front.blue / front.red;
    };
    const Scene plain = sceneOf(session, camera, 1.5, {});
    REQUIRE(plain.surfaces.size() == 3);
    EXPECT(!plain.surfaces[0].back, "the body is one colour");
    EXPECT(coolerAndDarker(plain.surfaces[1]), "a grey panel");
    EXPECT(coolerAndDarker(plain.surfaces[2]), "an instance");
    const Scene chosen = sceneOf(session, camera, 1.5, {.selected = start.front});
    EXPECT(coolerAndDarker(chosen.surfaces[1]), "the selected panel");
}

TEST_CASE("both edges of a seam are lined where the cloth has them; the selected seam in amber")
{
    Document doc;
    const PanelId left = doc.add(Panel{.name = "left"});
    const OpRef<Rect> leftRect = doc.add(left, Rect{.width = 20_cm, .height = 30_cm});
    doc.arrange(left, Arrangement{.placed = true, .x = -20, .y = 100, .z = 0});
    const PanelId right = doc.add(Panel{.name = "right"});
    const OpRef<Rect> rightRect = doc.add(right, Rect{.width = 20_cm, .height = 30_cm});
    doc.arrange(right, Arrangement{.placed = true, .x = 20, .y = 100, .z = 0});
    const SeamId seam = doc.add(Seam{.a = {left, leftRect.right()}, .b = {right, rightRect.left()}});
    session::Session session(doc, session::Threading::OneStepPerAdvance);
    session.update();
    OrbitCamera camera;
    // The seams' lines are the ones drawn nearer than they are.
    const auto seams = [&](const SceneOptions& options) {
        std::vector<Lines> found;
        for (const Lines& lines : sceneOf(session, camera, 1.5, options).lines)
            if (lines.lift > 0.0f) found.push_back(lines);
        return found;
    };
    // Whether a line runs along both sewn edges, where the shown particles are.
    const auto alongBothEdges = [&](const Lines& lines) {
        std::vector<Point> expected;
        std::size_t segments = 0;
        for (const SeamSide& side : {session.document().get(seam).a, session.document().get(seam).b}) {
            const sim::PanelMesh* panel = session.shownMesh().find(side.panel);
            const auto i = static_cast<std::size_t>(panel - session.shownMesh().panels.data());
            const sim::EdgeParticles* along = panel->along(side.edge);
            segments += along->points.size() - 1;
            for (std::uint32_t point : along->points) {
                const sim::Vec3 p = session.shownPositions()[i][point];
                expected.push_back({static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
            }
        }
        bool all = lines.points.size() == segments * 2;
        for (const Point& e : expected)
            all = all && std::any_of(lines.points.begin(), lines.points.end(), [&](const Point& p) { return p.x == e.x && p.y == e.y && p.z == e.z; });
        return all;
    };

    const std::vector<Lines> flat = seams({});
    REQUIRE(flat.size() == 1);
    EXPECT(alongBothEdges(flat[0]), "along both sewn edges of the arranged panels");
    EXPECT((flat[0].colour.blue > 0.9f && flat[0].colour.red < 0.5f), "in blue");
    EXPECT(flat[0].width > 1.0f, "wider than the floor's lines");

    const std::vector<Lines> chosen = seams({.seam = seam});
    REQUIRE(chosen.size() == 1);
    EXPECT((chosen[0].colour.red == 1.0f && chosen[0].colour.blue < 0.5f), "the selected seam is amber");
    EXPECT(chosen[0].width > flat[0].width, "and wider");
    EXPECT((seams({.pixel = 2.0})[0].width == flat[0].width * 2.0f), "as wide on a screen with twice the pixels");

    session.simulate(true);
    for (int i = 0; i < 30; ++i) session.update();
    const std::vector<Lines> draped = seams({});
    REQUIRE(draped.size() == 1);
    EXPECT(alongBothEdges(draped[0]), "the line follows the draped cloth");
    bool moved = false;
    for (std::size_t i = 0; i < draped[0].points.size() && i < flat[0].points.size(); ++i)
        moved = moved || std::abs(draped[0].points[i].x - flat[0].points[i].x) > 1.0f;
    EXPECT(moved, "and has left where the flat panels had it");

    REQUIRE(session.editor().change([&](Document& d) { d.remove(seam); }));
    session.update();
    EXPECT(seams({}).empty(), "a seam taken away has no line");
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
