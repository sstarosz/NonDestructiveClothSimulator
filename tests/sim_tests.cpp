// Tests for meshing: panels become triangles that cover them, and sewn
// edges get particles that pair up.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>

#include "core/Document.h"
#include "core/Evaluate.h"
#include "sim/Body.h"
#include "sim/Cloth.h"
#include "sim/Layout.h"
#include "sim/Mesh.h"
#include "tests/Check.h"

using namespace clothcad;
using namespace clothcad::sim;

// Every side of every triangle is used by one triangle (outline) or two (inside).
static bool watertight(const PanelMesh& mesh) {
    std::multiset<std::pair<uint32_t, uint32_t>> sides;
    for (const auto& t : mesh.triangles)
        for (int i = 0; i < 3; ++i) sides.insert(std::minmax(t[i], t[(i + 1) % 3]));
    for (const auto& side : sides)
        if (sides.count(side) > 2) return false;
    return true;
}

static double longestSide(const PanelMesh& mesh) {
    double longest = 0.0;
    for (const auto& t : mesh.triangles)
        for (int i = 0; i < 3; ++i) {
            const Vec2 a = mesh.points[t[i]], b = mesh.points[t[(i + 1) % 3]];
            longest = std::max(longest, std::hypot(b.x - a.x, b.y - a.y));
        }
    return longest;
}

static void aRectangleIsCoveredByTriangles(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 40_cm});
    const ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    check.that(cloth.panels.size() == 1 && cloth.panels[0].problem.empty(), "one mesh, no problem");
    const PanelMesh& mesh = cloth.panels[0];
    check.that(std::abs(mesh.area() - 1200.0) < 1e-6, "the triangles cover exactly the rectangle");
    check.that(watertight(mesh), "no side is shared by more than two triangles");
    check.that(mesh.along(rect.bottom()) && mesh.along(rect.bottom())->points.size() == 16, "30 cm at 2 cm apart is 15 pieces along the bottom");
    check.that(longestSide(mesh) < 2.0 * 2.0, "no triangle is much larger than asked");
    check.that(mesh.points.size() > 250 && mesh.points.size() < 450, "about one particle per 3.5 square cm");
    for (const auto& t : mesh.triangles) {
        const Vec2 a = mesh.points[t[0]], b = mesh.points[t[1]], c = mesh.points[t[2]];
        if ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) <= 0.0) {
            check.that(false, "every triangle is counter-clockwise");
            break;
        }
    }
    const ClothMesh fine = meshOf(doc, evaluate(doc), {.particleDistance = 1_cm});
    check.that(fine.panels[0].points.size() > 3 * mesh.points.size(), "half the distance is about four times the particles");
}

static void curvedAndHollowPanelsMesh(Check& check) {
    Document doc;
    PanelId disc = doc.add(Panel{.name = "disc"});
    doc.add(disc, Circle{.radius = 20_cm});
    PanelId ring = doc.add(Panel{.name = "ring"});
    doc.add(ring, RingSector{.innerLength = 70_cm, .sweep = 360_deg, .depth = 30_cm});
    PanelId half = doc.add(Panel{.name = "half"});
    doc.add(half, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 30_cm});
    const ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    check.that(cloth.panels.size() == 3, "three meshes");
    for (const PanelMesh& mesh : cloth.panels) check.that(mesh.problem.empty() && watertight(mesh), "each meshes cleanly");

    const double discArea = pi * 20 * 20;
    check.that(std::abs(cloth.find(disc)->area() - discArea) < 0.01 * discArea, "the disc within 1% of its area");
    const double inner = 70.0 / (2 * pi), outer = inner + 30;
    const double ringArea = pi * (outer * outer - inner * inner);
    check.that(std::abs(cloth.find(ring)->area() - ringArea) < 0.01 * ringArea, "the full ring within 1%: its hole is left open");
    check.that(cloth.find(ring)->edges.size() == 2, "a full ring has two edges; its slit is closed up");
    const double halfInner = 70.0 / pi, halfOuter = halfInner + 30;
    const double halfArea = pi / 2 * (halfOuter * halfOuter - halfInner * halfInner);
    check.that(std::abs(cloth.find(half)->area() - halfArea) < 0.01 * halfArea, "half a ring within 1%");
}

