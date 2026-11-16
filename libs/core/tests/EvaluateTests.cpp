// Unit tests of Evaluate.cpp: every operation evaluated to an outline, and the errors it reports.

#include <cmath>
#include <stdexcept>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/core/Document.h"
#include "clothcad/core/Drawing.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/core/Fields.h"
#include "clothcad/core/File.h"
#include "clothcad/core/Text.h"

using namespace clothcad;

TEST_CASE("parameter may be defined before the one it uses")
{
    Document doc;
    Param hip = doc.add(Parameter{.name = "hip", .value = 0});
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    doc.set(hip, waist + 20_cm);

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((result[hip]), (90.0), "hip follows waist");
}

TEST_CASE("circular parameters are errors")
{
    Document doc;
    Param a = doc.add(Parameter{.name = "a", .value = 1});
    Param b = doc.add(Parameter{.name = "b", .value = a + 1});
    Param fine = doc.add(Parameter{.name = "fine", .value = 5});
    doc.set(a, b + 1);

    Result result = evaluate(doc);
    EXPECT((result.failed(a)), "a is reported");
    EXPECT((result.failed(b)), "b is reported");
    EXPECT((!result.failed(fine)), "an unrelated parameter is not");
    EXPECT_NEAR((result[fine]), (5.0), "an unrelated parameter still has a value");
    EXPECT_THROWS([&] { (void)result[a]; }, std::out_of_range, "a has no value");
}

TEST_CASE("division by zero is an error")
{
    Document doc;
    Param zero = doc.add(Parameter{.name = "zero", .value = 0});
    Param bad = doc.add(Parameter{.name = "bad", .value = 1 / Expr(zero)});

    Result result = evaluate(doc);
    EXPECT((result.failed(bad)), "reported on the dividing parameter");
    EXPECT((!result.failed(zero)), "not on the divisor");
}

TEST_CASE("rectangle has named edges and corners")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 20_cm});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((shape.length(rect.bottom())), (30.0), "bottom");
    EXPECT_NEAR((shape.length(rect.right())), (20.0), "right");
    EXPECT_NEAR((shape.length(rect.top())), (30.0), "top");
    EXPECT_NEAR((shape.length(rect.left())), (20.0), "left");
    EXPECT_NEAR((shape.position(rect.bottomLeft()).x), (0.0), "bottom-left x");
    EXPECT_NEAR((shape.position(rect.topRight()).x), (30.0), "top-right x");
    EXPECT_NEAR((shape.position(rect.topRight()).y), (20.0), "top-right y");
}

TEST_CASE("rectangle with bad size is an error on that operation")
{
    Document doc;
    PanelId good = doc.add(Panel{.name = "good"});
    PanelId bad = doc.add(Panel{.name = "bad"});
    OpRef<Rect> goodRect = doc.add(good, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Rect> badRect = doc.add(bad, Rect{.width = 0_cm, .height = 10_cm});

    Result result = evaluate(doc);
    EXPECT((result.failed(badRect)), "the bad rectangle is reported");
    EXPECT((!result.failed(goodRect)), "the good one is not");
    EXPECT((result[bad].edges.empty()), "the bad panel has no outline");
    EXPECT_NEAR((result[good].length(goodRect.top())), (10.0), "the good panel is still computed");
}

TEST_CASE("second rectangle in a panel is an error")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> first = doc.add(panel, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Rect> second = doc.add(panel, Rect{.width = 99_cm, .height = 99_cm});

    Result result = evaluate(doc);
    EXPECT((result.failed(second)), "the second rectangle is reported");
    EXPECT_NEAR((result[panel].length(first.top())), (10.0), "the panel keeps the last good result");
}

TEST_CASE("operation using a removed parameter is an error")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = waist, .height = 4_cm});

    doc.remove(waist);

    Result result = evaluate(doc);
    EXPECT((result.failed(rect)), "the rectangle is reported");
}

