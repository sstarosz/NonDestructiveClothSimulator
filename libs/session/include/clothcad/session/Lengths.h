#pragma once
// The lengths of a panel's edges, as the table labels them: the number, and
// the formula that sets it when one does (shown in the formula colour). A
// length typed as a plain number, or changed by a later operation (a cut),
// has no formula.
//
//   for (const session::EdgeLength& l : session::lengthsOf(doc, shapes, panel))
//       label(l.edge, l.length, l.formula);

#include <string>
#include <vector>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"

namespace clothcad::session {

struct EdgeLength {
    EdgeRef edge;
    double length = 0.0;  // cm
    std::string formula;  // empty: not set by a formula
};

// Every edge of the panel's outline, except a ring's closing slit.
std::vector<EdgeLength> lengthsOf(const Document& doc, const Result& shapes, PanelId panel);

} // namespace clothcad::session
