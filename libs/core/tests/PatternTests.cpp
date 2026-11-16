// Unit tests of Pattern.cpp: looking up edges, corners and lines of an outline by name.

#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"

using namespace clothcad;

TEST_CASE("an outline is looked up by the names of its edges and corners")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 40_cm});
    OpRef<Line> line = doc.add(panel, Line{.from = rect.bottomLeft(), .angle = 45_deg});
    const Result result = evaluate(doc);
    const Pattern& shape = result[panel];

    EXPECT((shape.contains(rect.top()) && shape.contains(rect.topRight()) && shape.contains(line.line())), "it has what was made");
    EXPECT_NEAR((shape.length(rect.top())), (30.0), "an edge's length");
    EXPECT_NEAR((shape.position(rect.topRight()).x), (30.0), "a corner's place, across");
    EXPECT_NEAR((shape.position(rect.topRight()).y), (40.0), "and up");
    EXPECT_NEAR((shape.line(line.line()).end.y), (30.0), "a line ends where it meets the outline");
}

TEST_CASE("asking an outline for something it does not have throws")
{
    Document doc;
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 30_cm, .height = 40_cm});
    const Result result = evaluate(doc);
    const Pattern& shape = result[panel];
    const EdgeRef missing{rect.id, 99};

    EXPECT((!shape.contains(missing)), "it says it does not have it");
    EXPECT_THROWS([&] { (void)shape.edge(missing); }, std::out_of_range, "an unknown edge throws");
    EXPECT_THROWS([&] { (void)shape.length(missing); }, std::out_of_range, "so does its length");
    EXPECT_THROWS([&] { (void)shape.position(CornerRef{rect.id, 99}); }, std::out_of_range, "and an unknown corner");
}