TEST_CASE("ring sector inner edge has the requested length")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    const double radius = 70.0 / pi;  // half circle: 70 = radius * pi
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((shape.length(ring.inner())), (70.0), "inner edge");
    EXPECT_NEAR((shape.length(ring.outer())), ((radius + 60.0) * pi), "outer edge");
    EXPECT_NEAR((shape.length(ring.startSide())), (60.0), "start side");
    EXPECT_NEAR((shape.length(ring.endSide())), (60.0), "end side");
    EXPECT_NEAR((shape.edge(ring.inner()).arc->radius), (radius), "inner radius");
    // A half circle hangs below a horizontal line through the centre.
    EXPECT_NEAR((shape.position(ring.innerStart()).x), (-radius), "inner start x");
    EXPECT_NEAR((shape.position(ring.innerStart()).y), (0.0), "inner start y");
    EXPECT_NEAR((shape.position(ring.innerEnd()).x), (radius), "inner end x");
    EXPECT_NEAR((shape.position(ring.outerEnd()).x), (radius + 60.0), "outer end x");
}

TEST_CASE("full circle keeps the same four edges")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 360_deg, .depth = 60_cm});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    EXPECT((result.ok()), "no errors");
    EXPECT((shape.edges.size() == 4), "four edges");
    EXPECT_NEAR((shape.length(ring.inner())), (70.0), "inner edge");
    EXPECT_NEAR((shape.edge(ring.inner()).arc->radius), (70.0 / (2.0 * pi)), "inner radius");
    EXPECT_NEAR((shape.position(ring.outerStart()).x), (shape.position(ring.outerEnd()).x), "sides coincide in x");
    EXPECT_NEAR((shape.position(ring.outerStart()).y), (shape.position(ring.outerEnd()).y), "sides coincide in y");
}

TEST_CASE("ring sector with bad sweep is an error")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> tooWide = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 400_deg, .depth = 60_cm});
    EXPECT((evaluate(doc).failed(tooWide)), "more than 360 degrees");

    doc.set(tooWide, &RingSector::sweep, 0_deg);
    EXPECT((evaluate(doc).failed(tooWide)), "zero degrees");

    doc.set(tooWide, &RingSector::sweep, 90_deg);
    EXPECT((evaluate(doc).ok()), "a quarter circle is fine");
}

TEST_CASE("curve bends an edge and keeps its name")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = rect.bottom(), .bulge = 5_cm});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    // A bulge of half the edge length is a semicircle.
    EXPECT_NEAR((result[panel].length(rect.bottom())), (pi * 5.0), "semicircle length");
    EXPECT_NEAR((result[panel].edge(rect.bottom()).arc->radius), (5.0), "semicircle radius");
    EXPECT_NEAR((result[panel].edge(rect.bottom()).arc->center.y), (0.0), "centre on the old edge");
    EXPECT((result[panel].edge(rect.bottom()).arc->sweep > 0.0), "positive bulge goes outward");
    EXPECT_NEAR((result[panel].length(rect.top())), (10.0), "other edges untouched");

    doc.set(curve, &Curve::bulge, 0_cm);
    EXPECT((!evaluate(doc)[panel].edge(rect.bottom()).arc.has_value()), "zero bulge is straight again");
}

TEST_CASE("curve on a missing edge is an error")
{
    Document doc;
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    OpRef<Rect> inA = doc.add(a, Rect{.width = 10_cm, .height = 10_cm});
    doc.add(b, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Curve> wrongPanel = doc.add(b, Curve{.edge = inA.top(), .bulge = 1_cm});

    EXPECT((evaluate(doc).failed(wrongPanel)), "an edge from another panel is not found");
}

TEST_CASE("bezier with handles along the edge is straight")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 20_cm});
    // The top edge runs from the top-right corner to the top-left.
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {-10_cm, 0_cm}, .endHandle = {10_cm, 0_cm}});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((result[panel].edge(rect.top()).cubic.has_value()), "the edge is a free curve");
    EXPECT_NEAR((result[panel].edge(rect.top()).cubic->control1.x), (20.0), "start handle is measured from the start corner");
    EXPECT_NEAR((result[panel].edge(rect.top()).cubic->control2.x), (10.0), "end handle from the end corner");
    EXPECT((std::abs(result[panel].length(rect.top()) - 30.0) < 1e-9), "and here the curve is the straight edge");
}

