// Tests for the core: each one builds a small Document through the public
// API, evaluates it, and checks the result.

#include <cmath>
#include <stdexcept>

#include "core/Document.h"
#include "core/Drawing.h"
#include "core/Evaluate.h"
#include "core/File.h"
#include "core/Text.h"
#include "tests/Check.h"

using namespace clothcad;

// ---- Expressions and parameters ---------------------------------------------

static void formulasAreComputed(Check& check) {
    Document doc;
    Param a = doc.add(Parameter{.name = "a", .value = 10});
    Param b = doc.add(Parameter{.name = "b", .value = a / 4 + 1});
    Param c = doc.add(Parameter{.name = "c", .value = -a * 2 - b});
    Param d = doc.add(Parameter{.name = "d", .value = min(a, b) + max(a, b) + abs(c)});
    Param e = doc.add(Parameter{.name = "e", .value = clamp(a, 0, 5) + clamp(a, 20, 30)});

    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.equal(result[b], 3.5, "a / 4 + 1");
    check.equal(result[c], -23.5, "-a * 2 - b");
    check.equal(result[d], 37.0, "min + max + abs");
    check.equal(result[e], 25.0, "clamp");
}

static void unitLiteralsAreCentimetres(Check& check) {
    check.equal(70_cm, 70.0, "cm");
    check.equal(5_mm, 0.5, "mm");
    check.equal(2.5_cm + 5_mm, 3.0, "mixed");
}

static void parameterMayBeDefinedBeforeTheOneItUses(Check& check) {
    Document doc;
    Param hip = doc.add(Parameter{.name = "hip", .value = 0});
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    doc.set(hip, waist + 20_cm);

    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.equal(result[hip], 90.0, "hip follows waist");
}

static void circularParametersAreErrors(Check& check) {
    Document doc;
    Param a = doc.add(Parameter{.name = "a", .value = 1});
    Param b = doc.add(Parameter{.name = "b", .value = a + 1});
    Param fine = doc.add(Parameter{.name = "fine", .value = 5});
    doc.set(a, b + 1);

    Result result = evaluate(doc);
    check.that(result.failed(a), "a is reported");
    check.that(result.failed(b), "b is reported");
    check.that(!result.failed(fine), "an unrelated parameter is not");
    check.equal(result[fine], 5.0, "an unrelated parameter still has a value");
    check.throws<std::out_of_range>([&] { (void)result[a]; }, "a has no value");
}

static void divisionByZeroIsAnError(Check& check) {
    Document doc;
    Param zero = doc.add(Parameter{.name = "zero", .value = 0});
    Param bad = doc.add(Parameter{.name = "bad", .value = 1 / Expr(zero)});

    Result result = evaluate(doc);
    check.that(result.failed(bad), "reported on the dividing parameter");
    check.that(!result.failed(zero), "not on the divisor");
}

// ---- Rectangles ---------------------------------------------------------------

static void rectangleHasNamedEdgesAndCorners(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 20_cm});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    check.that(result.ok(), "no errors");
    check.equal(shape.length(rect.bottom()), 30.0, "bottom");
    check.equal(shape.length(rect.right()), 20.0, "right");
    check.equal(shape.length(rect.top()), 30.0, "top");
    check.equal(shape.length(rect.left()), 20.0, "left");
    check.equal(shape.position(rect.bottomLeft()).x, 0.0, "bottom-left x");
    check.equal(shape.position(rect.topRight()).x, 30.0, "top-right x");
    check.equal(shape.position(rect.topRight()).y, 20.0, "top-right y");
}

static void rectangleWithBadSizeIsAnErrorOnThatOperation(Check& check) {
    Document doc;
    PanelId good = doc.add(Panel{.name = "good"});
    PanelId bad = doc.add(Panel{.name = "bad"});
    OpRef<Rect> goodRect = doc.add(good, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Rect> badRect = doc.add(bad, Rect{.width = 0_cm, .height = 10_cm});

    Result result = evaluate(doc);
    check.that(result.failed(badRect), "the bad rectangle is reported");
    check.that(!result.failed(goodRect), "the good one is not");
    check.that(result[bad].edges.empty(), "the bad panel has no outline");
    check.equal(result[good].length(goodRect.top()), 10.0, "the good panel is still computed");
}

static void secondRectangleInAPanelIsAnError(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> first = doc.add(panel, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Rect> second = doc.add(panel, Rect{.width = 99_cm, .height = 99_cm});

    Result result = evaluate(doc);
    check.that(result.failed(second), "the second rectangle is reported");
    check.equal(result[panel].length(first.top()), 10.0, "the panel keeps the last good result");
}

static void operationUsingARemovedParameterIsAnError(Check& check) {
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = waist, .height = 4_cm});

    doc.remove(waist);

    Result result = evaluate(doc);
    check.that(result.failed(rect), "the rectangle is reported");
}

// ---- Ring sectors -------------------------------------------------------------

static void ringSectorInnerEdgeHasTheRequestedLength(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    const double radius = 70.0 / pi;  // half circle: 70 = radius * pi
    check.that(result.ok(), "no errors");
    check.equal(shape.length(ring.inner()), 70.0, "inner edge");
    check.equal(shape.length(ring.outer()), (radius + 60.0) * pi, "outer edge");
    check.equal(shape.length(ring.startSide()), 60.0, "start side");
    check.equal(shape.length(ring.endSide()), 60.0, "end side");
    check.equal(shape.edge(ring.inner()).arc->radius, radius, "inner radius");
    // A half circle hangs below a horizontal line through the centre.
    check.equal(shape.position(ring.innerStart()).x, -radius, "inner start x");
    check.equal(shape.position(ring.innerStart()).y, 0.0, "inner start y");
    check.equal(shape.position(ring.innerEnd()).x, radius, "inner end x");
    check.equal(shape.position(ring.outerEnd()).x, radius + 60.0, "outer end x");
}

static void fullCircleKeepsTheSameFourEdges(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 360_deg, .depth = 60_cm});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    check.that(result.ok(), "no errors");
    check.that(shape.edges.size() == 4, "four edges");
    check.equal(shape.length(ring.inner()), 70.0, "inner edge");
    check.equal(shape.edge(ring.inner()).arc->radius, 70.0 / (2.0 * pi), "inner radius");
    check.equal(shape.position(ring.outerStart()).x, shape.position(ring.outerEnd()).x, "sides coincide in x");
    check.equal(shape.position(ring.outerStart()).y, shape.position(ring.outerEnd()).y, "sides coincide in y");
}

static void ringSectorWithBadSweepIsAnError(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> tooWide = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 400_deg, .depth = 60_cm});
    check.that(evaluate(doc).failed(tooWide), "more than 360 degrees");

    doc.set(tooWide, &RingSector::sweep, 0_deg);
    check.that(evaluate(doc).failed(tooWide), "zero degrees");

    doc.set(tooWide, &RingSector::sweep, 90_deg);
    check.that(evaluate(doc).ok(), "a quarter circle is fine");
}

// ---- Curves ---------------------------------------------------------------------

static void curveBendsAnEdgeAndKeepsItsName(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = rect.bottom(), .bulge = 5_cm});

    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    // A bulge of half the edge length is a semicircle.
    check.equal(result[panel].length(rect.bottom()), pi * 5.0, "semicircle length");
    check.equal(result[panel].edge(rect.bottom()).arc->radius, 5.0, "semicircle radius");
    check.equal(result[panel].edge(rect.bottom()).arc->center.y, 0.0, "centre on the old edge");
    check.that(result[panel].edge(rect.bottom()).arc->sweep > 0.0, "positive bulge goes outward");
    check.equal(result[panel].length(rect.top()), 10.0, "other edges untouched");

    doc.set(curve, &Curve::bulge, 0_cm);
    check.that(!evaluate(doc)[panel].edge(rect.bottom()).arc.has_value(), "zero bulge is straight again");
}

