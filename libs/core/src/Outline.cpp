#include "internal/Outline.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "twintailor/core/Units.h"

namespace twintailor::outline {

namespace {

constexpr double tolerance = 1e-7;  // centimetres
constexpr int cubicPieces = 64;     // how finely a free curve is searched

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
double norm(Vec2 a) { return std::hypot(a.x, a.y); }
double radians(double degrees) { return degrees * (pi / 180.0); }
double degrees(double radians) { return radians * (180.0 / pi); }
Vec2 between(Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }

Vec2 startOf(const Pattern& p, const Edge& e) { return p.vertices[e.start].position; }
Vec2 endOf(const Pattern& p, const Edge& e) { return p.vertices[e.end].position; }

// ---- Free curves: a cubic Bezier through four points -------------------------

struct Cubic {
    Vec2 p0, p1, p2, p3;

    Vec2 at(double t) const {
        const double u = 1.0 - t;
        return p0 * (u * u * u) + p1 * (3.0 * u * u * t) + p2 * (3.0 * u * t * t) + p3 * (t * t * t);
    }
    Vec2 speed(double t) const {
        const double u = 1.0 - t;
        return (p1 - p0) * (3.0 * u * u) + (p2 - p1) * (6.0 * u * t) + (p3 - p2) * (3.0 * t * t);
    }
};

Cubic cubicOf(const Pattern& p, const Edge& e) { return {startOf(p, e), e.cubic->control1, e.cubic->control2, endOf(p, e)}; }

// ---- Arcs ---------------------------------------------------------------------

// Where on an arc a point at `angle` lies: 0 at the start, 1 at the end,
// more than 1 when it is past the end.
double arcParameter(const Arc& arc, Vec2 start, double angle) {
    const double startAngle = std::atan2(start.y - arc.center.y, start.x - arc.center.x);
    const double direction = arc.sweep >= 0.0 ? 1.0 : -1.0;
    double delta = std::fmod((angle - startAngle) * direction, 2.0 * pi);
    if (delta < 0.0) delta += 2.0 * pi;
    if (delta > 2.0 * pi - 1e-12) delta = 0.0;
    return delta / std::abs(radians(arc.sweep));
}

// ---- Rays and locations ---------------------------------------------------------

// Distances along a ray (origin + direction * s) at which it meets an edge.
std::vector<double> rayHits(const Pattern& p, const Edge& e, Vec2 origin, Vec2 direction) {
    std::vector<double> hits;
    const Vec2 a = startOf(p, e);
    if (e.cubic) {
        // Which side of the ray's line the curve is on changes sign at a
        // crossing; each piece that changes sign is narrowed down by halving.
        const Cubic curve = cubicOf(p, e);
        const auto side = [&](double t) { return cross(curve.at(t) - origin, direction); };
        double t0 = 0.0, side0 = side(0.0);
        for (int i = 1; i <= cubicPieces; ++i) {
            const double t1 = static_cast<double>(i) / cubicPieces, side1 = side(t1);
            if ((side0 <= 0.0) != (side1 <= 0.0)) {
                double low = t0, high = t1;
                const bool lowIsNegative = side0 <= 0.0;
                for (int step = 0; step < 60; ++step) {
                    const double middle = (low + high) / 2.0;
                    ((side(middle) <= 0.0) == lowIsNegative ? low : high) = middle;
                }
                hits.push_back(dot(curve.at((low + high) / 2.0) - origin, direction));
            }
            t0 = t1;
            side0 = side1;
        }
        return hits;
    }
    if (e.arc) {
        const Arc& arc = *e.arc;
        const Vec2 f = origin - arc.center;
        const double b = dot(f, direction);
        const double discriminant = b * b - (dot(f, f) - arc.radius * arc.radius);
        if (discriminant < 0.0) return hits;
        const double root = std::sqrt(discriminant);
        for (double s : {-b - root, -b + root}) {
            const Vec2 point = origin + direction * s;
            const double angle = std::atan2(point.y - arc.center.y, point.x - arc.center.x);
            const double slack = tolerance / std::max(arc.radius * std::abs(radians(arc.sweep)), tolerance);
            if (arcParameter(arc, a, angle) <= 1.0 + slack) hits.push_back(s);
        }
        return hits;
    }
    const Vec2 along = endOf(p, e) - a;
    const double denominator = cross(direction, along);
    if (std::abs(denominator) < 1e-12) return hits;  // parallel
    const double s = cross(a - origin, along) / denominator;
    const double t = cross(a - origin, direction) / denominator;
    const double slack = tolerance / std::max(norm(along), tolerance);
    if (t >= -slack && t <= 1.0 + slack) hits.push_back(s);
    return hits;
}

struct Location {
    std::size_t edge = 0;
    double t = 0.0;
};

// The place on one edge nearest to `point`.
double nearestParameter(const Pattern& p, const Edge& e, Vec2 point) {
    const Vec2 a = startOf(p, e);
    if (e.cubic) {
        const Cubic curve = cubicOf(p, e);
        const auto distance = [&](double t) { return norm(point - curve.at(t)); };
        int best = 0;
        for (int i = 1; i <= cubicPieces; ++i)
            if (distance(static_cast<double>(i) / cubicPieces) < distance(static_cast<double>(best) / cubicPieces)) best = i;
        // Narrow down between the neighbouring samples.
        double low = std::max(best - 1, 0) / static_cast<double>(cubicPieces);
        double high = std::min(best + 1, cubicPieces) / static_cast<double>(cubicPieces);
        for (int step = 0; step < 80; ++step) {
            const double third = (high - low) / 3.0;
            if (distance(low + third) < distance(high - third)) high -= third;
            else low += third;
        }
        return (low + high) / 2.0;
    }
    if (e.arc) {
        const double angle = std::atan2(point.y - e.arc->center.y, point.x - e.arc->center.x);
        const double t = arcParameter(*e.arc, a, angle);
        return t <= 1.0 ? t : norm(point - a) < norm(point - endOf(p, e)) ? 0.0 : 1.0;
    }
    const Vec2 along = endOf(p, e) - a;
    const double lengthSquared = dot(along, along);
    return lengthSquared > 0.0 ? std::clamp(dot(point - a, along) / lengthSquared, 0.0, 1.0) : 0.0;
}

// The place on the outline nearest to `point`, if it is on the outline.
std::optional<Location> locate(const Pattern& p, Vec2 point) {
    std::optional<Location> best;
    double bestDistance = 1e-5;
    for (std::size_t i = 0; i < p.edges.size(); ++i) {
        const double t = nearestParameter(p, p.edges[i], point);
        const double distance = norm(point - pointAt(p, p.edges[i], t));
        if (distance < bestDistance) {
            bestDistance = distance;
            best = Location{i, t};
        }
    }
    return best;
}

// Makes sure there is a vertex at `where` and returns its index. Splitting
// an edge gives both halves the original name; a cut keeps only one of them.
uint32_t splitAt(Pattern& p, Location where, const CornerRef& name) {
    const Edge whole = p.edges[where.edge];
    const Vec2 point = pointAt(p, whole, where.t);
    if (norm(point - startOf(p, whole)) < tolerance) return whole.start;
    if (norm(point - endOf(p, whole)) < tolerance) return whole.end;

    Edge first = whole, second = whole;
    if (whole.arc) {
        first.arc->sweep = whole.arc->sweep * where.t;
        second.arc->sweep = whole.arc->sweep * (1.0 - where.t);
    }
    if (whole.cubic) {
        // De Casteljau: the two halves are exact pieces of the same curve.
        const Cubic c = cubicOf(p, whole);
        const Vec2 a = between(c.p0, c.p1, where.t), b = between(c.p1, c.p2, where.t), d = between(c.p2, c.p3, where.t);
        first.cubic = CubicCurve{a, between(a, b, where.t)};
        second.cubic = CubicCurve{between(b, d, where.t), d};
    }

    const uint32_t vertex = static_cast<uint32_t>(p.vertices.size());
    p.vertices.push_back({name, point});
    first.end = vertex;
    second.start = vertex;
    p.edges[where.edge] = first;
    p.edges.insert(p.edges.begin() + static_cast<std::ptrdiff_t>(where.edge) + 1, second);
    return vertex;
}

// The edges walked from vertex `begin` forward around the outline to `finish`.
std::vector<Edge> chain(const Pattern& p, uint32_t begin, uint32_t finish) {
    std::vector<Edge> edges;
    const std::size_t count = p.edges.size();
    std::size_t i = 0;
    while (i < count && p.edges[i].start != begin) ++i;
    for (std::size_t step = 0; step < count && i < count; ++step) {
        const Edge& e = p.edges[(i + step) % count];
        edges.push_back(e);
        if (e.end == finish) return edges;
    }
    return {};
}

// Drops vertices no edge uses and renumbers the rest.
void compact(Pattern& p) {
    const uint32_t unused = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> renumbered(p.vertices.size(), unused);
    std::vector<Vertex> kept;
    const auto keep = [&](uint32_t& index) {
        if (renumbered[index] == unused) {
            renumbered[index] = static_cast<uint32_t>(kept.size());
            kept.push_back(p.vertices[index]);
        }
        index = renumbered[index];
    };
    for (Edge& e : p.edges) {
        keep(e.start);
        keep(e.end);
    }
    p.vertices = std::move(kept);
}

Vec2 reflect(Vec2 point, Vec2 a, Vec2 unitAlong) {
    const Vec2 v = point - a;
    return a + unitAlong * (2.0 * dot(v, unitAlong)) - v;
}

} // namespace

Vec2 pointAt(const Pattern& p, const Edge& e, double t) {
    const Vec2 a = startOf(p, e);
    if (e.cubic) return cubicOf(p, e).at(t);
    if (!e.arc) return a + (endOf(p, e) - a) * t;
    const Arc& arc = *e.arc;
    const double angle = std::atan2(a.y - arc.center.y, a.x - arc.center.x) + radians(arc.sweep) * t;
    return arc.center + Vec2{std::cos(angle), std::sin(angle)} * arc.radius;
}

std::optional<Nearest> nearest(const Pattern& p, Vec2 point) {
    std::optional<Nearest> best;
    double bestDistance = 0.0;
    for (std::size_t i = 0; i < p.edges.size(); ++i) {
        const double t = nearestParameter(p, p.edges[i], point);
        const double distance = norm(point - pointAt(p, p.edges[i], t));
        if (!best || distance < bestDistance) {
            bestDistance = distance;
            best = Nearest{i, t};
        }
    }
    return best;
}

double lengthOf(const Pattern& p, const Edge& e) {
    if (e.cubic) {
        // Simpson's rule on the curve's speed.
        const Cubic curve = cubicOf(p, e);
        const int pieces = 2 * cubicPieces;
        double sum = norm(curve.speed(0.0)) + norm(curve.speed(1.0));
        for (int i = 1; i < pieces; ++i) sum += norm(curve.speed(static_cast<double>(i) / pieces)) * (i % 2 == 1 ? 4.0 : 2.0);
        return sum / (3.0 * pieces);
    }
    if (e.arc) return std::abs(radians(e.arc->sweep)) * e.arc->radius;
    return norm(endOf(p, e) - startOf(p, e));
}

std::optional<Arc> arcFromBulge(Vec2 a, Vec2 b, double bulge) {
    const Vec2 along = b - a;
    const double chord = norm(along);
    if (std::abs(bulge) < 1e-12 || chord < tolerance) return std::nullopt;

    // Signed radius: negative when the arc bulges to the left.
    const double radius = (chord * chord / 4.0 + bulge * bulge) / (2.0 * bulge);
    const Vec2 left{-along.y / chord, along.x / chord};
    const Vec2 middle = a + along * 0.5;
    return Arc{middle + left * (radius - bulge), std::abs(radius), degrees(4.0 * std::atan(2.0 * bulge / chord))};
}

std::optional<Arc> arcFromSweep(Vec2 a, Vec2 b, double sweep) {
    if (std::abs(sweep) < 1e-9 || std::abs(sweep) > 360.0 - 1e-6) return std::nullopt;
    const double chord = norm(b - a);
    if (chord < tolerance) return std::nullopt;
    // Counter-clockwise arcs bulge to the right of a -> b, so the bulge has the sweep's sign.
    return arcFromBulge(a, b, chord / 2.0 * std::tan(radians(sweep) / 4.0));
}

namespace {

bool wholeTurn(const Edge& e) { return e.arc && (e.start == e.end || std::abs(e.arc->sweep) > 360.0 - 1e-6); }

} // namespace

Problem moveCorners(Pattern& pattern, const std::vector<CornerMove>& moves) {
    const auto moved = [&](uint32_t vertex) {
        return std::any_of(moves.begin(), moves.end(), [&](const CornerMove& m) { return m.vertex == vertex; });
    };
    for (const Edge& e : pattern.edges)
        if (wholeTurn(e) && (moved(e.start) || moved(e.end)))
            return "the corner belongs to a whole turn of a circle, which has no ends to move; change its radius instead";
    for (const CornerMove& m : moves) pattern.vertices[m.vertex].position = pattern.vertices[m.vertex].position + m.by;
    // A free curve's handle at a corner travels with it.
    for (Edge& e : pattern.edges) {
        if (!e.cubic) continue;
        for (const CornerMove& m : moves) {
            if (e.start == m.vertex) e.cubic->control1 = e.cubic->control1 + m.by;
            if (e.end == m.vertex) e.cubic->control2 = e.cubic->control2 + m.by;
        }
    }
    // An arc keeps turning through the same angle, between the new ends. Every
    // corner is in place first, so no arc is worked out from a half-moved edge.
    for (Edge& e : pattern.edges) {
        if (!e.arc || (!moved(e.start) && !moved(e.end))) continue;
        const std::optional<Arc> arc = arcFromSweep(pattern.vertices[e.start].position, pattern.vertices[e.end].position, e.arc->sweep);
        if (!arc) return "the corner cannot move there: the curved edge would have no length";
        e.arc = arc;
    }
    return std::nullopt;
}

std::optional<double> chordFor(const Edge& edge, double length) {
    if (edge.cubic || wholeTurn(edge)) return std::nullopt;
    if (!edge.arc) return length;
    // An arc of radius R through angle a is R * a long, and its chord is 2 R sin(a / 2).
    const double angle = std::abs(radians(edge.arc->sweep));
    return 2.0 * length * std::sin(angle / 2.0) / angle;
}

std::optional<Vec2> firstHit(const Pattern& pattern, Vec2 origin, double angleDegrees) {
    const Vec2 direction{std::cos(radians(angleDegrees)), std::sin(radians(angleDegrees))};
    std::optional<double> nearest;
    for (const Edge& e : pattern.edges)
        for (double s : rayHits(pattern, e, origin, direction))
            if (s > 1e-6 && (!nearest || s < *nearest)) nearest = s;
    if (!nearest) return std::nullopt;
    return origin + direction * *nearest;
}

Problem addPoint(Pattern& pattern, const EdgeRef& edge, double along, const CornerRef& corner, const EdgeRef& after) {
    const auto found = std::find_if(pattern.edges.begin(), pattern.edges.end(), [&](const Edge& e) { return e.name == edge; });
    if (found == pattern.edges.end()) return "the edge to add a point to does not exist; an earlier operation may have removed it";
    const std::size_t index = static_cast<std::size_t>(found - pattern.edges.begin());
    const std::size_t corners = pattern.vertices.size();
    splitAt(pattern, Location{index, along}, corner);
    if (pattern.vertices.size() == corners) return "the point would be on a corner that is already there; put it further along the edge";
    pattern.edges[index + 1].name = after;
    return std::nullopt;
}

Problem cut(Pattern& pattern, Vec2 from, Vec2 to, bool keepLeft,
            const EdgeRef& cutEdge, const CornerRef& startCorner, const CornerRef& endCorner) {
    const std::optional<Location> fromPlace = locate(pattern, from);
    if (!fromPlace) return "the line no longer starts on the outline";
    const uint32_t start = splitAt(pattern, *fromPlace, startCorner);

    const std::optional<Location> toPlace = locate(pattern, to);
    if (!toPlace) return "the line no longer ends on the outline";
    const uint32_t end = splitAt(pattern, *toPlace, endCorner);

    if (start == end) return "the line is too short to cut along";

    // Walking the outline forward keeps its inside on the left, so the part
    // on the left of start -> end is the walk from end round to start.
    std::vector<Edge> kept = keepLeft ? chain(pattern, end, start) : chain(pattern, start, end);
    if (kept.empty()) return "the outline is not a closed loop";

    // A line that enters and leaves through one edge can leave both ends of
    // that edge on the kept side. The piece met second gets its own name.
    for (std::size_t i = 0; i < kept.size(); ++i)
        for (std::size_t j = i + 1; j < kept.size(); ++j)
            if (kept[i].name == kept[j].name) kept[j].name.via.push_back(cutEdge.op);

    Edge added;
    added.name = cutEdge;
    added.start = keepLeft ? start : end;
    added.end = keepLeft ? end : start;
    if (keepLeft) kept.insert(kept.begin(), added);
    else kept.push_back(added);

    pattern.edges = std::move(kept);
    compact(pattern);
    return std::nullopt;
}

Problem mirror(Pattern& pattern, const EdgeRef& axis, OpId mirrorOp) {
    const std::size_t count = pattern.edges.size();
    std::size_t axisIndex = 0;
    while (axisIndex < count && !(pattern.edges[axisIndex].name == axis)) ++axisIndex;
    if (axisIndex == count) return "the edge to mirror across does not exist";

    const Edge axisEdge = pattern.edges[axisIndex];
    if (axisEdge.arc || axisEdge.cubic) return "the edge to mirror across must be straight";
    const Vec2 a = startOf(pattern, axisEdge);
    const Vec2 along = endOf(pattern, axisEdge) - a;
    if (norm(along) < tolerance) return "the edge to mirror across has no length";
    const Vec2 unitAlong = along * (1.0 / norm(along));

    // Everything except the axis edge, walked from the axis' end to its start.
    std::vector<Edge> half;
    for (std::size_t step = 1; step < count; ++step) half.push_back(pattern.edges[(axisIndex + step) % count]);
    if (half.empty()) return "there is nothing to mirror";

    const uint32_t none = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> image(pattern.vertices.size(), none);
    image[axisEdge.start] = axisEdge.start;  // points on the axis are their own image
    image[axisEdge.end] = axisEdge.end;
    const auto imageOf = [&](uint32_t vertex) {
        if (image[vertex] == none) {
            Vertex copy = pattern.vertices[vertex];
            copy.name.via.push_back(mirrorOp);
            copy.position = reflect(copy.position, a, unitAlong);
            image[vertex] = static_cast<uint32_t>(pattern.vertices.size());
            pattern.vertices.push_back(std::move(copy));
        }
        return image[vertex];
    };

    // The mirrored half is walked backwards so the joined outline stays
    // counter-clockwise. Reflecting and reversing an arc leaves its sweep as
    // is; a free curve's two control points swap places.
    std::vector<Edge> whole = half;
    for (auto it = half.rbegin(); it != half.rend(); ++it) {
        Edge copy = *it;
        copy.name.via.push_back(mirrorOp);
        copy.start = imageOf(it->end);
        copy.end = imageOf(it->start);
        if (copy.arc) copy.arc->center = reflect(copy.arc->center, a, unitAlong);
        if (copy.cubic)
            copy.cubic = CubicCurve{reflect(it->cubic->control2, a, unitAlong), reflect(it->cubic->control1, a, unitAlong)};
        whole.push_back(std::move(copy));
    }
    pattern.edges = std::move(whole);
    pattern.axes.push_back({mirrorOp, a, a + along});
    return std::nullopt;
}

void flip(Pattern& pattern) {
    for (Vertex& v : pattern.vertices) v.position.x = -v.position.x;
    for (InternalLine& l : pattern.lines) {
        l.start.x = -l.start.x;
        l.end.x = -l.end.x;
    }
    for (SymmetryAxis& axis : pattern.axes) {
        axis.start.x = -axis.start.x;
        axis.end.x = -axis.end.x;
    }
    std::reverse(pattern.edges.begin(), pattern.edges.end());
    for (Edge& e : pattern.edges) {
        std::swap(e.start, e.end);
        if (e.arc) e.arc->center.x = -e.arc->center.x;
        if (e.cubic) {
            std::swap(e.cubic->control1, e.cubic->control2);
            e.cubic->control1.x = -e.cubic->control1.x;
            e.cubic->control2.x = -e.cubic->control2.x;
        }
    }
    pattern.flipped = !pattern.flipped;
}

} // namespace twintailor::outline