TEST_CASE("bezier length is measured along the curve")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 10_cm});
    // Make the right edge run diagonally from (10,0) to (0,10), then give it
    // the standard handles that draw a quarter circle of radius 10.
    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = -10_cm, .dy = 0_cm});
    const double k = 0.5522847498 * 10.0;
    doc.add(panel, Bezier{.edge = rect.right(), .startHandle = {0_cm, k}, .endHandle = {k, 0_cm}});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((std::abs(result[panel].length(rect.right()) - 10.0 * pi / 2.0) < 0.003), "a quarter circle of radius 10, to the Bezier's accuracy");
}

TEST_CASE("bezier follows its corners and replaces an arc")
{
    Document doc;
    Param width = doc.add(Parameter{.name = "width", .value = 30_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = width, .height = 20_cm});
    doc.add(panel, Curve{.edge = rect.top(), .bulge = 3_cm});
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {0_cm, 5_cm}, .endHandle = {0_cm, 5_cm}});
    doc.add(panel, AdjustPoint{.corner = rect.topLeft(), .dx = 0_cm, .dy = 4_cm});

    Result result = evaluate(doc);
    const Edge& top = result[panel].edge(rect.top());
    EXPECT((result.ok()), "no errors, including adjusting a corner of the free curve");
    EXPECT((top.cubic.has_value() && !top.arc.has_value()), "the free curve replaced the arc");
    EXPECT_NEAR((top.cubic->control1.x), (30.0), "start handle above the top-right corner");
    EXPECT_NEAR((top.cubic->control1.y), (25.0), "by its offset");
    EXPECT_NEAR((top.cubic->control2.y), (29.0), "the end handle moved up with its adjusted corner");

    doc.set(width, 40_cm);
    EXPECT_NEAR((evaluate(doc)[panel].edge(rect.top()).cubic->control1.x), (40.0), "handles follow when the panel changes");
}

TEST_CASE("operation after a cut that removed its edge is an error")
{
    Document doc;
    Param angle = doc.add(Parameter{.name = "angle", .value = 20_deg});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> line = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = angle});
    doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Left});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = rect.right(), .bulge = 1_cm});

    EXPECT((evaluate(doc).ok()), "at 20 degrees the right edge survives");
    doc.set(angle, 60_deg);
    Result result = evaluate(doc);
    EXPECT((result.failed(curve)), "at 60 degrees the cut removes it, and the curve says so");
    EXPECT((result[panel].edges.size() == 3), "the panel keeps the cut result");
}

