// Unit tests of Cloth.cpp: the cloth in motion.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
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

TEST_CASE("a dropped cloth comes to rest on the floor")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "cloth"});
    doc.add(panel, Rect{.width = 40_cm, .height = 40_cm});
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 30, .z = 0, .tilt = 80});  // nearly lying down, 30 cm up
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    const Body nobody;
    run(cloth, nobody, 0.1);
    EXPECT((cloth.fastest() > 50.0), "it is falling after a tenth of a second");
    run(cloth, nobody, 3.0);
    double lowest = 1e9, highest = -1e9;
    for (const Vec3& p : cloth.positions()[0]) {
        lowest = std::min(lowest, p.y);
        highest = std::max(highest, p.y);
    }
    std::printf("      on the floor: y %.2f..%.2f, fastest %.2f cm/s, stretch %.1f%%\n", lowest, highest, cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    EXPECT((cloth.sound()), "every position is still a number");
    EXPECT((lowest > 0.25 && highest < 3.0), "it lies on the floor, not through it");
    EXPECT((cloth.fastest() < 2.0), "and has come to rest");
    EXPECT((worstStretch(mesh, cloth.positions()) < 0.03), "without stretching");
    EXPECT((std::abs(cloth.time() - 3.1) < 1e-6), "time has moved on 3.1 seconds");
}

TEST_CASE("a pinned cloth hangs")
{
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
    EXPECT((cloth.sound()), "every position is still a number");
    EXPECT((length(cloth.positions()[0][top->points.front()] - start[0][top->points.front()]) < 1e-12), "the pins have not moved");
    EXPECT((pinnedHeight - lowest > 49.0 && pinnedHeight - lowest < 51.5), "it hangs its full 50 cm, and no more than 3% longer");
    EXPECT((cloth.fastest() < 5.0), "and has nearly stopped swinging");
}

TEST_CASE("seams pull panels together")
{
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
    EXPECT((std::abs(widestGap() - 20.0) < 1e-9 && mesh.stitches.size() == 32), "the stitches start 20 cm long");
    run(cloth, Body{}, 0.1);
    EXPECT((widestGap() > 10.0 && widestGap() < 20.0), "they are drawn in at the speed of a thread, not at once");
    run(cloth, Body{}, 4.0);
    std::printf("      sewn: widest stitch %.3f cm, fastest %.2f cm/s, stretch %.1f%%\n", widestGap(), cloth.fastest(), 100 * worstStretch(mesh, cloth.positions()));
    EXPECT((cloth.sound() && widestGap() < 0.1), "and end up closed");
    double lowestBack = 1e9;
    for (const Vec3& p : cloth.positions()[1]) lowestBack = std::min(lowestBack, p.y);
    EXPECT((lowestBack > 60.0), "the back hangs from the front instead of falling to the floor");
    EXPECT((worstStretch(mesh, cloth.positions()) < 0.05), "without stretching");
}

TEST_CASE("cloth rests on a body")
{
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
    EXPECT((cloth.sound() && deepest > 0.2), "no particle ever goes inside the ball");
    EXPECT((highest > 64.5 && highest < 66.5), "its middle rests on top of the ball");
    EXPECT((lowest < 55.0 && lowest > 30.0), "its sides hang down around it");
}

TEST_CASE("a sewn tube stays on the hips")
{
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
    EXPECT((cloth.sound()), "every position is still a number");
    EXPECT((deepest > 0.15), "no particle is inside the body");
    EXPECT((highest < waistLine + 5 - 1.0), "it has slipped down from where it was put");
    EXPECT((lowest > hipLine - 4.0), "and is caught by the hips");
    // Where it is wedged on the hips single triangle sides give a little; around, it keeps its length.
    double around = 0.0;
    const EdgeParticles* hem = mesh.panels[0].along(rect.bottom());
    for (std::size_t i = 1; i < hem->points.size(); ++i) around += length(cloth.positions()[0][hem->points[i]] - cloth.positions()[0][hem->points[i - 1]]);
    std::printf("      its lower edge is now %.1f cm around (cut 80)\n", around);
    EXPECT((around < 80.0 * 1.04 && around > 80.0 * 0.97), "its lower edge is still 80 cm around, within 4%");
    EXPECT((cloth.fastest() < 10.0), "and it has settled");

    // The same every time.
    Cloth again(mesh, arranged(doc, mesh));
    run(again, body, 5.0);
    EXPECT((length(again.positions()[0][7] - cloth.positions()[0][7]) == 0.0), "the same input gives exactly the same motion");
}

