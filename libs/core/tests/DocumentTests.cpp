// Unit tests of Document.cpp: the document's verbs and reading.

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

TEST_CASE("copy keeps expressions and is otherwise independent")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId band = doc.add(Panel{.name = "waistband"});
    OpRef<Rect> bandRect = doc.add(band, Rect{.width = waist, .height = 4_cm});

    PanelCopy skirt = doc.copy(band, Panel{.name = "skirt"});
    OpRef<Rect> skirtRect = skirt[bandRect];
    doc.set(skirtRect, &Rect::height, 60_cm);

    EXPECT((!(OpId(skirtRect) == OpId(bandRect))), "the copy has its own operation");
    EXPECT((doc.get(skirt).name == "skirt"), "the copy has its own name");

    doc.set(waist, 80_cm);
    Result result = evaluate(doc);
    EXPECT_NEAR((result[band].length(bandRect.top())), (80.0), "original follows waist");
    EXPECT_NEAR((result[skirt].length(skirtRect.top())), (80.0), "copy follows waist");
    EXPECT_NEAR((result[band].length(bandRect.left())), (4.0), "original keeps its height");
    EXPECT_NEAR((result[skirt].length(skirtRect.left())), (60.0), "copy has its own height");
    EXPECT_THROWS([&] { (void)skirt[skirtRect]; }, std::out_of_range, "only originals can be looked up in a copy");
}

TEST_CASE("document copy is an independent snapshot")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = waist, .height = 4_cm});

    const Document snapshot = doc;  // what undo stores
    doc.set(waist, 80_cm);
    doc.rename(panel, "renamed");

    EXPECT_NEAR((evaluate(snapshot)[panel].length(rect.top())), (70.0), "snapshot keeps the old value");
    EXPECT_NEAR((evaluate(doc)[panel].length(rect.top())), (80.0), "document has the new value");
    EXPECT((snapshot.get(panel).name == "panel"), "snapshot keeps the old name");
}

TEST_CASE("remove, rename and move")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    OpRef<Rect> r1 = doc.add(a, Rect{.width = 1_cm, .height = 1_cm});
    OpRef<Rect> r2 = doc.add(a, Rect{.width = 2_cm, .height = 2_cm});
    OpRef<Rect> r3 = doc.add(a, Rect{.width = 3_cm, .height = 3_cm});

    doc.rename(waist, "waistline");
    EXPECT((doc.get(waist).name == "waistline"), "parameter renamed");

    doc.move(r3, 0);
    EXPECT((doc.operations(a) == std::vector<OpId>{r3, r1, r2}), "moved to the front");
    doc.move(r3, 2);
    EXPECT((doc.operations(a) == std::vector<OpId>{r1, r2, r3}), "moved to the back");

    doc.remove(r2);
    EXPECT((doc.operations(a) == std::vector<OpId>{r1, r3}), "operation removed");
    EXPECT((!doc.contains(OpId(r2))), "removed operation is gone");

    doc.remove(b);
    EXPECT((doc.panels() == std::vector<PanelId>{a}), "panel removed");
}

TEST_CASE("unknown handles throw")
{
    Document doc;
    Document other;
    Param foreign = other.add(Parameter{.name = "x", .value = 1});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 1_cm, .height = 1_cm});
    doc.remove(panel);

    EXPECT_THROWS([&] { doc.set(foreign, 2); }, std::out_of_range, "set unknown parameter");
    EXPECT_THROWS([&] { (void)doc.get(panel); }, std::out_of_range, "get removed panel");
    EXPECT_THROWS([&] { doc.add(panel, Rect{}); }, std::out_of_range, "add to removed panel");
    EXPECT_THROWS([&] { doc.set(rect, &Rect::width, 2_cm); }, std::out_of_range, "set on removed operation");
    EXPECT_THROWS([&] { doc.remove(OpId(rect)); }, std::out_of_range, "remove removed operation");
}