static void curveOnAMissingEdgeIsAnError(Check& check) {
    Document doc;
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    OpRef<Rect> inA = doc.add(a, Rect{.width = 10_cm, .height = 10_cm});
    doc.add(b, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Curve> wrongPanel = doc.add(b, Curve{.edge = inA.top(), .bulge = 1_cm});

    check.that(evaluate(doc).failed(wrongPanel), "an edge from another panel is not found");
}

static void copyPointsReferencesAtTheCopy(Check& check) {
    Document doc;
    PanelId original = doc.add(Panel{.name = "original"});
    OpRef<Rect> rect = doc.add(original, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Curve> curve = doc.add(original, Curve{.edge = rect.top(), .bulge = 2_cm});

    PanelCopy copy = doc.copy(original, Panel{.name = "copy"});
    OpRef<Rect> copyRect = copy[rect];
    OpRef<Curve> copyCurve = copy[curve];

    check.that(doc.get(copyCurve).edge == copyRect.top(), "the copied curve bends the copied rectangle");
    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.that(result[copy].edge(copyRect.top()).arc.has_value(), "the copy is curved");
}

// ---- Free curves ------------------------------------------------------------------

static void bezierWithHandlesAlongTheEdgeIsStraight(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 20_cm});
    // The top edge runs from the top-right corner to the top-left.
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {-10_cm, 0_cm}, .endHandle = {10_cm, 0_cm}});

    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.that(result[panel].edge(rect.top()).cubic.has_value(), "the edge is a free curve");
    check.equal(result[panel].edge(rect.top()).cubic->control1.x, 20.0, "start handle is measured from the start corner");
    check.equal(result[panel].edge(rect.top()).cubic->control2.x, 10.0, "end handle from the end corner");
    check.that(std::abs(result[panel].length(rect.top()) - 30.0) < 1e-9, "and here the curve is the straight edge");
}

static void bezierLengthIsMeasuredAlongTheCurve(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 10_cm});
    // Make the right edge run diagonally from (10,0) to (0,10), then give it
    // the standard handles that draw a quarter circle of radius 10.
    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = -10_cm, .dy = 0_cm});
    const double k = 0.5522847498 * 10.0;
    doc.add(panel, Bezier{.edge = rect.right(), .startHandle = {0_cm, k}, .endHandle = {k, 0_cm}});

    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.that(std::abs(result[panel].length(rect.right()) - 10.0 * pi / 2.0) < 0.003, "a quarter circle of radius 10, to the Bezier's accuracy");
}

static void bezierFollowsItsCornersAndReplacesAnArc(Check& check) {
    Document doc;
    Param width = doc.add(Parameter{.name = "width", .value = 30_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = width, .height = 20_cm});
    doc.add(panel, Curve{.edge = rect.top(), .bulge = 3_cm});
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {0_cm, 5_cm}, .endHandle = {0_cm, 5_cm}});
    doc.add(panel, AdjustPoint{.corner = rect.topLeft(), .dx = 0_cm, .dy = 4_cm});

    Result result = evaluate(doc);
    const Edge& top = result[panel].edge(rect.top());
    check.that(result.ok(), "no errors, including adjusting a corner of the free curve");
    check.that(top.cubic.has_value() && !top.arc.has_value(), "the free curve replaced the arc");
    check.equal(top.cubic->control1.x, 30.0, "start handle above the top-right corner");
    check.equal(top.cubic->control1.y, 25.0, "by its offset");
    check.equal(top.cubic->control2.y, 29.0, "the end handle moved up with its adjusted corner");

    doc.set(width, 40_cm);
    check.equal(evaluate(doc)[panel].edge(rect.top()).cubic->control1.x, 40.0, "handles follow when the panel changes");
}

static void cutThroughAFreeCurveSplitsItExactly(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {0_cm, 5_cm}, .endHandle = {0_cm, 5_cm}});  // a hump
    OpRef<Line> diagonal = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 45_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = diagonal.line(), .keep = Cut::Side::Left});

    const double whole = [&] {
        Document uncut = doc;
        uncut.remove(OpId(cut));
        return evaluate(uncut)[panel].length(rect.top());
    }();

    Result left = evaluate(doc);
    check.that(left.ok(), "no errors");
    const Vec2 meeting = left[panel].position(cut.end());
    check.that(std::abs(meeting.x - meeting.y) < 1e-6, "the cut ends on the 45 degree line");
    check.that(meeting.y > 10.0, "above the old straight edge, on the hump");
    check.that(left[panel].edge(rect.top()).cubic.has_value(), "what is left of the top is still a free curve");

    doc.set(cut, &Cut::keep, Cut::Side::Right);
    Result right = evaluate(doc);
    check.that(std::abs(left[panel].length(rect.top()) + right[panel].length(rect.top()) - whole) < 1e-6,
               "the two pieces add up to the whole curve");
}

static void sleeveCapIsSmoothAcrossTheMirror(Check& check) {
    Document doc;
    PanelId left = doc.add(Panel{.name = "left sleeve"});
    OpRef<Rect> half = doc.add(left, Rect{.width = 15_cm, .height = 60_cm});
    doc.add(left, AdjustPoint{.corner = half.topLeft(), .dx = 0_cm, .dy = 5_cm});  // the crown, on the centre line
    doc.add(left, Bezier{.edge = half.top(), .startHandle = {-7.5_cm, 0_cm}, .endHandle = {7.5_cm, 0_cm}});
    OpRef<Mirror> fold = doc.add(left, Mirror{.across = half.left()});
    PanelId right = doc.add(Panel{.name = "right sleeve"});
    doc.add(right, Instance{.source = left, .mirrored = true});

    Result result = evaluate(doc);
    const Pattern& sleeve = result[left];
    const Edge& drawn = sleeve.edge(half.top());
    const Edge& image = sleeve.edge(fold.image(half.top()));
    check.that(result.ok(), "no errors");
    check.equal(sleeve.position(half.topLeft()).y, 65.0, "the crown is raised");
    // Smooth at the crown: both halves arrive level.
    check.equal(drawn.cubic->control2.y, 65.0, "drawn half arrives at the crown level");
    check.equal(image.cubic->control1.y, 65.0, "mirrored half leaves the crown level");
    check.equal(image.cubic->control1.x, -drawn.cubic->control2.x, "and its handle is the mirror image");
    check.that(std::abs(sleeve.length(half.top()) - sleeve.length(fold.image(half.top()))) < 1e-9, "both halves of the cap are equally long");
    check.that(sleeve.length(half.top()) > std::hypot(15.0, 5.0), "the cap is longer than a straight line");
    check.that(std::abs(result[right].length(half.top()) - sleeve.length(half.top())) < 1e-9, "the other arm has the same cap");
    check.equal(result[right].edge(half.top()).cubic->control1.x, -drawn.cubic->control2.x, "flipped");
}

// ---- Lines and cuts ---------------------------------------------------------------

static void cutKeepsTheChosenSide(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> diagonal = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 45_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = diagonal.line(), .keep = Cut::Side::Left});

    // The line leaves through the top edge at x = 30.
    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.equal(result[panel].line(diagonal.line()).end.x, 30.0, "line end x");
    check.equal(result[panel].line(diagonal.line()).end.y, 30.0, "line end y");
    check.that(result[panel].edges.size() == 3, "left: a triangle");
    check.equal(result[panel].length(rect.top()), 30.0, "left: top is shortened and keeps its name");
    check.equal(result[panel].length(rect.left()), 30.0, "left: left edge is whole");
    check.equal(result[panel].length(cut.edge()), std::sqrt(1800.0), "left: the new edge");
    check.that(!result[panel].contains(rect.right()), "left: right edge is gone");
    check.that(result[panel].contains(cut.end()), "left: the new corner is named by the cut");
    check.that(!result[panel].contains(cut.start()), "left: the start corner keeps its own name");
    check.that(result[panel].contains(rect.bottomLeft()), "left: the start corner is still bottom-left");

    doc.set(cut, &Cut::keep, Cut::Side::Right);
    result = evaluate(doc);
    check.that(result[panel].edges.size() == 4, "right: four edges");
    check.equal(result[panel].length(rect.top()), 10.0, "right: the other part of the top");
    check.equal(result[panel].length(rect.right()), 30.0, "right: right edge is whole");
    check.equal(result[panel].length(rect.bottom()), 40.0, "right: bottom edge is whole");
    check.that(!result[panel].contains(rect.left()), "right: left edge is gone");
}

