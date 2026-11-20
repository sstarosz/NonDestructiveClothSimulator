// Unit tests of Text.cpp: formulas as text.

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

TEST_CASE("formulas round-trip through text")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    doc.add(Parameter{.name = "ease", .value = 2_cm});

    const auto roundTrip = [&](const char* text, double expected, const char* shown) {
        const std::variant<Expr, TextError> parsed = parseExpr(text, doc);
        EXPECT((std::holds_alternative<Expr>(parsed)), text);
        if (!std::holds_alternative<Expr>(parsed)) return;
        const Expr expr = std::get<Expr>(parsed);
        EXPECT((formatExpr(expr, doc) == shown), shown);
        Param probe = doc.add(Parameter{.name = "probe", .value = expr});
        EXPECT_NEAR((evaluate(doc)[probe]), (expected), text);
        doc.remove(probe);
    };
    roundTrip("70", 70.0, "70");
    roundTrip("waist/4+1", 18.5, "waist / 4 + 1");
    roundTrip("(waist + ease) * 2", 144.0, "(waist + ease) * 2");
    roundTrip("waist - (ease - 1)", 69.0, "waist - (ease - 1)");
    roundTrip("waist / (2 * pi)", 70.0 / (2.0 * pi), "waist / (2 * 3.141592654)");
    roundTrip("-waist + 5mm", -69.5, "-waist + 0.5");
    roundTrip("min(waist, 60cm) + max(1, ease) + abs(-3)", 65.0, "min(waist, 60) + max(1, ease) + abs(-3)");
    roundTrip("clamp(waist, 0, 50)", 50.0, "min(max(waist, 0), 50)");
    roundTrip("360 deg * 0.5", 180.0, "360 * 0.5");
    EXPECT((formatExpr(waist / 4 + 1_cm, doc) == "waist / 4 + 1"), "formulas built in code format the same way");
}

TEST_CASE("bad formula text is reported with a position")
{
    Document doc;
    doc.add(Parameter{.name = "waist", .value = 70_cm});

    const auto problemAt = [&](const char* text, std::size_t position) {
        const std::variant<Expr, TextError> parsed = parseExpr(text, doc);
        EXPECT((std::holds_alternative<TextError>(parsed)), text);
        if (const TextError* error = std::get_if<TextError>(&parsed)) {
            EXPECT((error->position == position), text);
            EXPECT((!error->message.empty()), "has a message");
        }
    };
    problemAt("", 0);
    problemAt("waist +", 7);
    problemAt("wiast / 2", 0);
    problemAt("(waist", 6);
    problemAt("waist 4", 6);
    problemAt("sqrt(4)", 0);
    problemAt("min(1)", 5);
}

TEST_CASE("values are shown with their units and read back")
{
    EXPECT((formatValue(26.0, Unit::Length) == "26 cm"), "a length in centimetres, without needless decimals");
    EXPECT((formatValue(15.524, Unit::Length) == "15.52 cm"), "to a tenth of a millimetre");
    EXPECT((formatValue(25.0, Unit::Angle) == "25°"), "an angle in degrees");
    EXPECT((formatValue(2.0 / 3.0, Unit::Fraction) == "66.7 %"), "a fraction in per cent");
    EXPECT((formatValue(0.5, Unit::None) == "0.5"), "a plain number as it is");
    EXPECT((formatValue(-0.0001, Unit::Length) == "0 cm"), "never minus nothing");

    Document doc;
    const auto read = [&](std::string_view text) {
        const auto parsed = parseExpr(text, doc);
        return std::holds_alternative<Expr>(parsed) ? asNumber(std::get<Expr>(parsed)).value_or(-1.0) : -1.0;
    };
    EXPECT_NEAR((read("66.7 %")), (0.667), "per cent is read as a fraction");
    EXPECT_NEAR((read("25°")), (25.0), "the degree sign is read");
    EXPECT_NEAR((read(formatValue(15.524, Unit::Length))), (15.52), "what is shown reads back");
}

TEST_CASE("what is typed into a field is read in the field's unit")
{
    Document doc;
    const Param chest = doc.add(Parameter{.name = "chest", .value = 96_cm});
    const auto number = [&](std::string_view text, Unit unit) {
        const auto read = parseValue(text, unit, doc);
        return std::holds_alternative<Expr>(read) ? asNumber(std::get<Expr>(read)).value_or(-1.0) : -1.0;
    };
    EXPECT_NEAR((number("70", Unit::Length)), (70.0), "a bare number in a length is centimetres");
    EXPECT_NEAR((number("70", Unit::Fraction)), (0.7), "a bare number in a fraction is per cent");
    EXPECT_NEAR((number("70 %", Unit::Fraction)), (0.7), "and so is one with its sign");
    EXPECT_NEAR((number("0.7 * 1", Unit::Fraction)), (-1.0), "a formula is not a number");
    const auto formula = parseValue("= chest / 4", Unit::Length, doc);
    EXPECT((std::holds_alternative<Expr>(formula) && formatExpr(std::get<Expr>(formula), doc) == "chest / 4"), "a formula may start with = , as it is shown");
    const auto wrong = parseValue("= chest +", Unit::Length, doc);
    EXPECT((std::holds_alternative<TextError>(wrong) && std::get<TextError>(wrong).position >= 2), "a problem points into what was typed");
    (void)chest;
}
