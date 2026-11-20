// Unit tests of Offers.cpp: what the "Add operation" list offers, and what
// an operation added from it starts as.

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/session/Offers.h"

using namespace clothcad;
using namespace clothcad::session;

TEST_CASE("an empty panel is offered shapes; a panel with a shape is offered changes")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "piece"});
    bool shapes = true;
    for (const Offered& offer : offersFor(doc, panel)) shapes = shapes && offer.starting;
    EXPECT((shapes && offersFor(doc, panel).size() == 4), "four shapes, nothing else");

    addOffered(doc, panel, Offer::Rectangle);
    bool changes = true;
    for (const Offered& offer : offersFor(doc, panel)) changes = changes && !offer.starting;
    EXPECT((changes && offersFor(doc, panel).size() == 8), "then eight changes, no shape");
}

TEST_CASE("an added shape works at once; a change that needs an edge asks for it")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "piece"});
    const Added rect = addOffered(doc, panel, Offer::Rectangle);
    EXPECT((std::holds_alternative<Rect>(doc.get(rect.operation)) && !rect.toChoose), "a rectangle needs nothing chosen");
    EXPECT((evaluate(doc).errors.empty()), "and it is not an error");

    const Added curve = addOffered(doc, panel, Offer::Curve);
    EXPECT((std::holds_alternative<Curve>(doc.get(curve.operation))), "a curve is added");
    EXPECT((curve.toChoose.has_value()), "and its edge is to be chosen next");
}

TEST_CASE("an instance follows another panel; a cut is along the panel's latest line")
{
    Document doc;
    const PanelId source = doc.add(Panel{.name = "source"});
    const OpRef<Rect> r = doc.add(source, Rect{.width = 40_cm, .height = 30_cm});
    const PanelId copy = doc.add(Panel{.name = "copy"});
    const Added instance = addOffered(doc, copy, Offer::Instance);
    EXPECT((std::get<Instance>(doc.get(instance.operation)).source == source), "the instance follows the other panel");

    const OpRef<Line> line = doc.add(source, Line{.from = r.bottomLeft(), .angle = 30_deg});
    const Added cut = addOffered(doc, source, Offer::Cut);
    EXPECT((std::get<Cut>(doc.get(cut.operation)).along == line.line()), "the cut is along the line");
    EXPECT((!cut.toChoose), "so nothing is left to choose");
}