static void sewnEdgesPairUp(Check& check) {
    Document doc;
    PanelId band = doc.add(Panel{.name = "band"});
    OpRef<Rect> bandRect = doc.add(band, Rect{.width = 80_cm, .height = 4_cm});
    PanelId skirt = doc.add(Panel{.name = "skirt"});
    OpRef<Rect> skirtRect = doc.add(skirt, Rect{.width = 120_cm, .height = 60_cm});
    SeamId seam = doc.add(Seam{.a = {band, bandRect.bottom()}, .b = {skirt, skirtRect.top()}});

    ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const PanelMesh& bandMesh = *cloth.find(band);
    const PanelMesh& skirtMesh = *cloth.find(skirt);
    const EdgeParticles* a = bandMesh.along(bandRect.bottom());
    const EdgeParticles* b = skirtMesh.along(skirtRect.top());
    check.that(a && b && a->points.size() == b->points.size() && a->points.size() == 61, "both edges get the longer one's 60 pieces");
    check.that(cloth.stitches.size() == 61, "one stitch per particle");
    check.that(bandMesh.problem.empty() && skirtMesh.problem.empty(), "both still mesh");
    // The band's bottom runs left to right, the skirt's top right to left: start meets end.
    const Stitch first = cloth.stitches.front();
    const Vec2 onBand = bandMesh.points[first.pointA], onSkirt = skirtMesh.points[first.pointB];
    check.that(std::abs(onBand.x) < 1e-9 && std::abs(onSkirt.x) < 1e-9, "the band's left end meets the skirt's left end");

    Seam reversed = doc.get(seam);
    reversed.reversed = true;
    doc.set(seam, reversed);
    cloth = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const Vec2 twisted = cloth.find(skirt)->points[cloth.stitches.front().pointB];
    check.that(std::abs(twisted.x - 120.0) < 1e-9, "reversed, it meets the other end");

    // A construction panel is not cloth; a broken seam makes no stitches.
    doc.setConstruction(band, true);
    cloth = meshOf(doc, evaluate(doc));
    check.that(cloth.panels.size() == 1 && cloth.stitches.empty(), "a base panel is left out, with its stitches");
}

static void aTubeSewsToItself(Check& check) {
    Document doc;
    PanelId tube = doc.add(Panel{.name = "tube"});
    OpRef<Rect> rect = doc.add(tube, Rect{.width = 30_cm, .height = 20_cm});
    doc.add(Seam{.a = {tube, rect.left()}, .b = {tube, rect.right()}});
    const ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    check.that(cloth.stitches.size() == 11, "the two sides pair up along their 10 pieces");
    bool level = true;
    for (const Stitch& s : cloth.stitches)
        level = level && std::abs(cloth.panels[0].points[s.pointA].y - cloth.panels[0].points[s.pointB].y) < 1e-9;
    check.that(level, "each stitch joins points at the same height");
}

static void badOutlinesAreReportedNotThrown(Check& check) {
    Document doc;
    PanelId bow = doc.add(Panel{.name = "bow"});
    OpRef<Rect> rect = doc.add(bow, Rect{.width = 20_cm, .height = 20_cm});
    doc.add(bow, AdjustPoint{.corner = rect.topRight(), .dx = -30_cm, .dy = 0_cm});  // dragged across the opposite side
    PanelId fine = doc.add(Panel{.name = "fine"});
    doc.add(fine, Rect{.width = 10_cm, .height = 10_cm});
    const ClothMesh cloth = meshOf(doc, evaluate(doc));
    check.that(cloth.panels.size() == 2, "both panels are listed");
    check.that(!cloth.find(bow)->problem.empty() && cloth.find(bow)->triangles.empty(), "the self-crossing one says why it has no triangles");
    check.that(cloth.find(fine)->problem.empty(), "the other is unaffected");
    PanelId huge = doc.add(Panel{.name = "huge"});
    doc.add(huge, Rect{.width = 5000_cm, .height = 5000_cm});
    check.that(!meshOf(doc, evaluate(doc), {.particleDistance = 0.1_cm}).find(huge)->problem.empty(), "a mesh that would be enormous is refused");
}