TEST_CASE("split is a cut plus its offcut")
{
    Document doc;
    Param position = doc.add(Parameter{.name = "position", .value = 0.25});
    PanelId kept = doc.add(Panel{.name = "kept"});
    OpRef<Rect> rect = doc.add(kept, Rect{.width = 40_cm, .height = 20_cm});
    // A level line, `position` of the way down the left edge. It runs left to
    // right, so its right-hand side is the part below it.
    OpRef<LineBetween> line = doc.add(kept, LineBetween{.from = {rect.left(), position}, .to = {rect.right(), 1 - Expr(position)}});
    OpRef<Cut> cut = doc.add(kept, Cut{.along = line.line(), .keep = Cut::Side::Right});
    doc.add(kept, Curve{.edge = rect.left(), .bulge = 1_cm});  // after the cut
    PanelId removed = doc.add(Panel{.name = "removed"});
    OpRef<Offcut> offcut = doc.add(removed, Offcut{.of = cut});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((result[removed].length(rect.left())), (5.0), "the offcut is the quarter above the line");
    EXPECT_NEAR((result[kept].length(rect.right()) + result[removed].length(rect.right())), (20.0), "right edge: the pieces add up");
    EXPECT_NEAR((result[removed].length(cut.edge())), (result[kept].length(cut.edge())), "both pieces have the cut edge, equally long");
    EXPECT((result[removed].contains(rect.top()) && !result[kept].contains(rect.top())), "the top edge went to the offcut");
    EXPECT((result[kept].contains(rect.bottom()) && !result[removed].contains(rect.bottom())), "the bottom edge stayed");
    EXPECT((result[kept].edge(rect.left()).arc.has_value()), "the kept piece was curved after its cut");
    EXPECT((!result[removed].edge(rect.left()).arc.has_value()), "which does not reach the offcut");

    doc.set(position, 0.5);
    result = evaluate(doc);
    EXPECT_NEAR((result[removed].length(rect.left())), (10.0), "moving the line changes the offcut");
    EXPECT_NEAR((result[kept].length(rect.right())), (10.0), "and the kept piece, together");

    doc.set(cut, &Cut::keep, Cut::Side::Left);
    result = evaluate(doc);
    EXPECT((result[kept].contains(rect.top()) && result[removed].contains(rect.bottom())), "flipping the cut swaps the pieces");

    doc.remove(OpId(cut));
    result = evaluate(doc);
    EXPECT((result.failed(offcut)), "without its cut the offcut is an error");
    EXPECT((result[removed].edges.empty()), "and has no outline");
}

TEST_CASE("offcuts of each other are an error, not a hang")
{
    Document doc;
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    OpRef<Rect> rect = doc.add(a, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<LineBetween> lineA = doc.add(a, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 0.5}});
    OpRef<Cut> cutA = doc.add(a, Cut{.along = lineA.line()});
    OpRef<Offcut> offcutB = doc.add(b, Offcut{.of = cutA});
    OpRef<LineBetween> lineB = doc.add(b, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.top(), 0.5}});
    OpRef<Cut> cutB = doc.add(b, Cut{.along = lineB.line()});
    // Now make a's first operation an offcut of b's cut: each needs the other first.
    doc.remove(OpId(rect));
    OpRef<Offcut> offcutA = doc.add(a, Offcut{.of = cutB});
    doc.move(OpId(offcutA), 0);

    Result result = evaluate(doc);
    EXPECT((result.failed(offcutA) || result.failed(offcutB)), "reported");
}

TEST_CASE("mirror applied last keeps later edits symmetric")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});
    // Added after the mirror in the list, but the mirror waits for them.
    doc.add(panel, AdjustPoint{.corner = half.bottomRight(), .dx = -3_cm, .dy = 0_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = half.top(), .bulge = 2_cm});

    Result result = evaluate(doc);
    const Pattern* shape = &result[panel];
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((shape->position(half.bottomRight()).x), (7.0), "the drawn corner moved");
    EXPECT_NEAR((shape->position(mirror.image(half.bottomRight())).x), (-7.0), "and so did its mirror image");
    EXPECT((shape->edge(half.top()).arc.has_value() && shape->edge(mirror.image(half.top())).arc.has_value()), "the later curve is on both halves");
    EXPECT((!outlineBefore(doc, curve).contains(mirror.image(half.top()))), "to the curve, the mirror has not happened yet");

    // The old behaviour is still available: the mirror runs in its place.
    doc.set(mirror, &Mirror::appliedLast, false);
    result = evaluate(doc);
    shape = &result[panel];
    EXPECT_NEAR((shape->position(half.bottomRight()).x), (7.0), "in place: the drawn corner moved");
    EXPECT_NEAR((shape->position(mirror.image(half.bottomRight())).x), (-10.0), "in place: its image did not");
    EXPECT((!shape->edge(mirror.image(half.top())).arc.has_value()), "in place: the curve is on one half only");
}

