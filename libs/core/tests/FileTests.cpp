// Unit tests of File.cpp: documents as text and as files.

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

TEST_CASE("documents round-trip through text")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist \"low\"", .value = 80});
    Param ease = doc.add(Parameter{.name = "ease", .value = max(waist / 20, 2) - abs(-waist) * 0});
    PanelId half = doc.add(Panel{.name = "half", .x = 12.5, .y = -3, .angle = 30});
    OpRef<Rect> rect = doc.add(half, Rect{.width = waist / 4 + ease, .height = 60_cm});
    OpRef<Bezier> curve = doc.add(half, Bezier{.edge = rect.top(), .startHandle = {5, -7.5}, .endHandle = {-5, 3}});
    doc.add(half, AdjustPoint{.corner = rect.bottomRight(), .dx = 4_cm, .dy = 0_cm});
    const OpRef<AddPoint> point = doc.add(half, AddPoint{.at = {rect.bottom(), waist / 200}});
    OpRef<LineBetween> line = doc.add(half, LineBetween{.from = {rect.left(), 0.5}, .to = {rect.right(), 0.25}});
    OpRef<Cut> cut = doc.add(half, Cut{.along = line.line(), .keep = Cut::Side::Right});
    OpRef<Mirror> fold = doc.add(half, Mirror{.across = cut.farPart(rect.left())});
    doc.setEnabled(curve, false);
    doc.arrange(half, Arrangement{.placed = true, .x = 1, .y = 100, .z = 25.5, .turn = 90, .tilt = -5, .roll = 180, .wrap = 60, .anchor = Vec2{7.5, 58.25}});
    PanelId rest = doc.add(Panel{.name = "rest", .construction = true});
    doc.add(rest, Offcut{.of = cut});
    PanelId twin = doc.add(Panel{.name = "twin"});
    doc.add(twin, Instance{.source = half, .mirrored = true});
    PanelId ring = doc.add(Panel{.name = "ring"});
    OpRef<RingSector> sector = doc.add(ring, RingSector{.innerLength = waist, .sweep = 180_deg, .depth = 50_cm});
    doc.add(Seam{.a = {half, fold.image(rect.top())}, .b = {ring, sector.inner()}, .reversed = true});

    const std::string text = toText(doc);
    std::variant<Document, FileError> read = fromText(text);
    EXPECT((std::holds_alternative<Document>(read)), "the text reads back");
    if (!std::holds_alternative<Document>(read)) return;
    Document& again = std::get<Document>(read);
    EXPECT((toText(again) == text), "and writes the same text again");
    const Result before = evaluate(doc), after = evaluate(again);
    EXPECT((before.errors.size() == after.errors.size() && before.seams.size() == after.seams.size()), "same problems, same seams");
    EXPECT_NEAR((after[half].length(rect.bottom())), (before[half].length(rect.bottom())), "same shapes");
    EXPECT((again.get(point).at.edge == rect.bottom() && toText(again).find("AddPoint") != std::string::npos), "an added point survives");
    EXPECT((again.get(waist).name == "waist \"low\"" && again.get(half).angle == 30.0 && !again.enabled(curve)), "names, placement and switched-off steps survive");
    EXPECT((again.get(half).arrangement == doc.get(half).arrangement && !again.get(rest).arrangement.placed), "arrangement in space survives, with its anchor");
    {
        // A file from before 0.1.1: an arrangement without an anchor.
        std::string old = text;
        const std::size_t at = old.find(" 7.5 58.25\n");
        REQUIRE(at != std::string::npos);
        old.erase(at, std::string(" 7.5 58.25").size());
        const std::variant<Document, FileError> readOld = fromText(old);
        REQUIRE(std::holds_alternative<Document>(readOld));
        const Arrangement oldOne = std::get<Document>(readOld).get(half).arrangement;
        EXPECT((oldOne.placed && oldOne.wrap == 60.0 && !oldOne.anchor), "an older file without an anchor still reads, anchored in the middle");
    }
    // New things made after reading do not collide with what was read.
    const PanelId added = again.add(Panel{.name = "new"});
    EXPECT((!doc.contains(added)), "ids continue after the highest one read");

    const auto bad = [](const char* file) { return std::holds_alternative<FileError>(fromText(file)); };
    EXPECT((bad("")), "an empty file is refused");
    EXPECT((bad("hello")), "so is another kind of file");
    EXPECT((bad("clothcad 2\n")), "and a newer version");
    std::variant<Document, FileError> broken = fromText("clothcad 1\npanel 1 \"a\" 0 0 0 0\nop 2 Rect 1 2 n 5 oops 0 0\n");
    EXPECT((std::holds_alternative<FileError>(broken) && std::get<FileError>(broken).line == 3), "a bad line is reported by number");
    EXPECT((bad("clothcad 1\npanel 1 \"a\" 0 0 0 0\npanel 1 \"b\" 0 0 0 0\n")), "an id used twice is refused");
}

TEST_CASE("a parameter's unit is saved, and a length needs none")
{
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const Param angle = doc.add(Parameter{.name = "hem_angle", .value = 25_deg, .unit = Unit::Angle});
    const Param fullness = doc.add(Parameter{.name = "fullness", .value = 0.5, .unit = Unit::None});
    const std::string text = toText(doc);
    EXPECT((text.find("unit length") == std::string::npos && text.find("unit angle") != std::string::npos), "only a unit that is not a length is written");
    const std::variant<Document, FileError> read = fromText(text);
    REQUIRE(std::holds_alternative<Document>(read));
    const Document& again = std::get<Document>(read);
    EXPECT((again.get(waist).unit == Unit::Length && again.get(angle).unit == Unit::Angle && again.get(fullness).unit == Unit::None), "every unit reads back");
    std::string wrong = text;
    wrong.replace(wrong.find("unit angle"), 10, "unit furlong");
    EXPECT((std::holds_alternative<FileError>(fromText(wrong))), "an unknown unit is a problem in words, not a guess");
}