TEST_CASE("a band sewn shut, with a skirt sewn to it, closes every seam on a body")
{
    // The waistband-and-skirt of the owner's picture: the band's own seam and the
    // skirt's seam meet at one corner. Body friction used to hold that corner
    // 3 cm open while the seams pulled in; now nothing rubs until they are closed.
    for (const double skirtWidth : {70.0, 105.0}) {
        Document doc;
        const PanelId band = doc.add(Panel{.name = "band"});
        const OpRef<Rect> b = doc.add(band, Rect{.width = 72_cm, .height = 6_cm});
        const PanelId skirt = doc.add(Panel{.name = "skirt"});
        const OpRef<Rect> s = doc.add(skirt, Rect{.width = Expr(skirtWidth), .height = 50_cm});
        doc.add(Seam{.a = {band, b.left()}, .b = {band, b.right()}});
        doc.add(Seam{.a = {band, b.bottom()}, .b = {skirt, s.top()}, .reversed = true});
        doc.arrange(band, Arrangement{.placed = true, .x = 0, .y = 108, .z = 72.0 / (2 * pi), .wrap = 350});
        doc.arrange(skirt, Arrangement{.placed = true, .x = 0, .y = 80, .z = skirtWidth / (2 * pi) + 3, .wrap = 340});
        const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
        Cloth cloth(mesh, arranged(doc, mesh));
        run(cloth, mannequin(), 6.0);
        double widest = 0.0;
        for (const Stitch& stitch : mesh.stitches)
            widest = std::max(widest, length(cloth.positions()[stitch.panelA][stitch.pointA] - cloth.positions()[stitch.panelB][stitch.pointB]));
        std::printf("      skirt %.0f cm wide: widest stitch %.2f cm\n", skirtWidth, widest);
        INFO("skirt " << skirtWidth << " cm wide");
        EXPECT((cloth.sound()), "every position is still a number");
        EXPECT((!cloth.sewing()), "the sewing is over");
        // Where the band's own seam meets the skirt, four particles are sewn
        // into one point and pull against each other: that corner stays up to
        // about 0.8 cm open (README, Known limits). Every other stitch closes.
        EXPECT((widest < 1.0), "no stitch is left open, but for a little at the corner where three seams meet");
    }
}

