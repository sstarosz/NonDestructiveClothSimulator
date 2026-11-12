// Tests for the core: each one builds a small Document through the public
// API, evaluates it, and checks the result.

#include <stdexcept>

#include "core/Document.h"
#include "core/Evaluate.h"
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
        {"copy keeps expressions and is otherwise independent", copyKeepsExpressionsAndIsOtherwiseIndependent},
        {"document copy is an independent snapshot", documentCopyIsAnIndependentSnapshot},
        {"remove, rename and move", removeRenameAndMove},
        {"unknown handles throw", unknownHandlesThrow},
    });
}