static void cutThroughACurvedEdgeShortensTheArc(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
    // A half ring hanging down. The slant leaves the left end of the outer
    // arc at 45 degrees downward and meets the outer arc again at its lowest
    // point, so it slices off the outer arc's left quarter.
    OpRef<Line> slant = doc.add(panel, Line{.from = ring.outerStart(), .angle = -45_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = slant.line(), .keep = Cut::Side::Left});

    Result result = evaluate(doc);
    const double outerRadius = 70.0 / pi + 60.0;
    check.that(result.ok(), "no errors");
    check.that(result[panel].edges.size() == 5, "the four edges plus the cut");
    check.equal(result[panel].length(ring.outer()), outerRadius * pi / 2.0, "half of the outer arc is left");
    check.equal(result[panel].edge(ring.outer()).arc->radius, outerRadius, "with the same radius");
    check.equal(result[panel].length(cut.edge()), outerRadius * std::sqrt(2.0), "the cut is the chord");
    check.equal(result[panel].length(ring.inner()), 70.0, "inner arc untouched");
}

static void lineThatMissesThePanelIsAnError(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> outward = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 225_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = outward.line()});

    Result result = evaluate(doc);
    check.that(result.failed(outward), "the line is reported");
    check.that(result.failed(cut), "the cut has no line to follow");
    check.that(result[panel].edges.size() == 4, "the rectangle is untouched");
}

static void operationAfterACutThatRemovedItsEdgeIsAnError(Check& check) {
    Document doc;
    Param angle = doc.add(Parameter{.name = "angle", .value = 20_deg});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> line = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = angle});
    doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Left});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = rect.right(), .bulge = 1_cm});

    check.that(evaluate(doc).ok(), "at 20 degrees the right edge survives");
    doc.set(angle, 60_deg);
    Result result = evaluate(doc);
    check.that(result.failed(curve), "at 60 degrees the cut removes it, and the curve says so");
    check.that(result[panel].edges.size() == 3, "the panel keeps the cut result");
}

static void lineBetweenTwoPointsOnTheOutline(Check& check) {
    Document doc;
    Param height = doc.add(Parameter{.name = "height", .value = 30_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = height});
    // The left edge runs top to bottom, the right edge bottom to top.
    OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 1.0}});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Right});

    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.equal(result[panel].line(line.line()).start.y, 15.0, "halfway down the left edge");
    check.equal(result[panel].line(line.line()).end.y, 30.0, "to the top of the right edge");
    check.equal(result[panel].length(rect.left()), 15.0, "keeping the lower part: the left edge is halved");
    check.equal(result[panel].length(rect.right()), 30.0, "the right edge is whole");
    check.that(!result[panel].contains(rect.top()), "the top edge is gone");
    check.equal(result[panel].length(cut.edge()), std::hypot(40.0, 15.0), "the new edge");

    doc.set(height, 50_cm);
    check.equal(evaluate(doc)[panel].line(line.line()).start.y, 25.0, "the line stays halfway when the panel grows");

    doc.set(line, &LineBetween::to, EdgePoint{rect.right(), 2.0});
    check.that(evaluate(doc).failed(line), "a position past the end of the edge is outside the field's limits");
    doc.set(line, &LineBetween::to, EdgePoint{rect.left(), 0.5});
    check.that(evaluate(doc).failed(line), "a line from a point to itself is an error");
}

static void splitIsACutPlusItsOffcut(Check& check) {
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
    check.that(result.ok(), "no errors");
    check.equal(result[removed].length(rect.left()), 5.0, "the offcut is the quarter above the line");
    check.equal(result[kept].length(rect.right()) + result[removed].length(rect.right()), 20.0, "right edge: the pieces add up");
    check.equal(result[removed].length(cut.edge()), result[kept].length(cut.edge()), "both pieces have the cut edge, equally long");
    check.that(result[removed].contains(rect.top()) && !result[kept].contains(rect.top()), "the top edge went to the offcut");
    check.that(result[kept].contains(rect.bottom()) && !result[removed].contains(rect.bottom()), "the bottom edge stayed");
    check.that(result[kept].edge(rect.left()).arc.has_value(), "the kept piece was curved after its cut");
    check.that(!result[removed].edge(rect.left()).arc.has_value(), "which does not reach the offcut");

    doc.set(position, 0.5);
    result = evaluate(doc);
    check.equal(result[removed].length(rect.left()), 10.0, "moving the line changes the offcut");
    check.equal(result[kept].length(rect.right()), 10.0, "and the kept piece, together");

    doc.set(cut, &Cut::keep, Cut::Side::Left);
    result = evaluate(doc);
    check.that(result[kept].contains(rect.top()) && result[removed].contains(rect.bottom()), "flipping the cut swaps the pieces");

    doc.remove(OpId(cut));
    result = evaluate(doc);
    check.that(result.failed(offcut), "without its cut the offcut is an error");
    check.that(result[removed].edges.empty(), "and has no outline");
}

static void offcutsOfEachOtherAreAnErrorNotAHang(Check& check) {
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
    check.that(result.failed(offcutA) || result.failed(offcutB), "reported");
}

static void panelsHaveAPlaceAndClicksSnapToTheOutline(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel", .x = 5.0, .y = 7.0});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 20_cm});
    check.equal(doc.get(panel).x, 5.0, "a panel can be created at a place");
    doc.place(panel, 100.0, -20.0);
    check.equal(doc.get(panel).y, -20.0, "and moved");
    Result result = evaluate(doc);
    check.equal(result[panel].position(rect.bottomLeft()).x, 0.0, "placing does not change the panel's own shape");

    const std::optional<OutlinePoint> hit = nearestOnOutline(result[panel], Vec2{10.0, 22.0});
    check.that(hit.has_value() && hit->edge == rect.top(), "a point above the top edge snaps to the top edge");
    check.equal(hit->along, 0.75, "three quarters along it, counting from its start at the right");
    check.equal(hit->position.y, 20.0, "on the edge");
    check.that(!nearestOnOutline(Pattern{}, Vec2{}).has_value(), "nothing to snap to on an empty outline");
}

// ---- Mirror and instances -----------------------------------------------------------

static void mirrorJoinsTwoHalves(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});

    Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    check.that(result.ok(), "no errors");
    check.that(shape.edges.size() == 6, "three edges from each half");
    check.that(shape.vertices.size() == 6, "two shared corners on the axis");
    check.that(!shape.contains(half.left()), "the axis edge is gone");
    check.equal(shape.position(mirror.image(half.bottomRight())).x, -10.0, "mirrored corner x");
    check.equal(shape.position(mirror.image(half.bottomRight())).y, 0.0, "mirrored corner y");
    check.equal(shape.length(mirror.image(half.right())), 20.0, "mirrored edge");
    for (std::size_t i = 0; i < shape.edges.size(); ++i)
        check.that(shape.edges[i].end == shape.edges[(i + 1) % shape.edges.size()].start, "outline is one closed loop");
}

static void mirrorAppliedLastKeepsLaterEditsSymmetric(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});
    // Added after the mirror in the list, but the mirror waits for them.
    doc.add(panel, AdjustPoint{.corner = half.bottomRight(), .dx = -3_cm, .dy = 0_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = half.top(), .bulge = 2_cm});

    Result result = evaluate(doc);
    const Pattern* shape = &result[panel];
    check.that(result.ok(), "no errors");
    check.equal(shape->position(half.bottomRight()).x, 7.0, "the drawn corner moved");
    check.equal(shape->position(mirror.image(half.bottomRight())).x, -7.0, "and so did its mirror image");
    check.that(shape->edge(half.top()).arc.has_value() && shape->edge(mirror.image(half.top())).arc.has_value(), "the later curve is on both halves");
    check.that(!outlineBefore(doc, curve).contains(mirror.image(half.top())), "to the curve, the mirror has not happened yet");

    // The old behaviour is still available: the mirror runs in its place.
    doc.set(mirror, &Mirror::appliedLast, false);
    result = evaluate(doc);
    shape = &result[panel];
    check.equal(shape->position(half.bottomRight()).x, 7.0, "in place: the drawn corner moved");
    check.equal(shape->position(mirror.image(half.bottomRight())).x, -10.0, "in place: its image did not");
    check.that(!shape->edge(mirror.image(half.top())).arc.has_value(), "in place: the curve is on one half only");
}

