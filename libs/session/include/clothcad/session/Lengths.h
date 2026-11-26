#pragma once
// The lengths of a panel's edges, as the table labels them: the number, and
// the formula that sets it when one does (shown in the formula colour). A
// length typed as a plain number, or changed by a later operation (a cut),
// has no formula.
//
//   for (const session::EdgeLength& l : session::lengthsOf(doc, shapes, panel))
//       label(l.edge, l.length, l.formula);
//
// And what sets an edge's length, so that it can be typed:
//
//   if (std::optional<session::LengthSetting> s = session::lengthSetBy(doc, shapes, panel, edge))
//       typeInto(s->operation, s->field);

#include <optional>
#include <string>
#include <vector>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/core/Fields.h"

namespace clothcad::session {

struct EdgeLength {
    EdgeRef edge;
    double length = 0.0;  // cm
    std::string formula;  // empty: not set by a formula
    friend bool operator==(const EdgeLength&, const EdgeLength&) = default;
};

// The field that is an edge's length: a SetLength's, or a setting of the
// operation that made the edge (a rectangle's width).
struct LengthSetting {
    OpId operation;
    FieldId field;
};

// What sets the length of `edge` of `panel` now, if anything does. A SetLength
// of the edge wins, the last one of them; else a setting of the shape itself.
// Only a setting that still gives the edge its length counts: when a later
// step (a cut, a dragged corner) changed the edge, nothing sets it.
std::optional<LengthSetting> lengthSetBy(const Document& doc, const Result& shapes, PanelId panel, const EdgeRef& edge);

// Every edge of the panel's outline, except a ring's closing slit.
std::vector<EdgeLength> lengthsOf(const Document& doc, const Result& shapes, PanelId panel);

} // namespace clothcad::session
