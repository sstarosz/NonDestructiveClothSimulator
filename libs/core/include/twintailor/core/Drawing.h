#pragma once
// A computed outline turned into things a view can draw or hit-test.

#include <optional>
#include <vector>

#include "twintailor/core/Math.h"
#include "twintailor/core/Pattern.h"
#include "twintailor/core/Units.h"

namespace twintailor {

// The outline as a closed polygon, in order, with arcs broken into straight
// pieces no further than `tolerance` from the true curve. The first point is
// not repeated at the end.
std::vector<Vec2> outlinePoints(const Pattern& pattern, double tolerance = 0.01_cm);

// One edge as points from its start to its end, both included.
std::vector<Vec2> edgePoints(const Pattern& pattern, const Edge& edge, double tolerance = 0.01_cm);

// The point halfway along an edge, on the edge itself.
Vec2 midpoint(const Pattern& pattern, const Edge& edge);

// The smallest axis-aligned box around the outline; empty if there is none.
std::optional<Bounds> bounds(const Pattern& pattern);

// The point on the outline nearest to `point`: on which edge, how far along
// it (0 at its start, 1 at its end), and where that is. Empty for an empty
// outline. A view uses it to snap a click to the outline.
struct OutlinePoint {
    EdgeRef edge;
    double along = 0.0;
    Vec2 position;
};
std::optional<OutlinePoint> nearestOnOutline(const Pattern& pattern, Vec2 point);

// How far along one edge the point of it nearest to `point` is, by length: 0
// at its start, 1 at its end. A view uses it to tell which end of an edge was
// clicked.
double fractionAlong(const Pattern& pattern, const Edge& edge, Vec2 point);

// The pattern turned `degrees` counter-clockwise about its origin: every
// position moves, names and lengths stay. For placing a panel on the table.
Pattern turned(const Pattern& pattern, double degrees);

// A free curve that follows an arc edge as closely as one cubic can (exact
// enough up to about a quarter turn, rough for more). Empty for an edge
// that is not an arc, or whose arc is a whole turn.
std::optional<CubicCurve> cubicFromArc(const Pattern& pattern, const Edge& edge);

// Whether a point lies inside the outline.
bool encloses(const Pattern& pattern, Vec2 point);

} // namespace twintailor