static void operationsCanBeSwitchedOffAndSteppedThrough(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = half.top(), .bulge = 2_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});
    OpRef<AdjustPoint> adjust = doc.add(panel, AdjustPoint{.corner = half.bottomRight(), .dx = -3_cm, .dy = 0_cm});

    check.that(doc.enabled(curve), "operations start switched on");
    doc.setEnabled(curve, false);
    Result result = evaluate(doc);
    check.that(result.ok() && !result[panel].edge(half.top()).arc.has_value(), "switched off: the curve is skipped");
    check.equal(result[panel].position(half.bottomRight()).x, 7.0, "the others still run");
    doc.setEnabled(curve, true);
    check.that(evaluate(doc)[panel].edge(half.top()).arc.has_value(), "switched back on");

    // Stepping back: the panel as it was after each operation, mirror included.
    const Result afterRect = evaluateUpTo(doc, half);
    check.that(!afterRect[panel].edge(half.top()).arc.has_value(), "after step 1: not yet curved");
    check.that(afterRect[panel].contains(mirror.image(half.top())), "but already shown mirrored, since the mirror is applied last");
    check.equal(afterRect[panel].position(half.bottomRight()).x, 10.0, "after step 1: corner not yet moved");
    const Result afterCurve = evaluateUpTo(doc, curve);
    check.that(afterCurve[panel].edge(half.top()).arc.has_value(), "after step 2: curved");
    check.equal(afterCurve[panel].position(half.bottomRight()).x, 10.0, "after step 2: corner still not moved");
    check.equal(evaluateUpTo(doc, adjust)[panel].position(half.bottomRight()).x, 7.0, "after the last step: everything");
    check.that(doc.enabled(adjust), "stepping back changes nothing in the document");

    PanelCopy copy = doc.copy(panel, Panel{.name = "copy"});
    doc.setEnabled(copy[curve], false);
    check.that(doc.enabled(curve) && !doc.enabled(copy[curve]), "each operation has its own switch");
}

static void circleAndItsSlices(Check& check) {
    Document doc;
    Param sweep = doc.add(Parameter{.name = "sweep", .value = 360_deg});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Circle> circle = doc.add(panel, Circle{.radius = 10_cm, .sweep = sweep});

    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.that(result[panel].edges.size() == 1 && result[panel].vertices.size() == 1, "a whole circle is one edge with one corner");
    check.equal(result[panel].length(circle.arc()), 20.0 * pi, "its length is the circumference");
    check.that(encloses(result[panel], Vec2{0, 0}) && encloses(result[panel], Vec2{7, 7}) && !encloses(result[panel], Vec2{8, 8}), "inside and outside");
    check.that(std::abs(bounds(result[panel])->max.x - bounds(result[panel])->min.x - 20.0) < 0.02, "as wide as its diameter, within drawing tolerance");

    doc.set(sweep, 90_deg);
    result = evaluate(doc);
    check.that(result.ok() && result[panel].edges.size() == 3, "a slice has the arc and two sides");
    check.equal(result[panel].length(circle.arc()), 5.0 * pi, "a quarter of the circumference");
    check.equal(result[panel].length(circle.startSide()), 10.0, "sides as long as the radius");
    check.equal(result[panel].position(circle.centre()).y, 0.0, "meeting at the centre");

    Document plain;
    PanelId p = plain.add(Panel{.name = "p"});
    OpRef<Circle> whole = plain.add(p, Circle{.radius = 5_cm});
    check.equal(evaluate(plain)[p].length(whole.arc()), 10.0 * pi, "sweep defaults to a whole circle");
}

static void cutCanEnterAndLeaveThroughOneEdge(Check& check) {
    // A chord across a whole circle: both ends of the cut are on its one edge.
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Circle> circle = doc.add(panel, Circle{.radius = 10_cm});
    // The circle's edge starts at the bottom and runs counter-clockwise:
    // 0.25 is the rightmost point, 0.5 the top.
    OpRef<LineBetween> chord = doc.add(panel, LineBetween{.from = {circle.arc(), 0.25}, .to = {circle.arc(), 0.5}});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = chord.line(), .keep = Cut::Side::Left});

    // Looking from right to top, the small cap is on the right; the left side
    // is the large part, which keeps both ends of the circle's edge.
    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.that(result[panel].edges.size() == 3, "two pieces of the circle and the cut edge");
    check.equal(result[panel].length(circle.arc()) + result[panel].length(cut.farPart(circle.arc())), 15.0 * pi, "together three quarters of the circumference");
    check.equal(result[panel].length(cut.edge()), 10.0 * std::sqrt(2.0), "the chord");
    check.that(nameOf(Reference(cut.farPart(circle.arc())), doc) == "far part of arc of 1. Circle", "the second piece has a name of its own");

    doc.set(cut, &Cut::keep, Cut::Side::Right);
    result = evaluate(doc);
    check.that(result.ok() && result[panel].edges.size() == 2, "the cap: one piece of the circle and the cut edge");
    check.equal(result[panel].length(circle.arc()), 5.0 * pi, "a quarter of the circumference");

    // The same on a straight edge: a notch cut out of a rectangle's side with a curved top.
    Document notch;
    PanelId n = notch.add(Panel{.name = "n"});
    OpRef<Rect> rect = notch.add(n, Rect{.width = 40_cm, .height = 20_cm});
    notch.add(n, Curve{.edge = rect.top(), .bulge = 6_cm});
    OpRef<LineBetween> across = notch.add(n, LineBetween{.from = {rect.top(), 0.2}, .to = {rect.top(), 0.8}});
    OpRef<Cut> flat = notch.add(n, Cut{.along = across.line(), .keep = Cut::Side::Left});
    result = evaluate(notch);
    check.that(result.ok(), "a chord across a curved top: no errors");
    check.that(result[n].contains(rect.top()) && result[n].contains(flat.farPart(rect.top())), "the top edge's two ends survive, each with a name");
    check.that(result[n].edges.size() == 6, "three sides, two pieces of the top, and the cut");
}

static void mirrorAcrossACurvedEdgeIsAnError(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    doc.add(panel, Curve{.edge = half.left(), .bulge = 1_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = half.left()});

    check.that(evaluate(doc).failed(mirror), "reported");
}

static void instanceFollowsItsSource(Check& check) {
    Document doc;
    Param width = doc.add(Parameter{.name = "width", .value = 10_cm});
    PanelId instancePanel = doc.add(Panel{.name = "instance"});  // listed before its source
    PanelId source = doc.add(Panel{.name = "source", .construction = true});
    OpRef<Rect> rect = doc.add(source, Rect{.width = width, .height = 20_cm});
    doc.add(source, Curve{.edge = rect.top(), .bulge = 2_cm});
    doc.add(instancePanel, Instance{.source = source, .mirrored = true});

    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.that(doc.get(source).construction, "source is a construction panel");
    check.that(result[instancePanel].flipped && !result[source].flipped, "only the instance is flipped");
    check.equal(result[instancePanel].position(rect.bottomRight()).x, -10.0, "flipped corner");
    check.equal(result[instancePanel].length(rect.top()), result[source].length(rect.top()), "curved edge has the same length");
    check.equal(result[instancePanel].edge(rect.top()).arc->sweep, result[source].edge(rect.top()).arc->sweep, "and bulges the same way");

    doc.set(width, 16_cm);
    check.equal(evaluate(doc)[instancePanel].length(rect.bottom()), 16.0, "follows the source");
}

static void instanceLoopsAndMissingSourcesAreErrors(Check& check) {
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
    check.that(result.failed(aOfB) || result.failed(bOfA), "the loop is reported");
    check.that(result.failed(cOfGone), "the missing source is reported");
}

static void adjustPointMovesOneCorner(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 40_cm});
    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = -30_cm, .dy = 0_cm});

    Result result = evaluate(doc);
    check.that(result.ok(), "no errors");
    check.equal(result[panel].length(rect.top()), 0.0, "top edge collapses");
    check.equal(result[panel].length(rect.right()), 50.0, "right edge becomes the 30-40-50 diagonal");
}

