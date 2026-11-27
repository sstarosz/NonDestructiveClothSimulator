// Unit tests of Lengths.cpp: the lengths the table labels, and which a formula sets.

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/session/Lengths.h"

using namespace twintailor;

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

TEST_CASE("what sets an edge's length: the last length given to it, else the shape; a later step takes it away")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const PanelId panel = doc.add(Panel{.name = "trousers"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 20_cm, .height = 30_cm});
    const auto setBy = [&](const EdgeRef& edge) { return session::lengthSetBy(doc, evaluate(doc), panel, edge); };
    const auto formulaOf = [&](const EdgeRef& edge) {
        for (const session::EdgeLength& l : session::lengthsOf(doc, evaluate(doc), panel))
            if (l.edge == edge) return l.formula;
        return std::string("no such edge");
    };

    EXPECT((setBy(rect.top()) && setBy(rect.top())->operation == OpId(rect) && setBy(rect.top())->field == FieldId{0}), "at first the rectangle's width sets the top");

    const OpRef<SetLength> first = doc.add(panel, SetLength{.edge = rect.top(), .length = waist / 2});
    EXPECT((setBy(rect.top()) && setBy(rect.top())->operation == OpId(first) && setBy(rect.top())->field == FieldId{0}), "a length given to the top sets it instead");
    EXPECT((formulaOf(rect.top()) == "waist / 2"), "and its formula is the one shown with the length");
    EXPECT((setBy(rect.bottom()) && setBy(rect.bottom())->operation == OpId(rect) && formulaOf(rect.bottom()).empty()), "the bottom is still the rectangle's width");

    const OpRef<SetLength> second = doc.add(panel, SetLength{.edge = rect.top(), .length = 40_cm});
    EXPECT((setBy(rect.top())->operation == OpId(second) && formulaOf(rect.top()).empty()), "the last one wins, and a typed number has no formula");
    doc.setEnabled(second, false);
    EXPECT((setBy(rect.top())->operation == OpId(first)), "one that is switched off does not count");
    doc.setEnabled(second, true);

    doc.add(panel, AdjustPoint{.corner = rect.topRight(), .dx = 5_cm, .dy = 0_cm});
    EXPECT((!setBy(rect.top()) && formulaOf(rect.top()).empty()), "a corner moved afterwards: nothing sets the top any more");

    EXPECT((!session::lengthSetBy(doc, evaluate(doc), PanelId{999}, rect.top()) && !setBy(EdgeRef{})), "an unknown panel or edge has no setting");
}

TEST_CASE("a length given to a drawn edge also sets its mirror image")
{
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "half"});
    const OpRef<Rect> rect = doc.add(panel, Rect{.width = 10_cm, .height = 20_cm});
    const OpRef<Mirror> fold = doc.add(panel, Mirror{.across = rect.left()});
    const OpRef<SetLength> top = doc.add(panel, SetLength{.edge = rect.top(), .length = 15_cm, .keeping = SetLength::Keeping::End});
    const Result shapes = evaluate(doc);
    const std::optional<session::LengthSetting> image = session::lengthSetBy(doc, shapes, panel, fold.image(rect.top()));
    EXPECT((image && image->operation == OpId(top)), "the mirrored top is as long as the drawn one, and it is the same setting");
}
