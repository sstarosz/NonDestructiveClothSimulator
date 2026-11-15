#include "core/Drawing.h"

#include <algorithm>
#include <cmath>

#include "core/internal/Outline.h"

namespace clothcad {

namespace {

// How many straight pieces stand in for an edge.
int piecesFor(const Edge& e, double tolerance) {
    if (e.cubic) return 48;
    if (e.arc && e.arc->radius > tolerance) {
        // The angle a straight piece may span before it strays `tolerance` from the arc.
        const double step = 2.0 * std::acos(1.0 - tolerance / e.arc->radius);
        const double sweep = std::abs(e.arc->sweep) * (pi / 180.0);
        return std::clamp(static_cast<int>(std::ceil(sweep / step)), 1, 512);
    }
    return 1;
}

} // namespace

std::vector<Vec2> edgePoints(const Pattern& pattern, const Edge& edge, double tolerance) {
    const int pieces = piecesFor(edge, tolerance);
    std::vector<Vec2> points;
    for (int i = 0; i <= pieces; ++i) points.push_back(outline::pointAt(pattern, edge, static_cast<double>(i) / pieces));
    return points;
}

std::vector<Vec2> outlinePoints(const Pattern& pattern, double tolerance) {
    std::vector<Vec2> points;
    for (const Edge& e : pattern.edges) {
        const int pieces = piecesFor(e, tolerance);
        for (int i = 0; i < pieces; ++i) points.push_back(outline::pointAt(pattern, e, static_cast<double>(i) / pieces));
    }
    return points;
}

Vec2 midpoint(const Pattern& pattern, const Edge& edge) { return outline::pointAt(pattern, edge, 0.5); }

std::optional<Bounds> bounds(const Pattern& pattern) {
    const std::vector<Vec2> points = outlinePoints(pattern);
    if (points.empty()) return std::nullopt;
    Bounds box{points.front(), points.front()};
    for (const Vec2& p : points) {
        box.min.x = std::min(box.min.x, p.x);
        box.min.y = std::min(box.min.y, p.y);
        box.max.x = std::max(box.max.x, p.x);
        box.max.y = std::max(box.max.y, p.y);
    }
    return box;
}

std::optional<OutlinePoint> nearestOnOutline(const Pattern& pattern, Vec2 point) {
    const std::optional<outline::Nearest> found = outline::nearest(pattern, point);
    if (!found) return std::nullopt;
    const Edge& edge = pattern.edges[found->edge];
    return OutlinePoint{edge.name, found->along, outline::pointAt(pattern, edge, found->along)};
}

bool encloses(const Pattern& pattern, Vec2 point) {
    const std::vector<Vec2> polygon = outlinePoints(pattern);
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2 a = polygon[i], b = polygon[j];
        if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

std::optional<CubicCurve> cubicFromArc(const Pattern& pattern, const Edge& edge) {
    if (!edge.arc || std::abs(edge.arc->sweep) > 360.0 - 1e-6 || edge.start == edge.end) return std::nullopt;
    const Arc& arc = *edge.arc;
    const Vec2 start = pattern.vertices[edge.start].position, end = pattern.vertices[edge.end].position;
    const double turn = arc.sweep * (pi / 180.0);
    const double direction = turn >= 0.0 ? 1.0 : -1.0;
    const double reach = 4.0 / 3.0 * std::tan(std::abs(turn) / 4.0) * arc.radius;
    const double a0 = std::atan2(start.y - arc.center.y, start.x - arc.center.x);
    const double a1 = std::atan2(end.y - arc.center.y, end.x - arc.center.x);
    const Vec2 leaving{-std::sin(a0) * direction, std::cos(a0) * direction};
    const Vec2 arriving{-std::sin(a1) * direction, std::cos(a1) * direction};
    return CubicCurve{{start.x + leaving.x * reach, start.y + leaving.y * reach},
                      {end.x - arriving.x * reach, end.y - arriving.y * reach}};
}

Vec2 turned(Vec2 point, double degrees) {
    const double a = degrees * (pi / 180.0);
    const double c = std::cos(a), s = std::sin(a);
    return {point.x * c - point.y * s, point.x * s + point.y * c};
}

Pattern turned(const Pattern& pattern, double degrees) {
    if (degrees == 0.0) return pattern;
    Pattern result = pattern;
    for (Vertex& v : result.vertices) v.position = turned(v.position, degrees);
    for (Edge& e : result.edges) {
        if (e.arc) e.arc->center = turned(e.arc->center, degrees);
        if (e.cubic) {
            e.cubic->control1 = turned(e.cubic->control1, degrees);
            e.cubic->control2 = turned(e.cubic->control2, degrees);
        }
    }
    for (InternalLine& l : result.lines) {
        l.start = turned(l.start, degrees);
        l.end = turned(l.end, degrees);
    }
    for (SymmetryAxis& a : result.axes) {
        a.start = turned(a.start, degrees);
        a.end = turned(a.end, degrees);
    }
    return result;
}

} // namespace clothcad
