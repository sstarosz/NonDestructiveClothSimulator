// Unit tests of Body.cpp: bodies the cloth rests on: rounded limbs and surfaces
// read from OBJ text. Reading the project's avatar from its file is in
// tests/integration/AvatarTests.cpp.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "Helpers.h"
#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/sim/Body.h"
#include "clothcad/sim/Cloth.h"
#include "clothcad/sim/Layout.h"
#include "clothcad/sim/Mesh.h"

using namespace clothcad;
using namespace clothcad::sim;
using namespace clothcad::sim::testing;

TEST_CASE("rounded limbs are a body, and can be drawn")
{
    // A trunk that narrows towards the top, and an arm held out from it.
    Body body;
    body.parts.push_back({{0, 80, 0}, {0, 120, 0}, 15, 12});
    body.parts.push_back({{12, 115, 0}, {40, 100, 0}, 5, 4});
    EXPECT((distanceTo(body, {0, 100, 0}) < -10.0), "the middle of the trunk is well inside");
    EXPECT((std::abs(distanceTo(body, {0, 80, 20}) - 5.0) < 1e-9), "a point in front is as far out as it is from the surface");
    EXPECT((std::abs(distanceTo(body, {0, 120, 20}) - 8.0) < 1e-9), "where the trunk is narrower too");
    EXPECT((distanceTo(body, {40, 100, 0}) < 0.0), "the end of the arm is inside");
    EXPECT((distanceTo(Body{}, {0, 0, 0}) > 1e20), "no body is far from everything");

    const Surface surface = surfaceOf(body);
    EXPECT((!surface.triangles.empty()), "it can be drawn");
    bool onSurface = true, outward = true;
    for (const Vec3& p : surface.points) onSurface = onSurface && distanceTo(body, p) < 0.5;
    for (std::size_t i = 0; i < surface.triangles.size(); i += 7) {
        const auto& t = surface.triangles[i];
        const Vec3 a = surface.points[t[0]], b = surface.points[t[1]], c = surface.points[t[2]];
        const Vec3 normal = normalized(cross(b - a, c - a));
        const Vec3 middle = (a + b + c) * (1.0 / 3.0);
        // A step outward from a limb's own surface moves away from that limb's axis; checking against the whole body is enough off the overlaps.
        if (distanceTo(body, middle + normal * 0.5) < distanceTo(body, middle - normal * 0.5) - 1e-9 && distanceTo(body, middle) > -0.2) outward = false;
    }
    EXPECT((onSurface), "no drawn point floats off the body");
    EXPECT((outward), "its triangles face outward");
}

TEST_CASE("a surface read from OBJ text is a body")
{
    // A cube as OBJ text, with quads, like a modelling program writes them.
    const LoadedSurface cube = readObj("# a cube 20 cm across, standing on the floor\nv -10 0 -10\nv 10 0 -10\nv 10 20 -10\nv -10 20 -10\nv -10 0 10\nv 10 0 10\nv 10 20 10\nv -10 20 10\n"
                                       "vn 0 0 1\nf 5//1 6//1 7//1 8//1\nf 2 1 4 3\nf 6/1 2/1 3/1 7/1\nf 1 5 8 4\nf -1 -2 -6 -5\nf 1 2 6 5\n");
    EXPECT((cube.problem.empty() && cube.surface.points.size() == 8 && cube.surface.triangles.size() == 12), "eight corners and six quads become twelve triangles");
    EXPECT((readObj("v 0 0 0\nv 1 0 0\nf 1 2 3\n").problem.starts_with("line 3")), "a face naming a vertex that is not there is reported, with its line");
    EXPECT((readObj("v 0 0\n").problem.starts_with("line 1")), "so is a vertex without three numbers");
    EXPECT((!readObj("v 0 0 0\n").problem.empty()), "and text with no faces");

    const Body body = bodyFrom(cube.surface);
    EXPECT((std::abs(distanceTo(body, {0, 10, 12.5}) - 2.5) < 0.05), "2.5 cm in front of a face is 2.5 cm away");
    EXPECT((std::abs(distanceTo(body, {0, 10, 8}) + 2.0) < 0.05), "2 cm behind it is 2 cm inside");
    EXPECT((distanceTo(body, {0, 10, 0}) < -3.0), "the middle is inside");
    EXPECT((distanceTo(body, {0, 60, 0}) > 3.0 && distanceTo(body, {500, 0, 0}) > 3.0), "far away is outside");
    EXPECT((std::abs(distanceTo(body, {11.5, 21.5, 0}) - std::hypot(1.5, 1.5)) < 0.15), "beyond an edge the distance is to the edge, and outside");
    const Contact front = body.scanned->contact({0, 10, 11});
    EXPECT((front.outward.z > 0.99), "the way out of a face is square to it");

    // A cloth dropped on it stays on top.
    Document doc;
    PanelId panel = doc.add(Panel{.name = "cloth"});
    doc.add(panel, Rect{.width = 40_cm, .height = 40_cm});
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 30, .z = 0, .tilt = 90});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    double deepest = 1e9;
    for (int i = 0; i < 180; ++i) {
        cloth.step(body);
        for (const Vec3& p : cloth.positions()[0]) deepest = std::min(deepest, distanceTo(body, p));
    }
    double highest = -1e9, lowest = 1e9;
    for (const Vec3& p : cloth.positions()[0]) {
        highest = std::max(highest, p.y);
        lowest = std::min(lowest, p.y);
    }
    std::printf("      on a cube from a file: y %.1f..%.1f, never closer than %.2f cm\n", lowest, highest, deepest);
    EXPECT((cloth.sound() && deepest > 0.1), "the cloth never goes inside");
    EXPECT((highest > 20.2 && highest < 21.0 && lowest < 15.0), "it lies on top with its edges hanging over");
}
