#pragma once
// Internal: geometry on outlines. Included only by core/*.cpp.
//
// Every function that changes a Pattern either succeeds or returns a
// Problem and may leave the pattern half-changed, so callers work on a copy.

#include <cstddef>
#include <optional>
#include <string>

#include "core/Pattern.h"

namespace clothcad::outline {

using Problem = std::optional<std::string>;  // empty: it worked

// The point a fraction `t` of the way along an edge: 0 is its start, 1 its end.
Vec2 pointAt(const Pattern& pattern, const Edge& edge, double t);

// The place on the outline nearest to a point: which edge (its index in
// pattern.edges) and how far along it, from 0 to 1. Empty for an empty outline.
struct Nearest {
    std::size_t edge = 0;
    double along = 0.0;
};
std::optional<Nearest> nearest(const Pattern& pattern, Vec2 point);

// An edge's length along its curve.
double lengthOf(const Pattern& pattern, const Edge& edge);

// The arc through a and b that rises `bulge` above the straight line between
// them; positive bulges to the right of the direction a -> b, which is
// outward on a counter-clockwise outline. Empty when bulge is zero.
std::optional<Arc> arcFromBulge(Vec2 a, Vec2 b, double bulge);

// The arc from a to b that turns through `sweep` degrees (positive is
// counter-clockwise). Empty when a and b coincide or the sweep is not a
// proper arc (zero, or a whole turn).
std::optional<Arc> arcFromSweep(Vec2 a, Vec2 b, double sweep);

// The first point where a ray leaving `origin` meets the outline again.
std::optional<Vec2> firstHit(const Pattern& pattern, Vec2 origin, double angleDegrees);

// Cuts along the straight line from -> to (both on the outline) and keeps
// the part on the left of that direction, or on the right.
Problem cut(Pattern& pattern, Vec2 from, Vec2 to, bool keepLeft,
            const EdgeRef& cutEdge, const CornerRef& startCorner, const CornerRef& endCorner);

// Reflects the outline across one of its straight edges and joins the halves.
Problem mirror(Pattern& pattern, const EdgeRef& axis, OpId mirrorOp);

// Flips the whole pattern left-to-right about the vertical axis.
void flip(Pattern& pattern);

} // namespace clothcad::outline
