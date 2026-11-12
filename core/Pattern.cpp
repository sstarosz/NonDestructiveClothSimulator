#include "core/Pattern.h"

#include <cmath>
#include <stdexcept>

#include "core/Units.h"

namespace clothcad {

bool Pattern::contains(EdgeRef edge) const {
    for (const Edge& e : edges)
        if (e.name == edge) return true;
    return false;
}

bool Pattern::contains(CornerRef corner) const {
    for (const Vertex& v : vertices)
        if (v.name == corner) return true;
    return false;
}

const Edge& Pattern::edge(EdgeRef edge) const {
    for (const Edge& e : edges)
        if (e.name == edge) return e;
    throw std::out_of_range("clothcad::Pattern: no such edge");
}

double Pattern::length(EdgeRef edge) const {
    const Edge& e = this->edge(edge);
    if (e.arc) return std::abs(e.arc->sweep) * (pi / 180.0) * e.arc->radius;
    const Vec2 a = vertices[e.start].position;
    const Vec2 b = vertices[e.end].position;
    return std::hypot(b.x - a.x, b.y - a.y);
}

Vec2 Pattern::position(CornerRef corner) const {
    for (const Vertex& v : vertices)
        if (v.name == corner) return v.position;
    throw std::out_of_range("clothcad::Pattern: no such corner");
}

} // namespace clothcad