TEST_CASE("operations can be switched off and stepped through")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = half.top(), .bulge = 2_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});
    OpRef<AdjustPoint> adjust = doc.add(panel, AdjustPoint{.corner = half.bottomRight(), .dx = -3_cm, .dy = 0_cm});

    EXPECT((doc.enabled(curve)), "operations start switched on");
    doc.setEnabled(curve, false);
    Result result = evaluate(doc);
    EXPECT((result.ok() && !result[panel].edge(half.top()).arc.has_value()), "switched off: the curve is skipped");
    EXPECT_NEAR((result[panel].position(half.bottomRight()).x), (7.0), "the others still run");
    doc.setEnabled(curve, true);
    EXPECT((evaluate(doc)[panel].edge(half.top()).arc.has_value()), "switched back on");

    // Stepping back: the panel as it was after each operation, mirror included.
    const Result afterRect = evaluateUpTo(doc, half);
    EXPECT((!afterRect[panel].edge(half.top()).arc.has_value()), "after step 1: not yet curved");
    EXPECT((afterRect[panel].contains(mirror.image(half.top()))), "but already shown mirrored, since the mirror is applied last");
    EXPECT_NEAR((afterRect[panel].position(half.bottomRight()).x), (10.0), "after step 1: corner not yet moved");
    const Result afterCurve = evaluateUpTo(doc, curve);
    EXPECT((afterCurve[panel].edge(half.top()).arc.has_value()), "after step 2: curved");
    EXPECT_NEAR((afterCurve[panel].position(half.bottomRight()).x), (10.0), "after step 2: corner still not moved");
    EXPECT_NEAR((evaluateUpTo(doc, adjust)[panel].position(half.bottomRight()).x), (7.0), "after the last step: everything");
    EXPECT((doc.enabled(adjust)), "stepping back changes nothing in the document");

    PanelCopy copy = doc.copy(panel, Panel{.name = "copy"});
    doc.setEnabled(copy[curve], false);
    EXPECT((doc.enabled(curve) && !doc.enabled(copy[curve])), "each operation has its own switch");
}

TEST_CASE("circle and its slices")
{
    Document doc;
    Param sweep = doc.add(Parameter{.name = "sweep", .value = 360_deg});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Circle> circle = doc.add(panel, Circle{.radius = 10_cm, .sweep = sweep});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((result[panel].edges.size() == 1 && result[panel].vertices.size() == 1), "a whole circle is one edge with one corner");
    EXPECT_NEAR((result[panel].length(circle.arc())), (20.0 * pi), "its length is the circumference");
    EXPECT((encloses(result[panel], Vec2{0, 0}) && encloses(result[panel], Vec2{7, 7}) && !encloses(result[panel], Vec2{8, 8})), "inside and outside");
    EXPECT((std::abs(bounds(result[panel])->max.x - bounds(result[panel])->min.x - 20.0) < 0.02), "as wide as its diameter, within drawing tolerance");

    doc.set(sweep, 90_deg);
    result = evaluate(doc);
    EXPECT((result.ok() && result[panel].edges.size() == 3), "a slice has the arc and two sides");
    EXPECT_NEAR((result[panel].length(circle.arc())), (5.0 * pi), "a quarter of the circumference");
    EXPECT_NEAR((result[panel].length(circle.startSide())), (10.0), "sides as long as the radius");
    EXPECT_NEAR((result[panel].position(circle.centre()).y), (0.0), "meeting at the centre");

    Document plain;
    PanelId p = plain.add(Panel{.name = "p"});
    OpRef<Circle> whole = plain.add(p, Circle{.radius = 5_cm});
    EXPECT_NEAR((evaluate(plain)[p].length(whole.arc())), (10.0 * pi), "sweep defaults to a whole circle");
}

