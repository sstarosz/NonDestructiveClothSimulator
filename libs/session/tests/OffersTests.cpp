// Unit tests of Offers.cpp: what the "Add operation" list offers, and what
// an operation added from it starts as.

#include <cmath>

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
    EXPECT((changes && offersFor(doc, panel).size() == 10), "then ten changes, no shape");
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

TEST_CASE("a scale can be added from the list, and starts at the size the panel has")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "piece"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 20_cm});
    bool offered = false;
    for (const Offered& offer : offersFor(doc, panel)) offered = offered || offer.offer == Offer::Scale;
    EXPECT(offered, "a panel with a shape is offered a scale");
    const Added scale = addOffered(doc, panel, Offer::Scale);
    EXPECT((std::holds_alternative<Scale>(doc.get(scale.operation)) && !scale.toChoose), "it needs nothing chosen");
    const Result result = evaluate(doc);
    EXPECT((result.ok() && std::abs(result[panel].length(rect.bottom()) - 30.0) < 1e-9), "and changes nothing until it is set");
}

TEST_CASE("a length for one edge is offered for a shape, and asks for its edge")
{
    Document doc;
    const PanelId empty = doc.add(Panel{.name = "empty"});
    const PanelId panel = doc.add(Panel{.name = "piece"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 20_cm});
    const auto offeredFor = [&](PanelId p) {
        for (const Offered& offer : offersFor(doc, p))
            if (offer.offer == Offer::SetLength) return true;
        return false;
    };
    EXPECT((offeredFor(panel) && !offeredFor(empty)), "a panel with a shape is offered it, an empty one is not");

    const Added added = addOffered(doc, panel, Offer::SetLength);
    EXPECT((std::holds_alternative<SetLength>(doc.get(added.operation)) && added.toChoose.has_value()), "it is added, and its edge is to be chosen next");
    doc.set(added.operation, *added.toChoose, Reference(rect.top()));
    const Result result = evaluate(doc);
    EXPECT((result.ok() && std::get<SetLength>(doc.get(added.operation)).edge == rect.top()), "once the edge is chosen it works");
    EXPECT_NEAR((result[panel].length(rect.top())), (20.0), "the top gets the 20 cm the added length starts at");
}
