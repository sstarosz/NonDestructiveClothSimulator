#pragma once
// Document in, geometry out. Recomputes everything on every call:
// no cache, no dirty flags, no threads.
//
//   Result shapes = evaluate(doc);
//   shapes[skirt].length(skirtRect.top());   // a panel's outline
//   shapes[waist];                           // a parameter's value

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "twintailor/core/Document.h"
#include "twintailor/core/Pattern.h"

namespace twintailor {

// Something that could not be computed, and where: a parameter (bad or
// circular formula) or an operation.
struct Error {
    std::variant<Param, OpId> where;
    std::string message;
};

struct ParameterValue {
    Param parameter;
    double value = 0.0;
};

struct PanelResult {
    PanelId panel;
    Pattern pattern;  // result of the last operation that succeeded
};

// A seam as it stands now. A seam is broken, not an error, when one of its
// edges no longer exists: the shapes are all still fine, and the seam comes
// back when the edge does.
struct SeamResult {
    SeamId seam;
    std::string problem;   // empty when both edges exist
    double lengthA = 0.0;  // the two edges' lengths, when they exist
    double lengthB = 0.0;

    bool sewn() const { return problem.empty(); }
};

struct Result {
    std::vector<ParameterValue> values;  // parameters that have a value
    std::vector<PanelResult> panels;     // every panel, in document order
    std::vector<Error> errors;
    std::vector<SeamResult> seams;       // every seam, in document order

    bool ok() const { return errors.empty(); }
    bool failed(Param parameter) const;
    bool failed(OpId operation) const;

    double operator[](Param parameter) const;        // throws std::out_of_range if it has no value
    const Pattern& operator[](PanelId panel) const;  // throws std::out_of_range if unknown
    const SeamResult& operator[](SeamId seam) const;  // throws std::out_of_range if unknown
};

// Never throws: problems are reported in Result::errors.
Result evaluate(const Document& doc);

// A panel's outline as it is just before one of its operations runs: what
// that operation gets to work on. A view uses it to offer the edges and
// corners the operation could refer to. Empty if the operation is unknown.
Pattern outlineBefore(const Document& doc, OpId operation);

// The whole document as it was after one operation: the operations below
// `last` in its panel count as switched off, except mirrors that are applied
// last. A view uses it to step back through how a panel was made.
Result evaluateUpTo(const Document& doc, OpId last);

// What a formula comes to, given a result's parameter values. Empty if it
// uses a parameter that has no value, or divides by zero.
std::optional<double> valueOf(const Expr& expr, const Result& result);

// Replaces every formula in a panel's operations with the number it comes to
// now, so the panel no longer depends on any parameter. Returns how many
// fields were replaced. A formula that has no value (an error) is left alone.
std::size_t bake(Document& doc, PanelId panel);

} // namespace twintailor