static void negativeNumbersStayNumbers(Check& check) {
    check.that(asNumber(-Expr(7.5)).has_value() && *asNumber(-Expr(7.5)) == -7.5, "-7.5 is a plain number");
    const std::variant<Expr, TextError> parsed = parseExpr("-7.5", Document{});
    const Expr* expr = std::get_if<Expr>(&parsed);
    check.that(expr && asNumber(*expr) && *asNumber(*expr) == -7.5, "typed -7.5 is a plain number");
}

static void cornersOfArcsCanMove(Check& check) {
    Document doc;
    PanelId slice = doc.add(Panel{.name = "slice"});
    OpRef<Circle> circle = doc.add(slice, Circle{.radius = 10_cm, .sweep = 90_deg});
    const double before = evaluate(doc)[slice].length(circle.arc());
    doc.add(slice, AdjustPoint{.corner = circle.start(), .dx = 1_cm, .dy = 0_cm});
    Result moved = evaluate(doc);
    check.that(moved.ok(), "moving a corner of a slice's arc is fine");
    check.that(std::abs(moved[slice].length(circle.arc()) - before) > 1e-6, "the arc follows its corner");
    check.that(std::abs(moved[slice].edge(circle.arc()).arc->sweep - 90.0) < 1e-6, "and keeps turning through the same angle");

    Document whole;
    PanelId disc = whole.add(Panel{.name = "disc"});
    OpRef<Circle> full = whole.add(disc, Circle{.radius = 10_cm});
    whole.add(disc, AdjustPoint{.corner = full.start(), .dx = 1_cm, .dy = 0_cm});
    check.that(!evaluate(whole).ok(), "a whole circle's only point cannot be moved");

    PanelId ringPanel = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> ring = doc.add(ringPanel, RingSector{.innerLength = 40_cm, .sweep = 120_deg, .depth = 20_cm});
    doc.add(ringPanel, AdjustPoint{.corner = ring.outerStart(), .dx = 2_cm, .dy = 1_cm});
    check.that(evaluate(doc).ok(), "a ring's corners move too");
}

static void anArcCanBecomeAFreeCurveThatFollowsIt(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "slice"});
    OpRef<Circle> circle = doc.add(panel, Circle{.radius = 10_cm, .sweep = 90_deg});
    const Pattern shape = evaluate(doc)[panel];
    const Edge& arc = shape.edge(circle.arc());
    const std::optional<CubicCurve> follow = cubicFromArc(shape, arc);
    check.that(follow.has_value(), "an arc has a matching free curve");
    if (!follow) return;
    const Vec2 start = shape.vertices[arc.start].position, end = shape.vertices[arc.end].position;
    doc.add(panel, Bezier{.edge = circle.arc(), .startHandle = {follow->control1.x - start.x, follow->control1.y - start.y},
                          .endHandle = {follow->control2.x - end.x, follow->control2.y - end.y}});
    Result r = evaluate(doc);
    check.that(r.ok(), "no errors");
    check.that(std::abs(r[panel].length(circle.arc()) - 10.0 * pi / 2.0) < 0.01, "same length as the arc it replaced");
}

static void bakingReplacesFormulasWithNumbers(Check& check) {
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 80});
    PanelId band = doc.add(Panel{.name = "band"});
    OpRef<Rect> rect = doc.add(band, Rect{.width = waist / 2, .height = 10_cm});
    check.that(bake(doc, band) == 1, "one formula was replaced");
    doc.set(waist, Expr(100));
    Result r = evaluate(doc);
    check.that(r.ok(), "no errors");
    check.equal(r[band].length(rect.bottom()), 40.0, "the width no longer follows the parameter");
    check.that(bake(doc, band) == 0, "baking again changes nothing");
}

static void aFullRingClosesOnItself(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 360_deg, .depth = 60_cm});
    Pattern shape = evaluate(doc)[panel];
    check.that(shape.edge(ring.startSide()).seam && shape.edge(ring.endSide()).seam, "its two sides are a seam");
    check.that(!shape.edge(ring.inner()).seam && !shape.edge(ring.outer()).seam, "its circles are real edges");
    Document half;
    PanelId h = half.add(Panel{.name = "half"});
    OpRef<RingSector> open = half.add(h, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
    check.that(!evaluate(half)[h].edge(open.startSide()).seam, "a partial ring's sides are edges");
}

static void seamsFollowTheirEdges(Check& check) {
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 80});
    PanelId band = doc.add(Panel{.name = "band"});
    OpRef<Rect> bandRect = doc.add(band, Rect{.width = waist, .height = 4_cm});
    PanelId skirt = doc.add(Panel{.name = "skirt"});
    OpRef<Rect> skirtRect = doc.add(skirt, Rect{.width = waist * 1.5, .height = 60_cm});
    SeamId seam = doc.add(Seam{.a = {band, bandRect.bottom()}, .b = {skirt, skirtRect.top()}});

    Result r = evaluate(doc);
    check.that(r.ok() && r.seams.size() == 1 && r[seam].sewn(), "the seam is sewn");
    check.equal(r[seam].lengthA, 80.0, "one side is the band");
    check.equal(r[seam].lengthB, 120.0, "the other is gathered onto it");

    doc.set(waist, Expr(90));
    check.equal(evaluate(doc)[seam].lengthA, 90.0, "it follows the parameter");

    // Cutting the sewn edge away breaks the seam but is no error.
    OpRef<Line> line = doc.add(skirt, Line{.from = skirtRect.topLeft(), .angle = -45_deg});
    OpRef<Cut> cut = doc.add(skirt, Cut{.along = line, .keep = Cut::Side::Right});
    Result afterCut = evaluate(doc);
    check.that(afterCut.ok(), "cutting is not an error");
    const bool topRemains = afterCut[skirt].contains(skirtRect.top());
    check.that(afterCut[seam].sewn() == topRemains, "the seam is broken exactly when its edge is gone");
    doc.remove(OpId(cut));
    check.that(evaluate(doc)[seam].sewn(), "and comes back with the edge");

    doc.remove(skirt);
    check.that(doc.seams().empty(), "removing a panel removes its seams");
}

static void documentsRoundTripThroughText(Check& check) {
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist \"low\"", .value = 80});
    Param ease = doc.add(Parameter{.name = "ease", .value = max(waist / 20, 2) - abs(-waist) * 0});
    PanelId half = doc.add(Panel{.name = "half", .x = 12.5, .y = -3, .angle = 30});
    OpRef<Rect> rect = doc.add(half, Rect{.width = waist / 4 + ease, .height = 60_cm});
    OpRef<Bezier> curve = doc.add(half, Bezier{.edge = rect.top(), .startHandle = {5, -7.5}, .endHandle = {-5, 3}});
    doc.add(half, AdjustPoint{.corner = rect.bottomRight(), .dx = 4_cm, .dy = 0_cm});
    OpRef<LineBetween> line = doc.add(half, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 0.25}});
    OpRef<Cut> cut = doc.add(half, Cut{.along = line.line(), .keep = Cut::Side::Right});
    OpRef<Mirror> fold = doc.add(half, Mirror{.across = cut.farPart(rect.left())});
    doc.setEnabled(curve, false);
    doc.arrange(half, Arrangement{.placed = true, .x = 1, .y = 100, .z = 25.5, .turn = 90, .tilt = -5, .roll = 180, .wrap = 60});
    PanelId rest = doc.add(Panel{.name = "rest", .construction = true});
    doc.add(rest, Offcut{.of = cut});
    PanelId twin = doc.add(Panel{.name = "twin"});
    doc.add(twin, Instance{.source = half, .mirrored = true});
    PanelId ring = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> sector = doc.add(ring, RingSector{.innerLength = waist, .sweep = 180_deg, .depth = 50_cm});
    doc.add(Seam{.a = {half, fold.image(rect.top())}, .b = {ring, sector.inner()}, .reversed = true});

    const std::string text = toText(doc);
    std::variant<Document, FileError> read = fromText(text);
    check.that(std::holds_alternative<Document>(read), "the text reads back");
    if (!std::holds_alternative<Document>(read)) return;
    Document& again = std::get<Document>(read);
    check.that(toText(again) == text, "and writes the same text again");
    const Result before = evaluate(doc), after = evaluate(again);
    check.that(before.errors.size() == after.errors.size() && before.seams.size() == after.seams.size(), "same problems, same seams");
    check.equal(after[half].length(rect.bottom()), before[half].length(rect.bottom()), "same shapes");
    check.that(again.get(waist).name == "waist \"low\"" && again.get(half).angle == 30.0 && !again.enabled(curve), "names, placement and switched-off steps survive");
    check.that(again.get(half).arrangement == doc.get(half).arrangement && !again.get(rest).arrangement.placed, "arrangement in space survives");
    // New things made after reading do not collide with what was read.
    const PanelId added = again.add(Panel{.name = "new"});
    check.that(!doc.contains(added), "ids continue after the highest one read");

    const auto bad = [](const char* file) { return std::holds_alternative<FileError>(fromText(file)); };
    check.that(bad(""), "an empty file is refused");
    check.that(bad("hello"), "so is another kind of file");
    check.that(bad("clothcad 2\n"), "and a newer version");
    std::variant<Document, FileError> broken = fromText("clothcad 1\npanel 1 \"a\" 0 0 0 0\nop 2 Rect 1 2 n 5 oops 0 0\n");
    check.that(std::holds_alternative<FileError>(broken) && std::get<FileError>(broken).line == 3, "a bad line is reported by number");
    check.that(bad("clothcad 1\npanel 1 \"a\" 0 0 0 0\npanel 1 \"b\" 0 0 0 0\n"), "an id used twice is refused");
}