static void panelsAreArrangedInSpace(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "front", .x = 100, .y = 50});
    doc.add(panel, Rect{.width = 40_cm, .height = 60_cm});
    ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 4_cm});
    Placement placed = arranged(doc, cloth);
    check.that(placed.size() == 1 && placed[0].size() == cloth.panels[0].points.size(), "one position per particle");
    // Not arranged yet: where the table has it, upright, in front of the body.
    check.that(!arrangementOf(doc, cloth.panels[0]).placed, "it reports that it was not arranged");
    bool upright = true;
    for (std::size_t i = 0; i < placed[0].size(); ++i) {
        const Vec2 flat = cloth.panels[0].points[i];
        const Vec3 p = placed[0][i];
        upright = upright && std::abs(p.x - (100 + flat.x)) < 1e-9 && std::abs(p.y - (50 + flat.y)) < 1e-9 && std::abs(p.z - unarrangedDistance) < 1e-9;
    }
    check.that(upright, "unarranged, it stands where the table has it");

    // Arranged: its centre goes where asked; a quarter turn puts its width along z.
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 110, .z = 20, .turn = 90});
    placed = arranged(doc, cloth);
    Vec3 low = placed[0][0], high = placed[0][0];
    for (const Vec3& p : placed[0]) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    check.that(std::abs(high.x - low.x) < 1e-9 && std::abs((high.z - low.z) - 40.0) < 1e-9 && std::abs((high.y - low.y) - 60.0) < 1e-9, "turned a quarter, it is edge-on");
    check.that(std::abs((low.z + high.z) / 2 - 20.0) < 1e-9 && std::abs((low.y + high.y) / 2 - 110.0) < 1e-9, "centred where it was put");

    // Wrapped half way round: both side edges end up directly behind the middle, a diameter apart.
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 110, .z = 20, .wrap = 180});
    placed = arranged(doc, cloth);
    const double radius = 40.0 / pi;
    double deepest = 0.0, widest = 0.0;
    for (const Vec3& p : placed[0]) {
        deepest = std::min(deepest, p.z - 20.0);
        widest = std::max(widest, std::abs(p.x));
    }
    check.that(std::abs(deepest + radius) < 1e-6 && std::abs(widest - radius) < 0.2, "wrapped half round, it is half a cylinder behind its middle");
    // Bending does not stretch: neighbours along a row stay the same distance apart along the arc.
    const EdgeParticles* bottom = cloth.panels[0].along(OpRef<Rect>(doc.operations(panel).front()).bottom());
    double arc = 0.0;
    for (std::size_t i = 1; i < bottom->points.size(); ++i) arc += length(placed[0][bottom->points[i]] - placed[0][bottom->points[i - 1]]);
    check.that(arc > 39.0 && arc <= 40.0 + 1e-9, "and its bottom edge is still 40 cm long");
}

static void theMannequinIsABody(Check& check) {
    const Body body = mannequin();
    check.that(body.parts.size() > 10, "it has limbs");
    check.that(distanceTo(body, {0, 110, 0}) < -3.0, "the middle of the chest is well inside");
    check.that(distanceTo(body, {0, 110, 60}) > 40.0, "a point in front is outside");
    check.that(distanceTo(body, {0, 250, 0}) > 70.0, "so is one above the head");
    check.that(distanceTo(body, {0, -1, 0}) > 0.0, "nothing goes through the floor at the middle");
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
    check.that(std::abs(waist - size.waist) < 0.05 * size.waist, "the waist is within 5% of the measurement");
    check.that(std::abs(hips - size.hips) < 0.05 * size.hips, "so are the hips");
    check.that(std::abs(chest - size.chest) < 0.05 * size.chest, "and the chest");
    const Body tall = mannequin({.height = 190_cm});
    check.that(distanceTo(tall, {0, 185, 0}) < 0.0 && distanceTo(body, {0, 185, 0}) > 0.0, "a taller body is taller");

    const Surface surface = surfaceOf(body);
    check.that(!surface.triangles.empty(), "it can be drawn");
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
    check.that(onSurface, "no drawn point floats off the body");
    check.that(outward, "its triangles face outward");
}

// ---- Cloth in motion ---------------------------------------------------------

