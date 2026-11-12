#pragma once
// A computed 2D outline. Produced by evaluate(); plain output data.

#include <cstdint>
#include <optional>
#include <vector>

#include "core/Ids.h"

namespace clothcad {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Vertex {
    CornerRef name;
    Vec2 position;
};

// A circular arc from an edge's start vertex to its end vertex.
struct Arc {
    Vec2 center;
    double radius = 0.0;
    double sweep = 0.0;  // degrees; positive is counter-clockwise
};

struct Edge {
    EdgeRef name;
    uint32_t start = 0;      // indices into Pattern::vertices
    uint32_t end = 0;
    std::optional<Arc> arc;  // empty for a straight edge
};

struct Pattern {
    std::vector<Vertex> vertices;
    std::vector<Edge> edges;  // closed outline, counter-clockwise

    bool contains(EdgeRef edge) const;
    bool contains(CornerRef corner) const;
    // These throw std::out_of_range if the name is absent.
    const Edge& edge(EdgeRef edge) const;
    double length(EdgeRef edge) const;
    Vec2 position(CornerRef corner) const;
};

} // namespace clothcad
