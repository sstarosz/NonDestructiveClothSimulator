#pragma once
// Formulas as text, for the places where a person types or reads one: the
// window's fields, and later files. The rest of the core never uses text.
//
//   parseExpr("waist / 4 + 1", doc)   ->  an Expr referring to doc's "waist"
//   formatExpr(expr, doc)             ->  "waist / 4 + 1"
//
// Syntax: numbers (optionally followed by cm, mm or deg), parameter names,
// + - * /, parentheses, pi, and min(a, b), max(a, b), abs(a), clamp(v, lo, hi).
// A parameter can be typed if its name is letters, digits and underscores.

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include "clothcad/core/Document.h"

namespace clothcad {

struct TextError {
    std::size_t position = 0;  // index into the text where the problem is
    std::string message;
};

// Whether a parameter with this name can be typed in a formula: letters,
// digits and underscores, not starting with a digit, and not a word the
// formulas already use (pi, min, max, abs, clamp, cm, mm, deg).
bool canBeTypedInFormulas(std::string_view name);

std::variant<Expr, TextError> parseExpr(std::string_view text, const Document& doc);
std::string formatExpr(const Expr& expr, const Document& doc);

} // namespace clothcad