// How much the most stretched triangle side has grown, as a part of its flat length.
static double worstStretch(const ClothMesh& mesh, const Placement& now) {
    double worst = 0.0;
    for (std::size_t p = 0; p < mesh.panels.size(); ++p)
        for (const auto& t : mesh.panels[p].triangles)
            for (int i = 0; i < 3; ++i) {
                const Vec2 a = mesh.panels[p].points[t[i]], b = mesh.panels[p].points[t[(i + 1) % 3]];
                const double flat = std::hypot(b.x - a.x, b.y - a.y);
                worst = std::max(worst, length(now[p][t[(i + 1) % 3]] - now[p][t[i]]) / flat - 1.0);
            }
    return worst;
}

static void run(Cloth& cloth, const Body& body, double seconds) {
    for (int i = 0; i < static_cast<int>(seconds * 60.0); ++i) cloth.step(body);
}

static void aDroppedClothComesToRestOnTheFloor(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "cloth"});
    doc.add(panel, Rect{.width = 40_cm, .height = 40_cm});
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 30, .z = 0, .tilt = 80});  // nearly lying down, 30 cm up
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    const Body nobody;
    run(cloth, nobody, 0.1);
    check.that(cloth.fastest() > 50.0, "it is falling after a tenth of a second");
    run(cloth, nobody, 3.0);
    double lowest = 1e9, highest = -1e9;
    for (const Vec3& p : cloth.positions()[0]) {
        lowest = std::min(lowest, p.y);
        highest = std::max(highest, p.y);
    }
    std::printf("      on the floor: y %.2f..%.2f, fastest %.2f cm/s, stretch %.1f%%\n", lowest, highest, cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    check.that(cloth.sound(), "every position is still a number");
    check.that(lowest > 0.25 && highest < 3.0, "it lies on the floor, not through it");
    check.that(cloth.fastest() < 2.0, "and has come to rest");
    check.that(worstStretch(mesh, cloth.positions()) < 0.03, "without stretching");
    check.that(std::abs(cloth.time() - 3.1) < 1e-6, "time has moved on 3.1 seconds");
}

static void aPinnedClothHangs(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "flag"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 50_cm});
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 150, .z = 0, .tilt = 60});  // held out at an angle
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const Placement start = arranged(doc, mesh);
    Cloth cloth(mesh, start);
    const EdgeParticles* top = mesh.panels[0].along(rect.top());
    for (uint32_t point : top->points) cloth.pin(0, point);
    run(cloth, Body{}, 8.0);
    const double pinnedHeight = start[0][top->points.front()].y;
    double lowest = 1e9;
    for (const Vec3& p : cloth.positions()[0]) lowest = std::min(lowest, p.y);
    std::printf("      hanging: %.1f cm below its pins, fastest %.2f cm/s, stretch %.1f%%\n", pinnedHeight - lowest, cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    check.that(cloth.sound(), "every position is still a number");
    check.that(length(cloth.positions()[0][top->points.front()] - start[0][top->points.front()]) < 1e-12, "the pins have not moved");
    check.that(pinnedHeight - lowest > 49.0 && pinnedHeight - lowest < 51.5, "it hangs its full 50 cm, and no more than 3% longer");
    check.that(cloth.fastest() < 5.0, "and has nearly stopped swinging");
}

