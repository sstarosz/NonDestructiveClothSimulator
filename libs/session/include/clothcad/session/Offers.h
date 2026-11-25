#pragma once
// The operations a panel can be given from a list rather than with a tool,
// and what each starts as. Shared by both windows: a shape for an empty
// panel, then changes to it. One that needs an edge, corner or line says
// which to choose next on the table.
//
//   for (const session::Offered& offer : session::offersFor(doc, panel)) menu.add(offer.name, offer.hint);
//   const session::Added added = session::addOffered(doc, panel, session::Offer::Curve);
//   if (added.toChoose) startPicking(added.operation, *added.toChoose);

#include <optional>
#include <vector>

#include "clothcad/core/Document.h"

namespace clothcad::session {

enum class Offer {
    Rectangle, Circle, RingSector, Instance,  // shapes: for an empty panel
    Curve, FreeCurve, Line, AngledLine, Cut, Mirror, AdjustPoint, AddPoint, Scale, SetLength,  // changes to a shape
};

struct Offered {
    Offer offer;
    const char* name;
    const char* hint;
    bool starting;  // makes the outline, so it is offered for an empty panel only
};

// What the list offers for this panel now: shapes when it has no operation yet, else changes.
std::vector<Offered> offersFor(const Document& doc, PanelId panel);

struct Added {
    OpId operation;
    std::optional<ReferenceId> toChoose;  // an edge, corner or line it needs and has not got yet
};
// Adds the offered operation at the end of the panel's operations.
Added addOffered(Document& doc, PanelId panel, Offer offer);

} // namespace clothcad::session
