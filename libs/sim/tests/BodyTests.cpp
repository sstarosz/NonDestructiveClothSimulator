// Unit tests of Body.cpp: bodies the cloth rests on: the mannequin and surfaces from files.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

TEST_CASE("the mannequin is a body")
{
    const Body body = mannequin();
    EXPECT((body.parts.size() > 10), "it has limbs");
    EXPECT((distanceTo(body, {0, 110, 0}) < -3.0), "the middle of the chest is well inside");
    EXPECT((distanceTo(body, {0, 110, 60}) > 40.0), "a point in front is outside");
    EXPECT((distanceTo(body, {0, 250, 0}) > 70.0), "so is one above the head");
    EXPECT((distanceTo(body, {0, -1, 0}) > 0.0), "nothing goes through the floor at the middle");
    // The trunk measures roughly what was asked: walk around it at each height.
    const auto around = [&](double height) {
        double sum = 0.0;
        Vec3 before;
        const int steps = 720;
        for (int i = 0; i <= steps; ++i) {
            const double angle = 2.0 * pi * i / steps;
            const Vec3 direction{std::cos(angle), 0, std::sin(angle)};
            double low = 0.0, high = 16.5;  // the trunk's surface lies between; the arms begin just beyond
            for (int k = 0; k < 40; ++k) {
                const double mid = (low + high) / 2.0;
                (distanceTo(body, Vec3{0, height, 0} + direction * mid) < 0.0 ? low : high) = mid;
            }
            const Vec3 at = Vec3{0, height, 0} + direction * low;
            if (i > 0) sum += length(at - before);
            before = at;
        }
        return sum;
    };
    const MannequinSize size;
    const double waist = around(0.62 * size.height), hips = around(0.53 * size.height), chest = around(0.72 * size.height);
    std::printf("      around: waist %.1f (asked %.0f), hips %.1f (%.0f), chest %.1f (%.0f)\n", waist, size.waist, hips, size.hips, chest, size.chest);
    EXPECT((std::abs(waist - size.waist) < 0.05 * size.waist), "the waist is within 5% of the measurement");
    EXPECT((std::abs(hips - size.hips) < 0.05 * size.hips), "so are the hips");
    EXPECT((std::abs(chest - size.chest) < 0.05 * size.chest), "and the chest");
    const Body tall = mannequin({.height = 190_cm});
    EXPECT((distanceTo(tall, {0, 185, 0}) < 0.0 && distanceTo(body, {0, 185, 0}) > 0.0), "a taller body is taller");

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

TEST_CASE("a surface from a file is a body")
{
    // A cube written as an OBJ file, with quads, like a modelling program writes them.
    // In the system's folder for temporary files: there is no /tmp on Windows.
    const std::filesystem::path folder = std::filesystem::temp_directory_path();
    const std::string path = (folder / "clothcad_cube.obj").string();
    std::ofstream(path) << "# a cube 20 cm across, standing on the floor\nv -10 0 -10\nv 10 0 -10\nv 10 20 -10\nv -10 20 -10\nv -10 0 10\nv 10 0 10\nv 10 20 10\nv -10 20 10\n"
                           "vn 0 0 1\nf 5//1 6//1 7//1 8//1\nf 2 1 4 3\nf 6/1 2/1 3/1 7/1\nf 1 5 8 4\nf -1 -2 -6 -5\nf 1 2 6 5\n";
    const LoadedSurface cube = loadObj(path);
    std::filesystem::remove(path);
    EXPECT((cube.problem.empty() && cube.surface.points.size() == 8 && cube.surface.triangles.size() == 12), "eight corners and six quads become twelve triangles");
    EXPECT((!loadObj((folder / "clothcad_no_such_file.obj").string()).problem.empty()), "a missing file is reported");

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
