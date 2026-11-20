// Unit tests of References.cpp: references kept pointing into a copy.

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

TEST_CASE("copy points references at the copy")
{
    Document doc;
    PanelId original = doc.add(Panel{.name = "original"});
    OpRef<Rect> rect = doc.add(original, Rect{.width = 10_cm, .height = 20_cm});
    OpRef<Curve> curve = doc.add(original, Curve{.edge = rect.top(), .bulge = 2_cm});

    PanelCopy copy = doc.copy(original, Panel{.name = "copy"});
    OpRef<Rect> copyRect = copy[rect];
    OpRef<Curve> copyCurve = copy[curve];

    EXPECT((doc.get(copyCurve).edge == copyRect.top()), "the copied curve bends the copied rectangle");
    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT((result[copy].edge(copyRect.top()).arc.has_value()), "the copy is curved");
}

TEST_CASE("a copied point is on the copy's edge, and so is its after-part")
{
    Document doc;
    const PanelId original = doc.add(Panel{.name = "original"});
    const OpRef<Rect> rect = doc.add(original, Rect{.width = 10_cm, .height = 20_cm});
    const OpRef<AddPoint> point = doc.add(original, AddPoint{.at = {rect.top(), 0.5}});
    const OpRef<Curve> curve = doc.add(original, Curve{.edge = point.after(rect.top()), .bulge = 1_cm});

    PanelCopy copy = doc.copy(original, Panel{.name = "copy"});
    const OpRef<Rect> copyRect = copy[rect];
    const OpRef<AddPoint> copyPoint = copy[point];
    EXPECT((doc.get(copyPoint).at.edge == copyRect.top()), "the copied point splits the copy's top");
    EXPECT((doc.get(copy[curve]).edge == copyPoint.after(copyRect.top())), "and what refers to its after-part follows it to the copy");
    const Result result = evaluate(doc);
    EXPECT((result.ok() && result[copy].edge(copyPoint.after(copyRect.top())).arc.has_value()), "the copy's after-part is curved, as the original's is");
}