static void modifyingOperationCannotComeFirst(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = EdgeRef{}});

    check.that(evaluate(doc).failed(mirror), "reported");
}

// ---- Text, fields and drawing: what the window is built on --------------------------

static void formulasRoundTripThroughText(Check& check) {
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    doc.add(Parameter{.name = "ease", .value = 2_cm});

    const auto roundTrip = [&](const char* text, double expected, const char* shown) {
        const std::variant<Expr, TextError> parsed = parseExpr(text, doc);
        check.that(std::holds_alternative<Expr>(parsed), text);
        if (!std::holds_alternative<Expr>(parsed)) return;
        const Expr expr = std::get<Expr>(parsed);
        check.that(formatExpr(expr, doc) == shown, shown);
        Param probe = doc.add(Parameter{.name = "probe", .value = expr});
        check.equal(evaluate(doc)[probe], expected, text);
        doc.remove(probe);
    };
    roundTrip("70", 70.0, "70");
    roundTrip("waist/4+1", 18.5, "waist / 4 + 1");
    roundTrip("(waist + ease) * 2", 144.0, "(waist + ease) * 2");
    roundTrip("waist - (ease - 1)", 69.0, "waist - (ease - 1)");
    roundTrip("waist / (2 * pi)", 70.0 / (2.0 * pi), "waist / (2 * 3.141592654)");
    roundTrip("-waist + 5mm", -69.5, "-waist + 0.5");
    roundTrip("min(waist, 60cm) + max(1, ease) + abs(-3)", 65.0, "min(waist, 60) + max(1, ease) + abs(-3)");
    roundTrip("clamp(waist, 0, 50)", 50.0, "min(max(waist, 0), 50)");
    roundTrip("360 deg * 0.5", 180.0, "360 * 0.5");
    check.that(formatExpr(waist / 4 + 1_cm, doc) == "waist / 4 + 1", "formulas built in code format the same way");
}

static void badFormulaTextIsReportedWithAPosition(Check& check) {
    Document doc;
    doc.add(Parameter{.name = "waist", .value = 70_cm});

    const auto problemAt = [&](const char* text, std::size_t position) {
        const std::variant<Expr, TextError> parsed = parseExpr(text, doc);
        check.that(std::holds_alternative<TextError>(parsed), text);
        if (const TextError* error = std::get_if<TextError>(&parsed)) {
            check.that(error->position == position, text);
            check.that(!error->message.empty(), "has a message");
        }
    };
    problemAt("", 0);
    problemAt("waist +", 7);
    problemAt("wiast / 2", 0);
    problemAt("(waist", 6);
    problemAt("waist 4", 6);
    problemAt("sqrt(4)", 0);
    problemAt("min(1)", 5);
}

static void operationsCanBeEditedWithoutKnowingTheirType(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Mirror> mirror = doc.add(panel, Mirror{.across = rect.left()});
    const OpId untyped = rect;

    const std::vector<Field> listed = fields(doc.get(untyped));
    check.that(label(doc.get(untyped)) == "Rectangle", "label");
    check.that(listed.size() == 2 && listed[0].label == "Width" && listed[1].label == "Height", "fields in order");
    check.that(formatExpr(listed[1].value, doc) == "20", "field value");
    check.that(fields(doc.get(OpId(mirror))).empty(), "a mirror has no formula fields");

    doc.set(untyped, listed[1].id, 35_cm);
    check.equal(evaluate(doc)[panel].length(rect.right()), 35.0, "set through the field id");
    check.throws<std::out_of_range>([&] { doc.set(untyped, FieldId{2}, 1_cm); }, "no third field");
}

