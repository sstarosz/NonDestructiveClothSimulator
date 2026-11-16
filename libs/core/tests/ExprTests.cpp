// Unit tests of Expr.cpp: formulas: numbers, parameters and operators.

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

TEST_CASE("formulas are computed")
{
    Document doc;
    Param a = doc.add(Parameter{.name = "a", .value = 10});
    Param b = doc.add(Parameter{.name = "b", .value = a / 4 + 1});
    Param c = doc.add(Parameter{.name = "c", .value = -a * 2 - b});
    Param d = doc.add(Parameter{.name = "d", .value = min(a, b) + max(a, b) + abs(c)});
    Param e = doc.add(Parameter{.name = "e", .value = clamp(a, 0, 5) + clamp(a, 20, 30)});

    Result result = evaluate(doc);
    EXPECT((result.ok()), "no errors");
    EXPECT_NEAR((result[b]), (3.5), "a / 4 + 1");
    EXPECT_NEAR((result[c]), (-23.5), "-a * 2 - b");
    EXPECT_NEAR((result[d]), (37.0), "min + max + abs");
    EXPECT_NEAR((result[e]), (25.0), "clamp");
}

TEST_CASE("unit literals are centimetres")
{
    EXPECT_NEAR((70_cm), (70.0), "cm");
    EXPECT_NEAR((5_mm), (0.5), "mm");
    EXPECT_NEAR((2.5_cm + 5_mm), (3.0), "mixed");
}

TEST_CASE("negative numbers stay numbers")
{
    EXPECT((asNumber(-Expr(7.5)).has_value() && *asNumber(-Expr(7.5)) == -7.5), "-7.5 is a plain number");
    const std::variant<Expr, TextError> parsed = parseExpr("-7.5", Document{});
    const Expr* expr = std::get_if<Expr>(&parsed);
    EXPECT((expr && asNumber(*expr) && *asNumber(*expr) == -7.5), "typed -7.5 is a plain number");
}
