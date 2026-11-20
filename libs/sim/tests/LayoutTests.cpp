// Unit tests of Layout.cpp: panels arranged in space.

#include <algorithm>
#include <cmath>
#include <cstdio>
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

TEST_CASE("a new panel's first place is where the table has it, standing in front of the body")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "front", .x = 100, .y = 50, .angle = 30});
    doc.add(panel, Rect{.width = 40_cm, .height = 60_cm});
    const Result shapes = evaluate(doc);
    const ClothMesh cloth = meshOf(doc, shapes, {.particleDistance = 4_cm});
    const Arrangement standing = arrangementOf(doc, cloth.panels[0]);
    const Arrangement first = firstPlace(doc.get(panel), shapes[panel]);
    EXPECT((first.placed), "it is a place of its own");
    EXPECT((std::abs(first.x - standing.x) < 1e-9 && std::abs(first.y - standing.y) < 1e-9 && first.z == standing.z && first.roll == 30.0),
           "the same place the panel stood in before it had one: nothing jumps");
    EXPECT((!firstPlace(doc.get(panel), Pattern{}).placed), "an outline with no corners gets no place");
}

TEST_CASE("a panel's anchor moves with its sewn edges, so it grows away from them")
{
    Document doc;
    const Param length = doc.add(Parameter{.name = "length", .value = 40});
    const PanelId skirt = doc.add(Panel{.name = "skirt"});
    const OpRef<Rect> rect = doc.add(skirt, Rect{.width = 30_cm, .height = length});
    const PanelId band = doc.add(Panel{.name = "band"});
    const OpRef<Rect> bandRect = doc.add(band, Rect{.width = 30_cm, .height = 4_cm});
    const Pattern before = evaluate(doc)[skirt];
    doc.set(length, Expr(50));
    const Pattern after = evaluate(doc)[skirt];
    const Vec2 middle{15, 20};

    const Vec2 free = anchorAfterChange(doc, skirt, before, after, middle);
    EXPECT((free == Vec2{15, 25}), "sewn to nothing, it grows about the middle of its box");

    doc.add(Seam{.a = {skirt, rect.top()}, .b = {band, bandRect.bottom()}, .reversed = true});
    const Vec2 waist = anchorAfterChange(doc, skirt, before, after, middle);
    EXPECT((waist == Vec2{15, 30}), "sewn at the top, the anchor moves up with the top: the skirt grows at the hem");

    doc.add(Seam{.a = {skirt, rect.left()}, .b = {skirt, rect.right()}});
    EXPECT((anchorAfterChange(doc, skirt, before, after, middle) == Vec2{15, 30}), "sides that grew longer do not hold it; the waist, which kept its length, does");
}

TEST_CASE("a panel far out on the table wraps round to stand near the body")
{
    Document doc;
    const PanelId near = doc.add(Panel{.name = "near", .x = -20, .y = 80});
    doc.add(near, Rect{.width = 40_cm, .height = 40_cm});
    const PanelId far = doc.add(Panel{.name = "far", .x = 380, .y = 80});  // two widths of the space to the right
    doc.add(far, Rect{.width = 40_cm, .height = 40_cm});
    const PanelId high = doc.add(Panel{.name = "high", .x = -20, .y = 480});
    doc.add(high, Rect{.width = 40_cm, .height = 40_cm});
    const Result shapes = evaluate(doc);
    const Arrangement n = firstPlace(doc.get(near), shapes[near]);
    const Arrangement f = firstPlace(doc.get(far), shapes[far]);
    const Arrangement h = firstPlace(doc.get(high), shapes[high]);
    std::printf("      near %.0f, %.0f, %.0f; far %.0f, %.0f, %.0f; high %.0f, %.0f, %.0f\n", n.x, n.y, n.z, f.x, f.y, f.z, h.x, h.y, h.z);
    EXPECT((n.x == 0.0 && n.y == 100.0 && n.z == unarrangedDistance), "within reach of the body, it stands where the table has it");
    EXPECT((std::abs(f.x) <= standingAcross && f.y == 100.0), "far to the side, it wraps round to stand beside the body");
    EXPECT((f.z == unarrangedDistance + 2 * standingStepForward), "a step further forward for each time round, clear of the first panels");
    EXPECT((h.y >= standingLowest && h.y < standingHighest && h.z > unarrangedDistance), "far up the table, it wraps round in height too");
}

TEST_CASE("panels are arranged in space")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "front", .x = 30, .y = 50});
    doc.add(panel, Rect{.width = 40_cm, .height = 60_cm});
    ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 4_cm});
    Placement placed = arranged(doc, cloth);
    EXPECT((placed.size() == 1 && placed[0].size() == cloth.panels[0].points.size()), "one position per particle");
    // Not arranged yet: where the table has it, upright, in front of the body.
    EXPECT((!arrangementOf(doc, cloth.panels[0]).placed), "it reports that it was not arranged");
    bool upright = true;
    for (std::size_t i = 0; i < placed[0].size(); ++i) {
        const Vec2 flat = cloth.panels[0].points[i];
        const Vec3 p = placed[0][i];
        upright = upright && std::abs(p.x - (30 + flat.x)) < 1e-9 && std::abs(p.y - (50 + flat.y)) < 1e-9 && std::abs(p.z - unarrangedDistance) < 1e-9;
    }
    EXPECT((upright), "unarranged, it stands where the table has it");

    // Arranged: its centre goes where asked; a quarter turn puts its width along z.
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 110, .z = 20, .turn = 90});
    placed = arranged(doc, cloth);
    Vec3 low = placed[0][0], high = placed[0][0];
    for (const Vec3& p : placed[0]) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    EXPECT((std::abs(high.x - low.x) < 1e-9 && std::abs((high.z - low.z) - 40.0) < 1e-9 && std::abs((high.y - low.y) - 60.0) < 1e-9), "turned a quarter, it is edge-on");
    EXPECT((std::abs((low.z + high.z) / 2 - 20.0) < 1e-9 && std::abs((low.y + high.y) / 2 - 110.0) < 1e-9), "centred where it was put");

    // Wrapped half way round: both side edges end up directly behind the middle, a diameter apart.
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 110, .z = 20, .wrap = 180});
    placed = arranged(doc, cloth);
    const double radius = 40.0 / pi;
    double deepest = 0.0, widest = 0.0;
    for (const Vec3& p : placed[0]) {
        deepest = std::min(deepest, p.z - 20.0);
        widest = std::max(widest, std::abs(p.x));
    }
    EXPECT((std::abs(deepest + radius) < 1e-6 && std::abs(widest - radius) < 0.2), "wrapped half round, it is half a cylinder behind its middle");
    // Bending does not stretch: neighbours along a row stay the same distance apart along the arc.
    const EdgeParticles* bottom = cloth.panels[0].along(OpRef<Rect>(doc.operations(panel).front()).bottom());
    double arc = 0.0;
    for (std::size_t i = 1; i < bottom->points.size(); ++i) arc += length(placed[0][bottom->points[i]] - placed[0][bottom->points[i - 1]]);
    EXPECT((arc > 39.0 && arc <= 40.0 + 1e-9), "and its bottom edge is still 40 cm long");
}