static void outlinesCanBeDrawnAndHitTested(Check& check) {
    Document doc;
    PanelId square = doc.add(Panel{.name = "square"});
    doc.add(square, Rect{.width = 10_cm, .height = 20_cm});
    PanelId ring = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> sector = doc.add(ring, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
    PanelId empty = doc.add(Panel{.name = "empty"});

    Result result = evaluate(doc);
    check.that(outlinePoints(result[square]).size() == 4, "a rectangle is its four corners");
    check.that(outlinePoints(result[ring]).size() > 40, "arcs become many short pieces");
    check.equal(bounds(result[square])->max.y, 20.0, "bounds of the rectangle");
    const double outer = 70.0 / pi + 60.0;
    check.equal(bounds(result[ring])->min.x, -outer, "bounds follow the arc");
    check.that(std::abs(bounds(result[ring])->min.y + outer) < 0.01, "to the bottom of the arc, within tolerance");
    check.that(!bounds(result[empty]).has_value(), "an empty panel has no bounds");

    check.that(encloses(result[square], Vec2{5.0, 5.0}), "inside the rectangle");
    check.that(!encloses(result[square], Vec2{15.0, 5.0}), "outside the rectangle");
    check.that(encloses(result[ring], Vec2{0.0, -50.0}), "inside the ring's cloth");
    check.that(!encloses(result[ring], Vec2{0.0, -5.0}), "the hole in the middle is outside");
    check.equal(midpoint(result[ring], result[ring].edge(sector.inner())).y, -70.0 / pi, "midpoint lies on the arc");
}

static void viewsCanTellWhatDrivesAnEdge(Check& check) {
    Document doc;
    Param bicep = doc.add(Parameter{.name = "bicep", .value = 30_cm});
    PanelId panel = doc.add(Panel{.name = "sleeve"});
    OpRef<Rect> half = doc.add(panel, Rect{.width = bicep / 2, .height = 60_cm});
    OpRef<Mirror> fold = doc.add(panel, Mirror{.across = half.left()});
    Result result = evaluate(doc);

    const OpData& data = doc.get(OpId(half));
    const std::vector<Dimension> driven = dimensions(data, half);
    check.that(driven.size() == 4, "a rectangle's four edges are its width and height");
    const std::vector<Field> listed = fields(data);
    for (const Dimension& d : driven) {
        const bool horizontal = d.edge == half.top() || d.edge == half.bottom();
        check.that(listed[d.field.index].label == (horizontal ? "Width" : "Height"), "edge matches its field");
    }
    check.that(!asNumber(listed[0].value).has_value(), "width is a formula");
    check.equal(*asNumber(listed[1].value), 60.0, "height is a typed number");
    check.equal(*valueOf(listed[0].value, result), 15.0, "the formula's value");
    check.equal(*valueOf(listed[0].value, result), result[panel].length(half.bottom()), "equals the edge it drives");
    check.that(dimensions(doc.get(OpId(fold)), fold).empty(), "a mirror drives no edge length");

    doc.remove(bicep);
    check.that(!valueOf(listed[0].value, evaluate(doc)).has_value(), "no value once its parameter is gone");
}

static void mirrorRecordsItsAxis(Check& check) {
    Document doc;
    PanelId left = doc.add(Panel{.name = "left"});
    OpRef<Rect> half = doc.add(left, Rect{.width = 15_cm, .height = 60_cm});
    OpRef<Mirror> fold = doc.add(left, Mirror{.across = half.left()});
    PanelId right = doc.add(Panel{.name = "right"});
    doc.add(right, Instance{.source = left, .mirrored = true});
    PanelId shifted = doc.add(Panel{.name = "shifted"});
    OpRef<Rect> other = doc.add(shifted, Rect{.width = 15_cm, .height = 60_cm});
    doc.add(shifted, Mirror{.across = other.right()});

    Result result = evaluate(doc);
    check.that(result[left].axes.size() == 1, "one axis");
    check.that(result[left].axes[0].mirror == OpId(fold), "named by its mirror");
    check.equal(result[left].axes[0].start.x, 0.0, "on the centre line");
    check.equal(std::abs(result[left].axes[0].end.y - result[left].axes[0].start.y), 60.0, "as long as the edge");
    check.that(result[right].axes.size() == 1, "an instance keeps the axis");
    check.equal(result[shifted].axes[0].start.x, 15.0, "axis at the right edge");
}

// ---- What authoring in the window is built on ---------------------------------------

static void referencesCanBeReadNamedAndSetWithoutKnowingTheType(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "sleeve"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 15_cm, .height = 60_cm});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = EdgeRef{}, .bulge = 2_cm});
    OpRef<Mirror> fold = doc.add(panel, Mirror{.across = rect.left()});
    OpRef<Line> line = doc.add(panel, Line{.from = rect.bottomRight(), .angle = 100_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line()});
    PanelId other = doc.add(Panel{.name = "other arm"});
    OpRef<Instance> instance = doc.add(other, Instance{.source = panel});

    const std::vector<ReferenceField> slots = references(doc.get(OpId(curve)));
    check.that(slots.size() == 1 && slots[0].label == "Edge", "a curve has one reference, its edge");
    check.that(std::holds_alternative<EdgeRef>(slots[0].value), "which takes an edge");
    check.that(nameOf(slots[0].value, doc) == "nothing chosen", "not chosen yet");
    check.that(references(doc.get(OpId(rect))).empty(), "a rectangle refers to nothing");

    doc.set(OpId(curve), slots[0].id, Reference(rect.top()));
    check.that(doc.get(curve).edge == rect.top(), "set through the reference id");
    check.that(nameOf(Reference(doc.get(curve).edge), doc) == "top edge of 1. Rectangle", "named in words");
    check.that(nameOf(Reference(fold.image(rect.right())), doc) == "mirrored right edge of 1. Rectangle", "a mirror image");
    check.that(nameOf(Reference(rect.topLeft()), doc) == "top-left corner of 1. Rectangle", "a corner");
    check.that(nameOf(Reference(line.line()), doc) == "line of 4. Angled line", "a line");
    check.that(nameOf(Reference(cut.edge()), doc) == "cut edge of 5. Cut", "an edge made by a cut");
    check.that(nameOf(references(doc.get(OpId(instance)))[0].value, doc) == "sleeve", "a panel");

    check.throws<std::invalid_argument>([&] { doc.set(OpId(curve), slots[0].id, Reference(rect.topLeft())); }, "a corner is not an edge");
    check.throws<std::out_of_range>([&] { doc.set(OpId(curve), ReferenceId{1}, Reference(rect.top())); }, "no second reference");

    doc.remove(OpId(line));
    check.that(nameOf(Reference(doc.get(cut).along), doc) == "something that was removed", "a removed target says so");
    check.that(doc.panelOf(OpId(instance)) == other, "which panel an operation is in");
}

static void outlineBeforeAnOperationIsWhatItWorksOn(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 40_cm, .height = 30_cm});
    OpRef<Line> line = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 45_deg});
    OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Left});
    OpRef<Curve> curve = doc.add(panel, Curve{.edge = rect.left(), .bulge = 1_cm});

    check.that(outlineBefore(doc, rect).edges.empty(), "nothing exists before the first operation");
    check.that(outlineBefore(doc, cut).edges.size() == 4, "before the cut: the whole rectangle");
    check.that(outlineBefore(doc, cut).contains(line.line()), "and the line to cut along");
    check.that(outlineBefore(doc, curve).edges.size() == 3, "before the curve: the cut triangle");
    check.that(!outlineBefore(doc, curve).contains(rect.right()), "without the edge the cut removed");
    check.that(!outlineBefore(doc, curve).edge(rect.left()).arc.has_value(), "and not yet curved");
    check.that(evaluate(doc)[panel].edge(rect.left()).arc.has_value(), "the document itself is untouched");
    check.that(outlineBefore(doc, OpId{9999}).edges.empty(), "an unknown operation gives nothing");
}

static void smallThingsTheWindowNeeds(Check& check) {
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 10_cm});
    doc.add(panel, Curve{.edge = rect.top(), .bulge = 5_cm});
    doc.setConstruction(panel, true);
    check.that(doc.get(panel).construction, "a panel can be made a base");

    Result result = evaluate(doc);
    const std::vector<Vec2> straight = edgePoints(result[panel], result[panel].edge(rect.left()));
    const std::vector<Vec2> curved = edgePoints(result[panel], result[panel].edge(rect.top()));
    check.that(straight.size() == 2, "a straight edge is its two ends");
    check.that(curved.size() > 10, "a curved edge is many points");
    check.equal(curved.front().x, 10.0, "from its start");
    check.equal(curved.back().x, 0.0, "to its end");

    check.that(canBeTypedInFormulas("waist") && canBeTypedInFormulas("skirt_length2"), "ordinary names");
    check.that(!canBeTypedInFormulas("") && !canBeTypedInFormulas("2waist") && !canBeTypedInFormulas("hip size"), "not empty, digit-first or spaced");
    check.that(!canBeTypedInFormulas("pi") && !canBeTypedInFormulas("min") && !canBeTypedInFormulas("cm"), "not a word formulas use");
}

static void everyFieldStatesWhatValuesItAccepts(Check& check) {
    Document doc;
    Param sweep = doc.add(Parameter{.name = "sweep", .value = 360_deg});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<RingSector> ring = doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = sweep, .depth = 60_cm});
    PanelId other = doc.add(Panel{.name = "other"});
    OpRef<Rect> rect = doc.add(other, Rect{.width = 10_cm, .height = 10_cm});
    OpRef<Line> line = doc.add(other, Line{.from = rect.topLeft(), .angle = -45_deg});

    const std::vector<Field> ringFields = fields(doc.get(OpId(ring)));
    check.that(ringFields[1].limits.rule() == "must be greater than 0 and at most 360", "the sweep's rule, in words");
    check.that(ringFields[1].limits.allows(360.0) && !ringFields[1].limits.allows(0.0) && !ringFields[1].limits.allows(360.5), "and as a test");
    check.that(fields(doc.get(OpId(rect)))[0].limits.rule() == "must be greater than 0", "a width must be positive");
    check.that(fields(doc.get(OpId(line)))[0].limits.rule().empty(), "an angle may be anything");
    check.that(fields(doc.get(OpId(line)))[0].limits.allows(-45.0), "including negative");

    // evaluate() enforces exactly these rules.
    check.that(evaluate(doc).ok(), "all within limits, negative angle included");
    for (double bad : {0.0, -10.0, 361.0}) {
        doc.set(sweep, bad);
        check.that(evaluate(doc).failed(ring), "a sweep outside its limits is an error on the operation");
    }
    doc.set(sweep, 360_deg);
    doc.set(rect, &Rect::width, -5_cm);
    Result result = evaluate(doc);
    check.that(result.failed(rect) && !result.failed(ring), "a negative width fails that rectangle only");
    check.that(result[other].edges.empty(), "and it makes no outline");
    check.that(!result.failed(line), "operations after it stay quiet; the one error is enough");
}

// ---- Document verbs -------------------------------------------------------------