static void seamsPullPanelsTogether(Check& check) {
    // A front and a back, facing each other 20 cm apart, sewn down both sides.
    Document doc;
    PanelId front = doc.add(Panel{.name = "front"});
    OpRef<Rect> frontRect = doc.add(front, Rect{.width = 20_cm, .height = 30_cm});
    PanelId back = doc.add(Panel{.name = "back"});
    OpRef<Rect> backRect = doc.add(back, Rect{.width = 20_cm, .height = 30_cm});
    doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    doc.add(Seam{.a = {front, frontRect.left()}, .b = {back, backRect.right()}});
    doc.arrange(front, Arrangement{.placed = true, .x = 0, .y = 100, .z = 10});
    doc.arrange(back, Arrangement{.placed = true, .x = 0, .y = 100, .z = -10, .turn = 180});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    cloth.pin(0, mesh.panels[0].along(frontRect.top())->points.back());   // hold the front up by its two top corners
    cloth.pin(0, mesh.panels[0].along(frontRect.top())->points.front());
    const auto widestGap = [&] {
        double gap = 0.0;
        for (const Stitch& s : mesh.stitches) gap = std::max(gap, length(cloth.positions()[s.panelA][s.pointA] - cloth.positions()[s.panelB][s.pointB]));
        return gap;
    };
    check.that(std::abs(widestGap() - 20.0) < 1e-9 && mesh.stitches.size() == 32, "the stitches start 20 cm long");
    run(cloth, Body{}, 0.1);
    check.that(widestGap() > 10.0 && widestGap() < 20.0, "they are drawn in at the speed of a thread, not at once");
    run(cloth, Body{}, 4.0);
    std::printf("      sewn: widest stitch %.3f cm, fastest %.2f cm/s, stretch %.1f%%\n", widestGap(), cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    check.that(cloth.sound() && widestGap() < 0.1, "and end up closed");
    double lowestBack = 1e9;
    for (const Vec3& p : cloth.positions()[1]) lowestBack = std::min(lowestBack, p.y);
    check.that(lowestBack > 60.0, "the back hangs from the front instead of falling to the floor");
    check.that(worstStretch(mesh, cloth.positions()) < 0.05, "without stretching");
}

static void clothRestsOnABody(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "cloth"});
    doc.add(panel, Rect{.width = 50_cm, .height = 50_cm});
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 80, .z = 0, .tilt = 90});  // lying flat, above the ball
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Body ball;
    ball.parts.push_back({{0, 50, 0}, {0, 50, 0}, 15, 15});
    Cloth cloth(mesh, arranged(doc, mesh));
    double deepest = 1e9;
    for (int i = 0; i < 240; ++i) {
        cloth.step(ball);
        for (const Vec3& p : cloth.positions()[0]) deepest = std::min(deepest, distanceTo(ball, p));
    }
    double highest = -1e9, lowest = 1e9;
    for (const Vec3& p : cloth.positions()[0]) {
        highest = std::max(highest, p.y);
        lowest = std::min(lowest, p.y);
    }
    std::printf("      on a ball: y %.1f..%.1f, never closer than %.2f cm, fastest %.2f cm/s\n", lowest, highest, deepest, cloth.fastest());
    check.that(cloth.sound() && deepest > 0.2, "no particle ever goes inside the ball");
    check.that(highest > 64.5 && highest < 66.5, "its middle rests on top of the ball");
    check.that(lowest < 55.0 && lowest > 30.0, "its sides hang down around it");
}