TEST_CASE("a T-shirt body shaped with added points sews shut on a body")
{
    // As examples/06_tshirt_body.cpp: halves with a neck point and an
    // underarm point, shoulders sloped, mirrored; sewn at sides and shoulders,
    // each a drawn edge to a mirrored one, which runs the other way.
    Document doc;
    const auto half = [&](const char* name, double neckDrop) {
        const PanelId panel = doc.add(Panel{.name = name});
        const OpRef<Rect> body = doc.add(panel, Rect{.width = 26_cm, .height = 66_cm});
        const OpRef<AddPoint> neck = doc.add(panel, AddPoint{.at = {body.top(), Expr(18.0 / 26.0)}});
        const OpRef<AddPoint> underarm = doc.add(panel, AddPoint{.at = {body.right(), Expr(44.0 / 66.0)}});
        doc.add(panel, AdjustPoint{.corner = body.topRight(), .dx = -3_cm, .dy = -4_cm});
        doc.add(panel, Curve{.edge = underarm.after(body.right()), .bulge = -2_cm});
        doc.add(panel, AdjustPoint{.corner = body.topLeft(), .dx = 0_cm, .dy = Expr(-neckDrop)});
        doc.add(panel, Bezier{.edge = neck.after(body.top()), .startHandle = {0_cm, Expr(-neckDrop * 0.6)}, .endHandle = {4_cm, 0_cm}});
        const OpRef<Mirror> fold = doc.add(panel, Mirror{.across = body.left()});
        return std::tuple{panel, body, fold};
    };
    const auto [front, frontBody, frontFold] = half("front", 8.0);
    const auto [back, backBody, backFold] = half("back", 2.0);
    const auto sew = [&](EdgeRef a, EdgeRef b) { doc.add(Seam{.a = {front, a}, .b = {back, b}}); };
    sew(frontBody.right(), backFold.image(backBody.right()));
    sew(frontFold.image(frontBody.right()), backBody.right());
    sew(frontBody.top(), backFold.image(backBody.top()));
    sew(frontFold.image(frontBody.top()), backBody.top());
    doc.arrange(front, Arrangement{.placed = true, .x = 0, .y = 108, .z = 16, .wrap = 150});
    doc.arrange(back, Arrangement{.placed = true, .x = 0, .y = 108, .z = -16, .turn = 180, .wrap = 150});

    const Result shapes = evaluate(doc);
    REQUIRE(shapes.ok());
    const ClothMesh mesh = meshOf(doc, shapes, {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    // A dress form: the mannequin without its arms. Its arms hang so close
    // to the body at the underarm that a loose top without sleeves is caught
    // on them (a known limit of the mannequin's pose, not of the pattern).
    Body body = mannequin();
    std::erase_if(body.parts, [](const Capsule& c) { return std::abs(c.a.x) > 15.0 || std::abs(c.b.x) > 15.0; });
    run(cloth, body, 6.0);
    double widest = 0.0, deepest = 1e9, lowest = 1e9;
    for (const Stitch& stitch : mesh.stitches)
        widest = std::max(widest, length(cloth.positions()[stitch.panelA][stitch.pointA] - cloth.positions()[stitch.panelB][stitch.pointB]));
    for (const auto& panel : cloth.positions())
        for (const Vec3& p : panel) deepest = std::min(deepest, distanceTo(body, p)), lowest = std::min(lowest, p.y);
    std::printf("      T-shirt: widest stitch %.2f cm, nearest to the body %.2f cm, hem down to %.1f cm, stretch %.1f%%, fastest %.1f cm/s\n", widest, deepest,
                lowest, 100 * worstStretch(mesh, cloth.positions()), cloth.fastest());
    EXPECT((cloth.sound() && !cloth.sewing()), "it is sewn, and every position is a number");
    EXPECT((widest < 0.6), "every stitch is closed");
    EXPECT((deepest > 0.15), "no particle is inside the body");
    EXPECT((lowest > 70.0), "it hangs from the shoulders, not lower");
    EXPECT((worstStretch(mesh, cloth.positions()) < 0.15 && cloth.fastest() < 5.0), "and has settled, nowhere stretched much");
}

TEST_CASE("cloth stays draped when the pattern changes")
{
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
    const auto heights = [](const Cloth& c, std::size_t which = 0) {
        std::pair<double, double> range{1e9, -1e9};
        for (const Vec3& p : c.positions()[which]) range = {std::min(range.first, p.y), std::max(range.second, p.y)};
        return range;
    };
    const auto draped = heights(cloth);

    // The same pattern again changes nothing.
    Cloth same = Cloth::continued(mesh, arranged(doc, mesh), cloth, mesh);
    double moved = 0.0;
    for (std::size_t i = 0; i < mesh.panels[0].points.size(); ++i) moved = std::max(moved, length(same.positions()[0][i] - cloth.positions()[0][i]));
    EXPECT((moved < 1e-9 && same.time() == cloth.time()), "carried over to the same pattern, every particle stays where it is");

    // Wider by 12 cm: the cloth there was stays draped, and a panel sewn to
    // nothing grows about its middle: a new strip continues from each side.
    doc.set(rect, &Rect::width, 62_cm);
    const ClothMesh wider = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth carried = Cloth::continued(wider, arranged(doc, wider), cloth, mesh);
    const auto after = heights(carried);
    std::printf("      draped y %.1f..%.1f; carried to the wider pattern y %.1f..%.1f\n", draped.first, draped.second, after.first, after.second);
    EXPECT((carried.positions()[0].size() > cloth.positions()[0].size()), "the wider pattern has more particles");
    EXPECT((std::abs(after.second - draped.second) < 0.5), "its middle is still resting on the ball, not back in the air");
    EXPECT((after.first < 55.0), "and its sides still hang");
    EXPECT((carried.time() == cloth.time()), "time carries on");
    // Particles at spots that had a particle before are exactly where those were.
    int shared = 0;
    double off = 0.0;
    for (std::size_t n = 0; n < wider.panels[0].points.size(); ++n)
        for (std::size_t i = 0; i < mesh.panels[0].points.size(); ++i)
            if (std::hypot(mesh.panels[0].points[i].x - (wider.panels[0].points[n].x - 6.0), mesh.panels[0].points[i].y - wider.panels[0].points[n].y) < 1e-9) {
                ++shared;
                off = std::max(off, length(carried.positions()[0][n] - cloth.positions()[0][i]));
            }
    EXPECT((shared > 300 && off < 1e-9), "particles at the same spot from the middle are exactly where they were");
    run(carried, ball, 2.0);
    double deepest = 1e9;
    for (const Vec3& p : carried.positions()[0]) deepest = std::min(deepest, distanceTo(ball, p));
    std::printf("      two seconds on: stretch %.1f%%, nearest to the ball %.2f cm, fastest %.2f cm/s\n", 100 * worstStretch(wider, carried.positions()), deepest, carried.fastest());
    EXPECT((carried.sound() && deepest > 0.2 && worstStretch(wider, carried.positions()) < 0.03), "it settles again, whole and outside the ball");

    // A second panel added later starts where it is arranged; the first stays draped.
    PanelId extra = doc.add(Panel{.name = "extra"});
    doc.add(extra, Rect{.width = 10_cm, .height = 10_cm});
    doc.arrange(extra, Arrangement{.placed = true, .x = 80, .y = 120, .z = 0});
    const ClothMesh two = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth withExtra = Cloth::continued(two, arranged(doc, two), carried, wider);
    EXPECT((std::abs(heights(withExtra, 1).second - 125.0) < 1e-9), "a new panel starts where it is arranged");
    EXPECT((std::abs(heights(withExtra, 0).second - heights(carried).second) < 1e-6), "while the first stays draped");
    // Carried through space while it hangs: a quarter turn and 30 cm up. It
    // goes as a whole, drape and all; nothing starts again.
    const Arrangement was = doc.get(panel).arrangement;
    Arrangement now = was;
    now.y += 30.0;
    now.turn += 90.0;
    const Motion motion{.before = was, .after = now};
    Cloth carriedOff = Cloth::continued(two, arranged(doc, two), withExtra, two, {Moved{panel, motion}});
    double furthest = 0.0;
    for (std::size_t i = 0; i < two.panels[0].points.size(); ++i)
        furthest = std::max(furthest, length(carriedOff.positions()[0][i] - motion(withExtra.positions()[0][i])));
    EXPECT((furthest < 1e-9), "a panel carried through space takes its drape with it");
    EXPECT((std::abs(heights(carriedOff, 0).second - (heights(withExtra, 0).second + 30.0)) < 1e-9), "30 cm higher, as it was carried");
    const Vec3 sideways = motion.turned({1, 0, 0});
    EXPECT((std::abs(sideways.x) < 1e-12 && std::abs(sideways.z + 1.0) < 1e-12), "and turned a quarter about the vertical");
}

TEST_CASE("a held particle is led by the hand and the cloth follows it")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "cloth"});
    doc.add(panel, Rect{.width = 30_cm, .height = 30_cm});
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 5, .z = 0, .tilt = 90});  // lying flat, just above the floor
    const ClothMesh mesh = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    Cloth cloth(mesh, arranged(doc, mesh));
    const Body nobody;
    run(cloth, nobody, 1.0);
    const Vec3 start = cloth.positions()[0][0];
    const Vec3 up = start + Vec3{0, 40, 0};
    cloth.hold(0, 0, up);
    EXPECT((cloth.holding()), "it is held");
    run(cloth, nobody, 0.05);
    const double partWay = cloth.positions()[0][0].y - start.y;
    EXPECT((partWay > 5.0 && partWay < 40.0), "it goes towards the hand at the hand's pace, not at once");
    run(cloth, nobody, 1.5);
    double highest = 0.0;
    for (std::size_t i = 1; i < cloth.positions()[0].size(); ++i) highest = std::max(highest, cloth.positions()[0][i].y);
    std::printf("      held corner at %.1f cm up; the rest of the cloth reaches %.1f cm, stretch %.1f%%\n", cloth.positions()[0][0].y, highest,
                100 * worstStretch(mesh, cloth.positions()));
    EXPECT((length(cloth.positions()[0][0] - up) < 1e-9), "then it is where the hand is");
    EXPECT((highest > 30.0 && worstStretch(mesh, cloth.positions()) < 0.1), "and the cloth hangs from it, whole");
    cloth.letGo();
    run(cloth, nobody, 3.0);
    // It lands on the floor or on a fold of itself, whichever way it happens to fall:
    // a layer or two of cloth up, not more.
    std::printf("      let go, the corner lies %.1f cm up\n", cloth.positions()[0][0].y);
    EXPECT((!cloth.holding() && cloth.positions()[0][0].y < 5.0 && cloth.fastest() < 10.0), "let go, it falls back down and lies there");
    cloth.hold(0, 99999, up);
    EXPECT((!cloth.holding()), "a particle that is not there is not held");
}

TEST_CASE("cloth does not pass through cloth")
{
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
    EXPECT((with.sound() && with.fastest() < 2.0), "both sheets come to rest");
    EXPECT((closest(with) > 1.1), "no particle of one is closer than the cloth's thickness to the other");
    EXPECT((meanHeight(with, 1) - meanHeight(with, 0) > 0.8), "the upper sheet lies on the lower one");
    EXPECT((closest(without) < 0.5), "switched off, the sheets end up in the same place");
    EXPECT((worstStretch(mesh, with.positions()) < 0.03), "and neither is stretched");

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
    EXPECT((folded.sound() && widest < 0.1), "its seam closes");
    EXPECT((tooClose == 0), "and its two layers stay apart");
}
