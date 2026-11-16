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
