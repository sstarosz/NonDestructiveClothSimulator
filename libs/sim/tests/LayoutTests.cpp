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

TEST_CASE("panels are arranged in space")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "front", .x = 100, .y = 50});
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
        upright = upright && std::abs(p.x - (100 + flat.x)) < 1e-9 && std::abs(p.y - (50 + flat.y)) < 1e-9 && std::abs(p.z - unarrangedDistance) < 1e-9;
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