static void aSewnTubeStaysOnTheHips(Check& check) {
    // A band: one rectangle wrapped all the way round at the waist and sewn into a tube.
    // 80 cm around: loose at the waist (68) and too narrow to pass the hips (94).
    Document doc;
    PanelId band = doc.add(Panel{.name = "band"});
    OpRef<Rect> rect = doc.add(band, Rect{.width = 80_cm, .height = 6_cm});
    doc.add(Seam{.a = {band, rect.left()}, .b = {band, rect.right()}});
    const Body body = mannequin();
    const double waistLine = 0.62 * 170, hipLine = 0.53 * 170;
    doc.arrange(band, Arrangement{.placed = true, .x = 0, .y = waistLine + 2, .z = 80.0 / (2 * pi), .wrap = 358});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    run(cloth, body, 5.0);
    double lowest = 1e9, highest = -1e9, deepest = 1e9;
    for (const Vec3& p : cloth.positions()[0]) {
        lowest = std::min(lowest, p.y);
        highest = std::max(highest, p.y);
        deepest = std::min(deepest, distanceTo(body, p));
    }
    std::printf("      band: y %.1f..%.1f (waist %.1f, hips %.1f), nearest to the body %.2f cm, fastest %.2f cm/s, stretch %.1f%%\n", lowest, highest, waistLine, hipLine,
                deepest, cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    check.that(cloth.sound(), "every position is still a number");
    check.that(deepest > 0.15, "no particle is inside the body");
    check.that(highest < waistLine + 5 - 1.0, "it has slipped down from where it was put");
    check.that(lowest > hipLine - 4.0, "and is caught by the hips");
    // Where it is wedged on the hips single triangle sides give a little; around, it keeps its length.
    double around = 0.0;
    const EdgeParticles* hem = mesh.panels[0].along(rect.bottom());
    for (std::size_t i = 1; i < hem->points.size(); ++i) around += length(cloth.positions()[0][hem->points[i]] - cloth.positions()[0][hem->points[i - 1]]);
    std::printf("      its lower edge is now %.1f cm around (cut 80)\n", around);
    check.that(around < 80.0 * 1.04 && around > 80.0 * 0.97, "its lower edge is still 80 cm around, within 4%");
    check.that(cloth.fastest() < 10.0, "and it has settled");

    // The same every time.
    Cloth again(mesh, arranged(doc, mesh));
    run(again, body, 5.0);
    check.that(length(again.positions()[0][7] - cloth.positions()[0][7]) == 0.0, "the same input gives exactly the same motion");
}

static void clothStaysDrapedWhenThePatternChanges(Check& check) {
    // A cloth hanging over a ball; then the pattern is made wider.
    Document doc;
    PanelId panel = doc.add(Panel{.name = "cloth"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 50_cm, .height = 50_cm});
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 80, .z = 0, .tilt = 90});
    Body ball;
    ball.parts.push_back({{0, 50, 0}, {0, 50, 0}, 15, 15});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    run(cloth, ball, 3.0);
    const auto heights = [](const Cloth& c, std::size_t panel = 0) {
        std::pair<double, double> range{1e9, -1e9};
        for (const Vec3& p : c.positions()[panel]) range = {std::min(range.first, p.y), std::max(range.second, p.y)};
        return range;
    };
    const auto draped = heights(cloth);

    // The same pattern again changes nothing.
    Cloth same = Cloth::continued(mesh, arranged(doc, mesh), cloth, mesh);
    double moved = 0.0;
    for (std::size_t i = 0; i < mesh.panels[0].points.size(); ++i) moved = std::max(moved, length(same.positions()[0][i] - cloth.positions()[0][i]));
    check.that(moved < 1e-9 && same.time() == cloth.time(), "carried over to the same pattern, every particle stays where it is");

    // Wider by 10 cm: the cloth there was stays draped, the new strip continues from its edge.
    doc.set(rect, &Rect::width, 60_cm);
    const ClothMesh wider = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth carried = Cloth::continued(wider, arranged(doc, wider), cloth, mesh);
    const auto after = heights(carried);
    std::printf("      draped y %.1f..%.1f; carried to the wider pattern y %.1f..%.1f\n", draped.first, draped.second, after.first, after.second);
    check.that(carried.positions()[0].size() > cloth.positions()[0].size(), "the wider pattern has more particles");
    check.that(std::abs(after.second - draped.second) < 0.5, "its middle is still resting on the ball, not back in the air");
    check.that(after.first < 55.0, "and its sides still hang");
    check.that(carried.time() == cloth.time(), "time carries on");
    // Particles at spots that had a particle before are exactly where those were.
    int shared = 0;
    double off = 0.0;
    for (std::size_t n = 0; n < wider.panels[0].points.size(); ++n)
        for (std::size_t i = 0; i < mesh.panels[0].points.size(); ++i)
            if (std::hypot(mesh.panels[0].points[i].x - wider.panels[0].points[n].x, mesh.panels[0].points[i].y - wider.panels[0].points[n].y) < 1e-9) {
                ++shared;
                off = std::max(off, length(carried.positions()[0][n] - cloth.positions()[0][i]));
            }
    check.that(shared > 300 && off < 1e-9, "particles that exist in both patterns are exactly where they were");
    run(carried, ball, 2.0);
    double deepest = 1e9;
    for (const Vec3& p : carried.positions()[0]) deepest = std::min(deepest, distanceTo(ball, p));
    std::printf("      two seconds on: stretch %.1f%%, nearest to the ball %.2f cm, fastest %.2f cm/s\n", 100 * worstStretch(wider, carried.positions()), deepest, carried.fastest());
    check.that(carried.sound() && deepest > 0.2 && worstStretch(wider, carried.positions()) < 0.03, "it settles again, whole and outside the ball");

    // A second panel added later starts where it is arranged; the first stays draped.
    PanelId extra = doc.add(Panel{.name = "extra"});
    doc.add(extra, Rect{.width = 10_cm, .height = 10_cm});
    doc.arrange(extra, Arrangement{.placed = true, .x = 80, .y = 120, .z = 0});
    const ClothMesh two = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth withExtra = Cloth::continued(two, arranged(doc, two), carried, wider);
    check.that(std::abs(heights(withExtra, 1).second - 125.0) < 1e-9, "a new panel starts where it is arranged");
    check.that(std::abs(heights(withExtra, 0).second - heights(carried).second) < 1e-6, "while the first stays draped");
    // Asked to, a panel is placed again.
    Cloth again = Cloth::continued(two, arranged(doc, two), withExtra, two, {panel});
    check.that(std::abs(heights(again, 0).first - 80.0) < 1e-9 && std::abs(heights(again, 0).second - 80.0) < 1e-9, "a panel listed to be placed again is back at its arrangement");
}

