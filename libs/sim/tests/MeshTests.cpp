// Unit tests of Mesh.cpp: panels become triangles; sewn edges get particles that pair up.

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

TEST_CASE("a rectangle is covered by triangles")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 40_cm});
    const ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    EXPECT((cloth.panels.size() == 1 && cloth.panels[0].problem.empty()), "one mesh, no problem");
    const PanelMesh& mesh = cloth.panels[0];
    EXPECT((std::abs(mesh.area() - 1200.0) < 1e-6), "the triangles cover exactly the rectangle");
    EXPECT((watertight(mesh)), "no side is shared by more than two triangles");
    EXPECT((mesh.along(rect.bottom()) && mesh.along(rect.bottom())->points.size() == 16), "30 cm at 2 cm apart is 15 pieces along the bottom");
    EXPECT((longestSide(mesh) < 2.0 * 2.0), "no triangle is much larger than asked");
    EXPECT((mesh.points.size() > 250 && mesh.points.size() < 450), "about one particle per 3.5 square cm");
    for (const auto& t : mesh.triangles) {
        const Vec2 a = mesh.points[t[0]], b = mesh.points[t[1]], c = mesh.points[t[2]];
        if ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) <= 0.0) {
            EXPECT((false), "every triangle is counter-clockwise");
            break;
        }
    }
    const ClothMesh fine = meshOf(doc, evaluate(doc), {.particleDistance = 1_cm});
    EXPECT((fine.panels[0].points.size() > 3 * mesh.points.size()), "half the distance is about four times the particles");
}

TEST_CASE("each panel is meshed as finely as its own particle distance says")
{
    Document doc;
    PanelId coarse = doc.add(Panel{.name = "coarse"});
    doc.add(coarse, Rect{.width = 30_cm, .height = 30_cm});
    PanelId fine = doc.add(Panel{.name = "fine", .particleDistance = 1.0});
    doc.add(fine, Rect{.width = 30_cm, .height = 30_cm});
    const ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 3_cm});
    EXPECT((cloth.find(fine)->points.size() > 5 * cloth.find(coarse)->points.size()), "the panel with its own distance is finer than the default");

    doc.setParticleDistance(coarse, 1.0);
    const ClothMesh same = meshOf(doc, evaluate(doc), {.particleDistance = 3_cm});
    EXPECT((same.find(fine)->points.size() == same.find(coarse)->points.size()), "the same distance gives the same particles");
}

TEST_CASE("a seam between panels of different distances is cut as finely as the finer one")
{
    Document doc;
    PanelId a = doc.add(Panel{.name = "a", .particleDistance = 4.0});
    OpRef<Rect> ra = doc.add(a, Rect{.width = 20_cm, .height = 20_cm});
    PanelId b = doc.add(Panel{.name = "b", .particleDistance = 1.0});
    OpRef<Rect> rb = doc.add(b, Rect{.width = 20_cm, .height = 20_cm});
    doc.add(Seam{.a = {a, ra.right()}, .b = {b, rb.left()}});
    const ClothMesh cloth = meshOf(doc, evaluate(doc));
    EXPECT((cloth.find(a)->along(ra.right())->points.size() == 21 && cloth.find(b)->along(rb.left())->points.size() == 21), "both sides have a particle every centimetre");
    EXPECT((cloth.stitches.size() == 21), "so they pair up one to one");
}

TEST_CASE("curved and hollow panels mesh")
{
    Document doc;
    PanelId disc = doc.add(Panel{.name = "disc"});
    doc.add(disc, Circle{.radius = 20_cm});
    PanelId ring = doc.add(Panel{.name = "ring"});
    doc.add(ring, RingSector{.innerLength = 70_cm, .sweep = 360_deg, .depth = 30_cm});
    PanelId half = doc.add(Panel{.name = "half"});
    doc.add(half, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 30_cm});
    const ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    EXPECT((cloth.panels.size() == 3), "three meshes");
    for (const PanelMesh& mesh : cloth.panels) EXPECT((mesh.problem.empty() && watertight(mesh)), "each meshes cleanly");

    const double discArea = pi * 20 * 20;
    EXPECT((std::abs(cloth.find(disc)->area() - discArea) < 0.01 * discArea), "the disc within 1% of its area");
    const double inner = 70.0 / (2 * pi), outer = inner + 30;
    const double ringArea = pi * (outer * outer - inner * inner);
    EXPECT((std::abs(cloth.find(ring)->area() - ringArea) < 0.01 * ringArea), "the full ring within 1%: its hole is left open");
    EXPECT((cloth.find(ring)->edges.size() == 2), "a full ring has two edges; its slit is closed up");
    const double halfInner = 70.0 / pi, halfOuter = halfInner + 30;
    const double halfArea = pi / 2 * (halfOuter * halfOuter - halfInner * halfInner);
    EXPECT((std::abs(cloth.find(half)->area() - halfArea) < 0.01 * halfArea), "half a ring within 1%");
}

