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