static void clothDoesNotPassThroughCloth(Check& check) {
    // Two sheets lying flat, one 10 cm above the other, dropped onto the floor; the upper one is smaller and off-centre.
    Document doc;
    PanelId lower = doc.add(Panel{.name = "lower"});
    doc.add(lower, Rect{.width = 40_cm, .height = 40_cm});
    PanelId upper = doc.add(Panel{.name = "upper"});
    doc.add(upper, Rect{.width = 25_cm, .height = 25_cm});
    doc.arrange(lower, Arrangement{.placed = true, .x = 0, .y = 20, .z = 0, .tilt = 90});
    doc.arrange(upper, Arrangement{.placed = true, .x = 3.3, .y = 30, .z = 2.7, .tilt = 90, .roll = 20});
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const auto settle = [&](bool collides) {
        Fabric fabric;
        fabric.collidesWithItself = collides;
        Cloth cloth(mesh, arranged(doc, mesh), fabric);
        run(cloth, Body{}, 3.0);
        return cloth;
    };
    const Cloth with = settle(true), without = settle(false);
    const auto closest = [&](const Cloth& cloth) {
        double nearest = 1e9;
        for (const Vec3& a : cloth.positions()[1])
            for (const Vec3& b : cloth.positions()[0]) nearest = std::min(nearest, length(a - b));
        return nearest;
    };
    const auto meanHeight = [](const Cloth& cloth, std::size_t panel) {
        double sum = 0.0;
        for (const Vec3& p : cloth.positions()[panel]) sum += p.y;
        return sum / cloth.positions()[panel].size();
    };
    std::printf("      two sheets: closest particles %.2f cm apart (%.2f without), upper rests %.2f cm above the lower, fastest %.2f cm/s\n", closest(with),
                closest(without), meanHeight(with, 1) - meanHeight(with, 0), with.fastest());
    check.that(with.sound() && with.fastest() < 2.0, "both sheets come to rest");
    check.that(closest(with) > 1.1, "no particle of one is closer than the cloth's thickness to the other");
    check.that(meanHeight(with, 1) - meanHeight(with, 0) > 0.8, "the upper sheet lies on the lower one");
    check.that(closest(without) < 0.5, "switched off, the sheets end up in the same place");
    check.that(worstStretch(mesh, with.positions()) < 0.03, "and neither is stretched");

    // A sheet folded back on itself by a seam does not collapse into itself.
    Document fold;
    PanelId sheet = fold.add(Panel{.name = "sheet"});
    OpRef<Rect> rect = fold.add(sheet, Rect{.width = 40_cm, .height = 20_cm});
    fold.add(Seam{.a = {sheet, rect.left()}, .b = {sheet, rect.right()}, .reversed = true});  // left edge onto right edge, same way up: a flat loop
    fold.arrange(sheet, Arrangement{.placed = true, .x = 0, .y = 15, .z = 0, .tilt = 90});
    const ClothMesh loop = meshOf(fold, evaluate(fold), {.particleDistance = 2_cm});
    Cloth folded(loop, arranged(fold, loop));
    run(folded, Body{}, 4.0);
    double tooClose = 0;
    const auto& now = folded.positions()[0];
    const auto& pattern = loop.panels[0].points;
    std::vector<char> sewn(now.size(), 0);
    for (const Stitch& stitch : loop.stitches) sewn[stitch.pointA] = sewn[stitch.pointB] = 1;
    for (std::size_t i = 0; i < now.size(); ++i)
        for (std::size_t j = i + 1; j < now.size(); ++j)
            if (!sewn[i] && !sewn[j] && std::hypot(pattern[i].x - pattern[j].x, pattern[i].y - pattern[j].y) > 3.0 && length(now[i] - now[j]) < 1.0) ++tooClose;
    double widest = 0.0;
    for (const Stitch& stitch : loop.stitches) widest = std::max(widest, length(now[stitch.pointA] - now[stitch.pointB]));
    std::printf("      folded loop: widest stitch %.2f cm, %g pairs of far-apart particles closer than 1 cm\n", widest, tooClose);
    check.that(folded.sound() && widest < 0.1, "its seam closes");
    check.that(tooClose == 0, "and its two layers stay apart");
}