TEST_CASE("instance follows its source")
{
    Document doc;
    Param width = doc.add(Parameter{.name = "width", .value = 10_cm});
    PanelId instancePanel = doc.add(Panel{.name = "instance"});  // listed before its source
    PanelId source = doc.add(Panel{.name = "source", .construction = true});
    OpRef<Rect> rect = doc.add(source, Rect{.width = width, .height = 20_cm});
    doc.add(source, Curve{.edge = rect.top(), .bulge = 2_cm});
    doc.add(instancePanel, Instance{.source = source, .mirrored = true});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((doc.get(source).construction), "source is a construction panel");
    EXPECT((result[instancePanel].flipped && !result[source].flipped), "only the instance is flipped");
    EXPECT_NEAR((result[instancePanel].position(rect.bottomRight()).x), (-10.0), "flipped corner");
    EXPECT_NEAR((result[instancePanel].length(rect.top())), (result[source].length(rect.top())), "curved edge has the same length");
    EXPECT_NEAR((result[instancePanel].edge(rect.top()).arc->sweep), (result[source].edge(rect.top()).arc->sweep), "and bulges the same way");

    doc.set(width, 16_cm);
    EXPECT_NEAR((evaluate(doc)[instancePanel].length(rect.bottom())), (16.0), "follows the source");
}

TEST_CASE("instance loops and missing sources are errors")
{
    Document doc;
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    PanelId gone = doc.add(Panel{.name = "gone"});
    OpRef<Instance> aOfB = doc.add(a, Instance{.source = b});
    OpRef<Instance> bOfA = doc.add(b, Instance{.source = a});
    PanelId c = doc.add(Panel{.name = "c"});
    OpRef<Instance> cOfGone = doc.add(c, Instance{.source = gone});
    doc.remove(gone);

    Result result = evaluate(doc);
    EXPECT((result.failed(aOfB) || result.failed(bOfA)), "the loop is reported");
    EXPECT((result.failed(cOfGone)), "the missing source is reported");
}

TEST_CASE("adjust point moves one corner")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 40_cm});
    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = -30_cm, .dy = 0_cm});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((result[panel].length(rect.top())), (0.0), "top edge collapses");
    EXPECT_NEAR((result[panel].length(rect.right())), (50.0), "right edge becomes the 30-40-50 diagonal");
}

TEST_CASE("corners of arcs can move")
{
    Document doc;
    PanelId slice = doc.add(Panel{.name = "slice"});
    OpRef<Circle> circle = doc.add(slice, Circle{.radius = 10_cm, .sweep = 90_deg});
    const double before = evaluate(doc)[slice].length(circle.arc());
    doc.add(slice, AdjustPoint{.corner = circle.start(), .dx = 1_cm, .dy = 0_cm});
    Result moved = evaluate(doc);
    EXPECT((moved.ok()), "moving a corner of a slice's arc is fine");
    EXPECT((std::abs(moved[slice].length(circle.arc()) - before) > 1e-6), "the arc follows its corner");
    EXPECT((std::abs(moved[slice].edge(circle.arc()).arc->sweep - 90.0) < 1e-6), "and keeps turning through the same angle");

    Document whole;
    PanelId disc = whole.add(Panel{.name = "disc"});
    OpRef<Circle> full = whole.add(disc, Circle{.radius = 10_cm});
    whole.add(disc, AdjustPoint{.corner = full.start(), .dx = 1_cm, .dy = 0_cm});
    EXPECT((!evaluate(whole).ok()), "a whole circle's only point cannot be moved");

    PanelId ringPanel = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> ring = doc.add(ringPanel, RingSector{.innerLength = 40_cm, .sweep = 120_deg, .depth = 20_cm});
    doc.add(ringPanel, AdjustPoint{.corner = ring.outerStart(), .dx = 2_cm, .dy = 1_cm});
    EXPECT((evaluate(doc).ok()), "a ring's corners move too");
}

TEST_CASE("baking replaces formulas with numbers")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 80});
    PanelId band = doc.add(Panel{.name = "band"});
    OpRef<Rect> rect = doc.add(band, Rect{.width = waist / 2, .height = 10_cm});
    EXPECT((bake(doc, band) == 1), "one formula was replaced");
    doc.set(waist, Expr(100));
    Result r = evaluate(doc);
    EXPECT((r.ok()), "no errors");
    EXPECT_NEAR((r[band].length(rect.bottom())), (40.0), "the width no longer follows the parameter");
    EXPECT((bake(doc, band) == 0), "baking again changes nothing");
}

