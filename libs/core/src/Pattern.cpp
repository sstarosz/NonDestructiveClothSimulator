#include "twintailor/core/Pattern.h"

#include <cmath>
#include <stdexcept>

#include "internal/Outline.h"

namespace twintailor {

bool Pattern::contains(const EdgeRef& edge) const {
    for (const Edge& e : edges)
        if (e.name == edge) return true;
    return false;
}

bool Pattern::contains(const CornerRef& corner) const {
    for (const Vertex& v : vertices)
        if (v.name == corner) return true;
    return false;
}

bool Pattern::contains(LineRef line) const {
    for (const InternalLine& l : lines)
        if (l.name == line) return true;
    return false;
}

const InternalLine& Pattern::line(LineRef line) const {
    for (const InternalLine& l : lines)
        if (l.name == line) return l;
    throw std::out_of_range("twintailor::Pattern: no such line");
}

const Edge& Pattern::edge(const EdgeRef& edge) const {
    for (const Edge& e : edges)
        if (e.name == edge) return e;
    throw std::out_of_range("twintailor::Pattern: no such edge");
}

double Pattern::length(const EdgeRef& edge) const {
    return outline::lengthOf(*this, this->edge(edge));
}

Vec2 Pattern::position(const CornerRef& corner) const {
    for (const Vertex& v : vertices)
        if (v.name == corner) return v.position;
    throw std::out_of_range("twintailor::Pattern: no such corner");
}

} // namespace twintailor
