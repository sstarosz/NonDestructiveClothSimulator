#pragma once
// A computed 2D outline. Produced by evaluate(); plain output data.

#include <cstdint>
#include <optional>
#include <vector>

#include "clothcad/core/Ids.h"

namespace clothcad {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
    friend bool operator==(const Vec2&, const Vec2&) = default;
};

struct Vertex {
    CornerRef name;
    Vec2 position;
    friend bool operator==(const Vertex&, const Vertex&) = default;
};

// A circular arc from an edge's start vertex to its end vertex.
struct Arc {
    Vec2 center;
    double radius = 0.0;
    double sweep = 0.0;  // degrees; positive is counter-clockwise
    friend bool operator==(const Arc&, const Arc&) = default;
};

// A free curve (a cubic Bezier) from an edge's start vertex to its end
// vertex. It leaves the start heading for control1 and arrives at the end
// coming from control2.
struct CubicCurve {
    Vec2 control1;
    Vec2 control2;
    friend bool operator==(const CubicCurve&, const CubicCurve&) = default;
};

// An edge is straight unless it has an arc or a free curve; never both.
struct Edge {
    EdgeRef name;
    uint32_t start = 0;  // indices into Pattern::vertices
    uint32_t end = 0;
    std::optional<Arc> arc{};
    std::optional<CubicCurve> cubic{};
    bool seam = false;  // a join that is not an edge of the cloth (a full ring's closing slit): not drawn, not measured
    friend bool operator==(const Edge&, const Edge&) = default;
};

// A line drawn inside the outline by a Line operation.
struct InternalLine {
    LineRef name;
    Vec2 start;
    Vec2 end;
    friend bool operator==(const InternalLine&, const InternalLine&) = default;
};

// The line a Mirror operation reflected across.
struct SymmetryAxis {
    OpId mirror;
    Vec2 start;
    Vec2 end;
    friend bool operator==(const SymmetryAxis&, const SymmetryAxis&) = default;
};

struct Pattern {
    std::vector<Vertex> vertices;
    std::vector<Edge> edges;  // closed outline, counter-clockwise, each edge ending where the next starts
    std::vector<InternalLine> lines;
    std::vector<SymmetryAxis> axes;
    bool flipped = false;     // true when this is a mirrored instance of its source

    bool contains(const EdgeRef& edge) const;
    bool contains(const CornerRef& corner) const;
    bool contains(LineRef line) const;
    // These throw std::out_of_range if the name is absent.
    const Edge& edge(const EdgeRef& edge) const;
    const InternalLine& line(LineRef line) const;
    double length(const EdgeRef& edge) const;
    Vec2 position(const CornerRef& corner) const;
    friend bool operator==(const Pattern&, const Pattern&) = default;
};

} // namespace clothcad