TEST_CASE("a full ring closes on itself")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 360_deg, .depth = 60_cm});
    Pattern shape = evaluate(doc)[panel];
    EXPECT((shape.edge(ring.startSide()).seam && shape.edge(ring.endSide()).seam), "its two sides are a seam");
    EXPECT((!shape.edge(ring.inner()).seam && !shape.edge(ring.outer()).seam), "its circles are real edges");
    Document half;
    PanelId h = half.add(Panel{.name = "half"});
    OpRef<RingSector> open = half.add(h, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
    EXPECT((!evaluate(half)[h].edge(open.startSide()).seam), "a partial ring's sides are edges");
}

TEST_CASE("seams follow their edges")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 80});
    PanelId band = doc.add(Panel{.name = "band"});
    OpRef<Rect> bandRect = doc.add(band, Rect{.width = waist, .height = 4_cm});
    PanelId skirt = doc.add(Panel{.name = "skirt"});
    OpRef<Rect> skirtRect = doc.add(skirt, Rect{.width = waist * 1.5, .height = 60_cm});
    SeamId seam = doc.add(Seam{.a = {band, bandRect.bottom()}, .b = {skirt, skirtRect.top()}});

    Result r = evaluate(doc);
    EXPECT((r.ok() && r.seams.size() == 1 && r[seam].sewn()), "the seam is sewn");
    EXPECT_NEAR((r[seam].lengthA), (80.0), "one side is the band");
    EXPECT_NEAR((r[seam].lengthB), (120.0), "the other is gathered onto it");

    doc.set(waist, Expr(90));
    EXPECT_NEAR((evaluate(doc)[seam].lengthA), (90.0), "it follows the parameter");

    // Cutting the sewn edge away breaks the seam but is no error.
    OpRef<Line> line = doc.add(skirt, Line{.from = skirtRect.topLeft(), .angle = -45_deg});
    OpRef<Cut> cut = doc.add(skirt, Cut{.along = line, .keep = Cut::Side::Right});
    Result afterCut = evaluate(doc);
    EXPECT((afterCut.ok()), "cutting is not an error");
    const bool topRemains = afterCut[skirt].contains(skirtRect.top());
    EXPECT((afterCut[seam].sewn() == topRemains), "the seam is broken exactly when its edge is gone");
    doc.remove(OpId(cut));
    EXPECT((evaluate(doc)[seam].sewn()), "and comes back with the edge");

    doc.remove(skirt);
    EXPECT((doc.seams().empty()), "removing a panel removes its seams");
}

TEST_CASE("modifying operation cannot come first")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = EdgeRef{}});

    EXPECT((evaluate(doc).failed(mirror)), "reported");
}

TEST_CASE("outline before an operation is what it works on")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> line = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 45_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Left});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = rect.left(), .bulge = 1_cm});

    EXPECT((outlineBefore(doc, rect).edges.empty()), "nothing exists before the first operation");
    EXPECT((outlineBefore(doc, cut).edges.size() == 4), "before the cut: the whole rectangle");
    EXPECT((outlineBefore(doc, cut).contains(line.line())), "and the line to cut along");
    EXPECT((outlineBefore(doc, curve).edges.size() == 3), "before the curve: the cut triangle");
    EXPECT((!outlineBefore(doc, curve).contains(rect.right())), "without the edge the cut removed");
    EXPECT((!outlineBefore(doc, curve).edge(rect.left()).arc.has_value()), "and not yet curved");
    EXPECT((evaluate(doc)[panel].edge(rect.left()).arc.has_value()), "the document itself is untouched");
    EXPECT((outlineBefore(doc, OpId{9999}).edges.empty()), "an unknown operation gives nothing");
}