TEST_CASE("sewn edges pair up")
{
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
    EXPECT((a && b && a->points.size() == b->points.size() && a->points.size() == 61), "both edges get the longer one's 60 pieces");
    EXPECT((cloth.stitches.size() == 61), "one stitch per particle");
    EXPECT((bandMesh.problem.empty() && skirtMesh.problem.empty()), "both still mesh");
    // The band's bottom runs left to right, the skirt's top right to left: start meets end.
    const Stitch first = cloth.stitches.front();
    const Vec2 onBand = bandMesh.points[first.pointA], onSkirt = skirtMesh.points[first.pointB];
    EXPECT((std::abs(onBand.x) < 1e-9 && std::abs(onSkirt.x) < 1e-9), "the band's left end meets the skirt's left end");

    Seam reversed = doc.get(seam);
    reversed.reversed = true;
    doc.set(seam, reversed);
    cloth = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    const Vec2 twisted = cloth.find(skirt)->points[cloth.stitches.front().pointB];
    EXPECT((std::abs(twisted.x - 120.0) < 1e-9), "reversed, it meets the other end");

    // A construction panel is not cloth; a broken seam makes no stitches.
    doc.setConstruction(band, true);
    cloth = meshOf(doc, evaluate(doc));
    EXPECT((cloth.panels.size() == 1 && cloth.stitches.empty()), "a base panel is left out, with its stitches");
}

TEST_CASE("a tube sews to itself")
{
    Document doc;
    PanelId tube = doc.add(Panel{.name = "tube"});
    OpRef<Rect> rect = doc.add(tube, Rect{.width = 30_cm, .height = 20_cm});
    doc.add(Seam{.a = {tube, rect.left()}, .b = {tube, rect.right()}});
    const ClothMesh cloth = meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
    EXPECT((cloth.stitches.size() == 11), "the two sides pair up along their 10 pieces");
    bool level = true;
    for (const Stitch& s : cloth.stitches)
        level = level && std::abs(cloth.panels[0].points[s.pointA].y - cloth.panels[0].points[s.pointB].y) < 1e-9;
    EXPECT((level), "each stitch joins points at the same height");
}

TEST_CASE("bad outlines are reported, not thrown")
{
    Document doc;
    PanelId bow = doc.add(Panel{.name = "bow"});
    OpRef<Rect> rect = doc.add(bow, Rect{.width = 20_cm, .height = 20_cm});
    doc.add(bow, AdjustPoint{.corner = rect.topRight(), .dx = -30_cm, .dy = 0_cm});  // dragged across the opposite side
    PanelId fine = doc.add(Panel{.name = "fine"});
    doc.add(fine, Rect{.width = 10_cm, .height = 10_cm});
    const ClothMesh cloth = meshOf(doc, evaluate(doc));
    EXPECT((cloth.panels.size() == 2), "both panels are listed");
    EXPECT((!cloth.find(bow)->problem.empty() && cloth.find(bow)->triangles.empty()), "the self-crossing one says why it has no triangles");
    EXPECT((cloth.find(fine)->problem.empty()), "the other is unaffected");
    PanelId huge = doc.add(Panel{.name = "huge"});
    doc.add(huge, Rect{.width = 5000_cm, .height = 5000_cm});
    EXPECT((!meshOf(doc, evaluate(doc), {.particleDistance = 0.1_cm}).find(huge)->problem.empty()), "a mesh that would be enormous is refused");
}

TEST_CASE("a stitch says whether its seam is reversed")
{
    Document doc;
    const PanelId a = doc.add(Panel{.name = "a"});
    const OpRef<Rect> first = doc.add(a, Rect{.width = 10_cm, .height = 10_cm});
    const PanelId b = doc.add(Panel{.name = "b"});
    const OpRef<Rect> second = doc.add(b, Rect{.width = 10_cm, .height = 10_cm});
    const SeamId plain = doc.add(Seam{.a = {a, first.right()}, .b = {b, second.left()}});
    doc.add(Seam{.a = {a, first.top()}, .b = {b, second.top()}, .reversed = true});
    const ClothMesh mesh = meshOf(doc, evaluate(doc));
    REQUIRE(!mesh.stitches.empty());
    bool right = true;
    for (const Stitch& stitch : mesh.stitches) right = right && stitch.reversed == !(stitch.seam == plain);
    EXPECT(right, "as its seam has it");
}
