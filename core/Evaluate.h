#pragma once
// Document in, geometry out. Recomputes everything on every call:
// no cache, no dirty flags, no threads.
//
//   Result shapes = evaluate(doc);
//   shapes[skirt].length(skirtRect.top());   // a panel's outline
//   shapes[waist];                           // a parameter's value

#include <string>
#include <variant>
#include <vector>

#include "core/Document.h"
#include "core/Pattern.h"

namespace clothcad {

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

struct Result {
    std::vector<ParameterValue> values;  // parameters that have a value
    std::vector<PanelResult> panels;     // every panel, in document order
    std::vector<Error> errors;

    bool ok() const { return errors.empty(); }
    bool failed(Param parameter) const;
    bool failed(OpId operation) const;

    double operator[](Param parameter) const;        // throws std::out_of_range if it has no value
    const Pattern& operator[](PanelId panel) const;  // throws std::out_of_range if unknown
};

// Never throws: problems are reported in Result::errors.
Result evaluate(const Document& doc);

} // namespace clothcad