static void copyKeepsExpressionsAndIsOtherwiseIndependent(Check& check) {
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId band = doc.add(Panel{.name = "waistband"});
    OpRef<Rect> bandRect = doc.add(band, Rect{.width = waist, .height = 4_cm});

    PanelCopy skirt = doc.copy(band, Panel{.name = "skirt"});
    OpRef<Rect> skirtRect = skirt[bandRect];
    doc.set(skirtRect, &Rect::height, 60_cm);

    check.that(!(OpId(skirtRect) == OpId(bandRect)), "the copy has its own operation");
    check.that(doc.get(skirt).name == "skirt", "the copy has its own name");

    doc.set(waist, 80_cm);
    Result result = evaluate(doc);
    check.equal(result[band].length(bandRect.top()), 80.0, "original follows waist");
    check.equal(result[skirt].length(skirtRect.top()), 80.0, "copy follows waist");
    check.equal(result[band].length(bandRect.left()), 4.0, "original keeps its height");
    check.equal(result[skirt].length(skirtRect.left()), 60.0, "copy has its own height");
    check.throws<std::out_of_range>([&] { (void)skirt[skirtRect]; }, "only originals can be looked up in a copy");
}

static void documentCopyIsAnIndependentSnapshot(Check& check) {
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = waist, .height = 4_cm});

    const Document snapshot = doc;  // what undo stores
    doc.set(waist, 80_cm);
    doc.rename(panel, "renamed");

    check.equal(evaluate(snapshot)[panel].length(rect.top()), 70.0, "snapshot keeps the old value");
    check.equal(evaluate(doc)[panel].length(rect.top()), 80.0, "document has the new value");
    check.that(snapshot.get(panel).name == "panel", "snapshot keeps the old name");
}

static void removeRenameAndMove(Check& check) {
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    OpRef<Rect> r1 = doc.add(a, Rect{.width = 1_cm, .height = 1_cm});
    OpRef<Rect> r2 = doc.add(a, Rect{.width = 2_cm, .height = 2_cm});
    OpRef<Rect> r3 = doc.add(a, Rect{.width = 3_cm, .height = 3_cm});

    doc.rename(waist, "waistline");
    check.that(doc.get(waist).name == "waistline", "parameter renamed");

    doc.move(r3, 0);
    check.that(doc.operations(a) == std::vector<OpId>{r3, r1, r2}, "moved to the front");
    doc.move(r3, 2);
    check.that(doc.operations(a) == std::vector<OpId>{r1, r2, r3}, "moved to the back");

    doc.remove(r2);
    check.that(doc.operations(a) == std::vector<OpId>{r1, r3}, "operation removed");
    check.that(!doc.contains(OpId(r2)), "removed operation is gone");

    doc.remove(b);
    check.that(doc.panels() == std::vector<PanelId>{a}, "panel removed");
}

static void unknownHandlesThrow(Check& check) {
    Document doc;
    Document other;
    Param foreign = other.add(Parameter{.name = "x", .value = 1});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 1_cm, .height = 1_cm});
    doc.remove(panel);

    check.throws<std::out_of_range>([&] { doc.set(foreign, 2); }, "set unknown parameter");
    check.throws<std::out_of_range>([&] { (void)doc.get(panel); }, "get removed panel");
    check.throws<std::out_of_range>([&] { doc.add(panel, Rect{}); }, "add to removed panel");
    check.throws<std::out_of_range>([&] { doc.set(rect, &Rect::width, 2_cm); }, "set on removed operation");
    check.throws<std::out_of_range>([&] { doc.remove(OpId(rect)); }, "remove removed operation");
}

int main() {
    return runTests({
        {"formulas are computed", formulasAreComputed},
        {"unit literals are centimetres", unitLiteralsAreCentimetres},
        {"parameter may be defined before the one it uses", parameterMayBeDefinedBeforeTheOneItUses},
        {"circular parameters are errors", circularParametersAreErrors},
        {"division by zero is an error", divisionByZeroIsAnError},
        {"rectangle has named edges and corners", rectangleHasNamedEdgesAndCorners},
        {"rectangle with bad size is an error on that operation", rectangleWithBadSizeIsAnErrorOnThatOperation},
        {"second rectangle in a panel is an error", secondRectangleInAPanelIsAnError},
        {"operation using a removed parameter is an error", operationUsingARemovedParameterIsAnError},
        {"ring sector inner edge has the requested length", ringSectorInnerEdgeHasTheRequestedLength},
        {"full circle keeps the same four edges", fullCircleKeepsTheSameFourEdges},
        {"ring sector with bad sweep is an error", ringSectorWithBadSweepIsAnError},
        {"curve bends an edge and keeps its name", curveBendsAnEdgeAndKeepsItsName},
        {"curve on a missing edge is an error", curveOnAMissingEdgeIsAnError},
        {"copy points references at the copy", copyPointsReferencesAtTheCopy},
        {"bezier with handles along the edge is straight", bezierWithHandlesAlongTheEdgeIsStraight},
        {"bezier length is measured along the curve", bezierLengthIsMeasuredAlongTheCurve},
        {"bezier follows its corners and replaces an arc", bezierFollowsItsCornersAndReplacesAnArc},
        {"cut through a free curve splits it exactly", cutThroughAFreeCurveSplitsItExactly},
        {"sleeve cap is smooth across the mirror", sleeveCapIsSmoothAcrossTheMirror},
        {"cut keeps the chosen side", cutKeepsTheChosenSide},
        {"cut through a curved edge shortens the arc", cutThroughACurvedEdgeShortensTheArc},
        {"line that misses the panel is an error", lineThatMissesThePanelIsAnError},
        {"operation after a cut that removed its edge is an error", operationAfterACutThatRemovedItsEdgeIsAnError},
        {"line between two points on the outline", lineBetweenTwoPointsOnTheOutline},
        {"split is a cut plus its offcut", splitIsACutPlusItsOffcut},
        {"offcuts of each other are an error, not a hang", offcutsOfEachOtherAreAnErrorNotAHang},
        {"panels have a place and clicks snap to the outline", panelsHaveAPlaceAndClicksSnapToTheOutline},
        {"mirror joins two halves", mirrorJoinsTwoHalves},
        {"mirror applied last keeps later edits symmetric", mirrorAppliedLastKeepsLaterEditsSymmetric},
        {"operations can be switched off and stepped through", operationsCanBeSwitchedOffAndSteppedThrough},
        {"circle and its slices", circleAndItsSlices},
        {"cut can enter and leave through one edge", cutCanEnterAndLeaveThroughOneEdge},
        {"mirror across a curved edge is an error", mirrorAcrossACurvedEdgeIsAnError},
        {"instance follows its source", instanceFollowsItsSource},
        {"instance loops and missing sources are errors", instanceLoopsAndMissingSourcesAreErrors},
        {"adjust point moves one corner", adjustPointMovesOneCorner},
        {"negative numbers stay numbers", negativeNumbersStayNumbers},
        {"corners of arcs can move", cornersOfArcsCanMove},
        {"an arc can become a free curve that follows it", anArcCanBecomeAFreeCurveThatFollowsIt},
        {"baking replaces formulas with numbers", bakingReplacesFormulasWithNumbers},
        {"a full ring closes on itself", aFullRingClosesOnItself},
        {"seams follow their edges", seamsFollowTheirEdges},
        {"documents round-trip through text", documentsRoundTripThroughText},
        {"modifying operation cannot come first", modifyingOperationCannotComeFirst},
        {"formulas round-trip through text", formulasRoundTripThroughText},
        {"bad formula text is reported with a position", badFormulaTextIsReportedWithAPosition},
        {"operations can be edited without knowing their type", operationsCanBeEditedWithoutKnowingTheirType},
        {"outlines can be drawn and hit-tested", outlinesCanBeDrawnAndHitTested},
        {"views can tell what drives an edge", viewsCanTellWhatDrivesAnEdge},
        {"mirror records its axis", mirrorRecordsItsAxis},
        {"references can be read, named and set without knowing the type", referencesCanBeReadNamedAndSetWithoutKnowingTheType},
        {"outline before an operation is what it works on", outlineBeforeAnOperationIsWhatItWorksOn},
        {"small things the window needs", smallThingsTheWindowNeeds},
        {"every field states what values it accepts", everyFieldStatesWhatValuesItAccepts},
        {"copy keeps expressions and is otherwise independent", copyKeepsExpressionsAndIsOtherwiseIndependent},
        {"document copy is an independent snapshot", documentCopyIsAnIndependentSnapshot},
        {"remove, rename and move", removeRenameAndMove},
        {"unknown handles throw", unknownHandlesThrow},
    });
}