static void aSurfaceFromAFileIsABody(Check& check) {
    // A cube written as an OBJ file, with quads, like a modelling program writes them.
    const char* path = "/tmp/clothcad_cube.obj";
    {
        std::FILE* file = std::fopen(path, "w");
        std::fputs("# a cube 20 cm across, standing on the floor\nv -10 0 -10\nv 10 0 -10\nv 10 20 -10\nv -10 20 -10\nv -10 0 10\nv 10 0 10\nv 10 20 10\nv -10 20 10\n"
                   "vn 0 0 1\nf 5//1 6//1 7//1 8//1\nf 2 1 4 3\nf 6/1 2/1 3/1 7/1\nf 1 5 8 4\nf -1 -2 -6 -5\nf 1 2 6 5\n",
                   file);
        std::fclose(file);
    }
    const LoadedSurface cube = loadObj(path);
    check.that(cube.problem.empty() && cube.surface.points.size() == 8 && cube.surface.triangles.size() == 12, "eight corners and six quads become twelve triangles");
    check.that(!loadObj("/tmp/no_such_file.obj").problem.empty(), "a missing file is reported");

    const Body body = bodyFrom(cube.surface);
    check.that(std::abs(distanceTo(body, {0, 10, 12.5}) - 2.5) < 0.05, "2.5 cm in front of a face is 2.5 cm away");
    check.that(std::abs(distanceTo(body, {0, 10, 8}) + 2.0) < 0.05, "2 cm behind it is 2 cm inside");
    check.that(distanceTo(body, {0, 10, 0}) < -3.0, "the middle is inside");
    check.that(distanceTo(body, {0, 60, 0}) > 3.0 && distanceTo(body, {500, 0, 0}) > 3.0, "far away is outside");
    check.that(std::abs(distanceTo(body, {11.5, 21.5, 0}) - std::hypot(1.5, 1.5)) < 0.15, "beyond an edge the distance is to the edge, and outside");
    const Contact front = body.scanned->contact({0, 10, 11});
    check.that(front.outward.z > 0.99, "the way out of a face is square to it");

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
    check.that(cloth.sound() && deepest > 0.1, "the cloth never goes inside");
    check.that(highest > 20.2 && highest < 21.0 && lowest < 15.0, "it lies on top with its edges hanging over");
}

int main() {
    return runTests({
        {"a rectangle is covered by triangles", aRectangleIsCoveredByTriangles},
        {"curved and hollow panels mesh", curvedAndHollowPanelsMesh},
        {"sewn edges pair up", sewnEdgesPairUp},
        {"a tube sews to itself", aTubeSewsToItself},
        {"bad outlines are reported, not thrown", badOutlinesAreReportedNotThrown},
        {"panels are arranged in space", panelsAreArrangedInSpace},
        {"the mannequin is a body", theMannequinIsABody},
        {"a dropped cloth comes to rest on the floor", aDroppedClothComesToRestOnTheFloor},
        {"a pinned cloth hangs", aPinnedClothHangs},
        {"seams pull panels together", seamsPullPanelsTogether},
        {"cloth rests on a body", clothRestsOnABody},
        {"a sewn tube stays on the hips", aSewnTubeStaysOnTheHips},
        {"cloth stays draped when the pattern changes", clothStaysDrapedWhenThePatternChanges},
        {"cloth does not pass through cloth", clothDoesNotPassThroughCloth},
        {"a surface from a file is a body", aSurfaceFromAFileIsABody},
    });
}
