// Unit tests of Lengths.cpp: the lengths the table labels, and which a formula sets.

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/session/Lengths.h"

using namespace clothcad;

TEST_CASE("each edge's length is labelled; a formula that sets it is shown with it")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const PanelId panel = doc.add(Panel{.name = "band"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = waist / 2, .height = 4_cm});
    const Result shapes = evaluate(doc);
    const std::vector<session::EdgeLength> lengths = session::lengthsOf(doc, shapes, panel);
    REQUIRE(lengths.size() == 4);
    const auto of = [&](EdgeRef edge) {
        for (const session::EdgeLength& l : lengths)
            if (l.edge == edge) return l;
        throw std::runtime_error("no such edge");
    };
    EXPECT((of(rect.bottom()).length == 35.0 && of(rect.bottom()).formula == "waist / 2"), "the width follows the waist, and says so");
    EXPECT((of(rect.left()).length == 4.0 && of(rect.left()).formula.empty()), "a typed number has no formula");

    // A cut changes the bottom: its length no longer comes from the formula.
    const OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.bottom(), 0.5}});
    doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Right});
    const Result cut = evaluate(doc);
    bool anyFormula = false;
    for (const session::EdgeLength& l : session::lengthsOf(doc, cut, panel)) anyFormula = anyFormula || (l.formula == "waist / 2" && std::abs(l.length - 35.0) > 1e-6);
    EXPECT((!anyFormula), "an edge a cut has shortened shows no formula that would be wrong");
    EXPECT((session::lengthsOf(doc, shapes, PanelId{999}).empty()), "an unknown panel has none");
}
