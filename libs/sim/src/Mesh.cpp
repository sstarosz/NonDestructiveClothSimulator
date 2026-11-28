#include "twintailor/sim/Mesh.h"
#include "twintailor/sim/Layout.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <optional>

#include <CDT.h>

#include "twintailor/core/Drawing.h"

namespace twintailor::sim {

namespace {

constexpr double weld = 1e-6;         // points closer than this are one point
constexpr std::size_t mostPoints = 400000;
constexpr std::size_t mostAlongTheOutline = 20000;
constexpr const char* panelTooLarge = "the particle distance is too small for a panel this large";

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    double t = lengthSquared > 0.0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

double lengthOf(const std::vector<Vec2>& line) {
    double sum = 0.0;
    for (std::size_t i = 1; i < line.size(); ++i) sum += length(line[i] - line[i - 1]);
    return sum;
}

// How many pieces a length is cut into with particles `distance` apart. Any
// number above what an outline may have (and a length that is no number) comes
// out as one more than that: a mistyped size must not be made particle by
// particle, nor overflow the count.
std::size_t piecesOf(double length, double distance) {
    const double pieces = std::round(length / distance);
    if (!(pieces <= static_cast<double>(mostAlongTheOutline))) return mostAlongTheOutline + 1;
    return pieces > 1.0 ? static_cast<std::size_t>(pieces) : 1;
}

// An edge as straight pieces, close enough for particles that far apart.
std::vector<Vec2> lineOf(const Pattern& pattern, const Edge& edge, double distance) {
    return edgePoints(pattern, edge, std::min(0.01, distance / 50.0));
}

// Whether the outline needs more pieces than a mesh may have, by its own
// lengths alone.
bool tooLongToMesh(const Pattern& pattern, double distance) {
    std::size_t along = 0;
    for (const Edge& edge : pattern.edges) {
        if (edge.seam) continue;
        const double length = lengthOf(lineOf(pattern, edge, distance));
        if (length < weld) continue;  // an edge squeezed to nothing
        along += piecesOf(length, distance);
        if (along > mostAlongTheOutline) return true;
    }
    return false;
}

// `pieces` - 1 points spread evenly by length along a line, ends not included.
std::vector<Vec2> spreadAlong(const std::vector<Vec2>& line, std::size_t pieces) {
    std::vector<Vec2> points;
    const double total = lengthOf(line);
    std::size_t segment = 1;
    double before = 0.0;  // length of the line up to line[segment - 1]
    for (std::size_t k = 1; k < pieces; ++k) {
        const double target = total * static_cast<double>(k) / static_cast<double>(pieces);
        while (segment + 1 < line.size() && before + length(line[segment] - line[segment - 1]) < target) {
            before += length(line[segment] - line[segment - 1]);
            ++segment;
        }
        const double span = length(line[segment] - line[segment - 1]);
        const double t = span > 0.0 ? std::clamp((target - before) / span, 0.0, 1.0) : 0.0;
        points.push_back({line[segment - 1].x + (line[segment].x - line[segment - 1].x) * t,
                          line[segment - 1].y + (line[segment].y - line[segment - 1].y) * t});
    }
    return points;
}

// How far apart the panel's particles are: its own, or the default.
double distanceOf(const Document& doc, PanelId panel, double fallback) {
    return std::max(doc.contains(panel) ? doc.get(panel).particleDistance.value_or(fallback) : fallback, 0.05);
}

// An edge of a panel: what a seam side names.
struct EdgeKey {
    PanelId panel;
    EdgeRef edge;
    friend bool operator==(const EdgeKey&, const EdgeKey&) = default;
};

// How many pieces each sewn edge is divided into. Edges joined by seams,
// directly or through other seams, share one count: that of the longest,
// cut as finely as the finest panel among them asks. A seam to a panel that is
// too large to mesh counts for nothing: its other side is cut as if alone.
class PieceCounts {
public:
    PieceCounts(const Document& doc, const Result& shapes, double fallbackDistance, const std::vector<PanelId>& tooLarge) {
        const auto refused = [&](PanelId panel) { return std::find(tooLarge.begin(), tooLarge.end(), panel) != tooLarge.end(); };
        for (const SeamResult& sewn : shapes.seams) {
            if (!sewn.sewn() || !doc.contains(sewn.seam)) continue;
            const Seam& seam = doc.get(sewn.seam);
            if (refused(seam.a.panel) || refused(seam.b.panel)) continue;
            const std::size_t a = indexOf({seam.a.panel, seam.a.edge}, sewn.lengthA, distanceOf(doc, seam.a.panel, fallbackDistance));
            const std::size_t b = indexOf({seam.b.panel, seam.b.edge}, sewn.lengthB, distanceOf(doc, seam.b.panel, fallbackDistance));
            group_[root(a)] = root(b);
        }
        std::vector<double> longest(keys_.size(), 0.0), finest(keys_.size(), std::numeric_limits<double>::max());
        for (std::size_t i = 0; i < keys_.size(); ++i) {
            longest[root(i)] = std::max(longest[root(i)], length_[i]);
            finest[root(i)] = std::min(finest[root(i)], distance_[i]);
        }
        pieces_.resize(keys_.size());
        for (std::size_t i = 0; i < keys_.size(); ++i) pieces_[i] = piecesOf(longest[root(i)], finest[root(i)]);
    }

    // 0 when the edge is in no seam.
    std::size_t of(const EdgeKey& key) const {
        for (std::size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i] == key) return pieces_[i];
        return 0;
    }

private:
    std::size_t indexOf(const EdgeKey& key, double length, double distance) {
        for (std::size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i] == key) return i;
        keys_.push_back(key);
        length_.push_back(length);
        distance_.push_back(distance);
        group_.push_back(keys_.size() - 1);
        return keys_.size() - 1;
    }
    std::size_t root(std::size_t i) {
        while (group_[i] != i) i = group_[i] = group_[group_[i]];
        return i;
    }

    std::vector<EdgeKey> keys_;
    std::vector<double> length_;
    std::vector<double> distance_;  // of the panel the edge is on
    std::vector<std::size_t> group_;
    std::vector<std::size_t> pieces_;
};

struct Segment {
    uint32_t a, b;
};

PanelMesh meshPanel(PanelId panel, const Pattern& pattern, const PieceCounts& counts, double d) {
    PanelMesh mesh;
    mesh.panel = panel;
    std::vector<Segment> boundary;

    // Corners first. Corners in the same place are one particle: that is
    // what closes a full ring across its slit.
    std::vector<uint32_t> cornerPoint(pattern.vertices.size());
    for (std::size_t i = 0; i < pattern.vertices.size(); ++i) {
        const Vec2 at = pattern.vertices[i].position;
        std::size_t same = 0;
        while (same < mesh.points.size() && length(at - mesh.points[same]) >= weld) ++same;
        if (same == mesh.points.size()) mesh.points.push_back(at);
        cornerPoint[i] = static_cast<uint32_t>(same);
    }

    // Then how many pieces each edge is cut into: nothing is put along an
    // outline that needs more than a mesh may have.
    struct Division {
        const Edge& edge;
        std::vector<Vec2> line;
        std::size_t pieces;
    };
    std::vector<Division> divisions;
    std::size_t alongTheOutline = 0;
    for (const Edge& edge : pattern.edges) {
        if (edge.seam) continue;
        std::vector<Vec2> line = lineOf(pattern, edge, d);
        const double length = lengthOf(line);
        if (length < weld) continue;  // an edge squeezed to nothing
        std::size_t pieces = counts.of({panel, edge.name});
        if (pieces == 0) pieces = piecesOf(length, d);
        const bool closed = cornerPoint[edge.start] == cornerPoint[edge.end];
        if (closed) pieces = std::max<std::size_t>(pieces, 3);
        alongTheOutline += pieces;
        if (alongTheOutline > mostAlongTheOutline) {
            mesh.problem = panelTooLarge;
            return mesh;
        }
        divisions.push_back({edge, std::move(line), pieces});
    }

    // Then particles along each edge.
    for (const auto& [edge, line, pieces] : divisions) {
        EdgeParticles along{edge.name, {cornerPoint[edge.start]}};
        for (const Vec2& point : spreadAlong(line, pieces)) {
            along.points.push_back(static_cast<uint32_t>(mesh.points.size()));
            mesh.points.push_back(point);
        }
        along.points.push_back(cornerPoint[edge.end]);
        for (std::size_t i = 1; i < along.points.size(); ++i) boundary.push_back({along.points[i - 1], along.points[i]});
        mesh.edges.push_back(std::move(along));
    }
    if (boundary.size() < 3) {
        mesh.problem = "the outline is too small to mesh";
        return mesh;
    }

    // How large the mesh would be.
    Vec2 low = mesh.points.front(), high = mesh.points.front();
    for (const Vec2& p : mesh.points) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y)};
    }
    const double rowHeight = d * std::sqrt(3.0) / 2.0;
    if ((high.x - low.x) / d * ((high.y - low.y) / rowHeight) > static_cast<double>(mostPoints)) {
        mesh.problem = panelTooLarge;
        return mesh;
    }

    // An outline that crosses itself has no inside to fill.
    const auto side = [&](Vec2 a, Vec2 b, Vec2 c) { return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y); };
    for (std::size_t i = 0; i < boundary.size(); ++i)
        for (std::size_t j = i + 1; j < boundary.size(); ++j) {
            const Segment& s = boundary[i];
            const Segment& t = boundary[j];
            if (s.a == t.a || s.a == t.b || s.b == t.a || s.b == t.b) continue;  // neighbours share an end
            const Vec2 a = mesh.points[s.a], b = mesh.points[s.b], c = mesh.points[t.a], e = mesh.points[t.b];
            if (std::max(a.x, b.x) < std::min(c.x, e.x) || std::max(c.x, e.x) < std::min(a.x, b.x)) continue;
            if (std::max(a.y, b.y) < std::min(c.y, e.y) || std::max(c.y, e.y) < std::min(a.y, b.y)) continue;
            const double d1 = side(a, b, c), d2 = side(a, b, e), d3 = side(c, e, a), d4 = side(c, e, b);
            if (((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0))) {
                mesh.problem = "the outline crosses itself, so it cannot be meshed";
                return mesh;
            }
        }

    // Then particles inside, on a triangular grid, which gives even triangles.
    const auto inside = [&](Vec2 p) {
        bool in = false;
        for (const Segment& s : boundary) {
            const Vec2 a = mesh.points[s.a], b = mesh.points[s.b];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
        }
        return in;
    };
    const auto clearOfBoundary = [&](Vec2 p) {
        for (const Segment& s : boundary)
            if (distanceToSegment(p, mesh.points[s.a], mesh.points[s.b]) < 0.6 * d) return false;
        return true;
    };
    int row = 0;
    for (double y = low.y + rowHeight / 2.0; y < high.y; y += rowHeight, ++row)
        for (double x = low.x + (row % 2 == 0 ? d / 2.0 : d); x < high.x; x += d) {
            const Vec2 p{x, y};
            if (inside(p) && clearOfBoundary(p)) mesh.points.push_back(p);
        }

    // Triangulate, keeping the outline's segments as triangle sides.
    try {
        CDT::Triangulation<double> cdt(CDT::VertexInsertionOrder::Auto, CDT::IntersectingConstraintEdges::NotAllowed, 0.0);
        std::vector<CDT::V2d<double>> vertices;
        vertices.reserve(mesh.points.size());
        for (const Vec2& p : mesh.points) vertices.push_back(CDT::V2d<double>(p.x, p.y));
        std::vector<CDT::Edge> edges;
        edges.reserve(boundary.size());
        for (const Segment& s : boundary) edges.emplace_back(s.a, s.b);
        cdt.insertVertices(vertices);
        cdt.insertEdges(edges);
        cdt.eraseOuterTrianglesAndHoles();
        for (const CDT::Triangle& t : cdt.triangles) {
            std::array<uint32_t, 3> corners{t.vertices[0], t.vertices[1], t.vertices[2]};
            const Vec2 a = mesh.points[corners[0]], b = mesh.points[corners[1]], c = mesh.points[corners[2]];
            const double twiceArea = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (std::abs(twiceArea) < 1e-12) continue;
            if (twiceArea < 0.0) std::swap(corners[1], corners[2]);
            mesh.triangles.push_back(corners);
        }
    } catch (const std::exception&) {
        mesh.triangles.clear();
        mesh.problem = "the outline crosses itself, so it cannot be meshed";
        return mesh;
    }
    if (mesh.triangles.empty()) mesh.problem = "the outline encloses no area";
    return mesh;
}

} // namespace

const EdgeParticles* PanelMesh::along(const EdgeRef& edge) const {
    for (const EdgeParticles& e : edges)
        if (e.edge == edge) return &e;
    return nullptr;
}

double PanelMesh::area() const {
    double sum = 0.0;
    for (const std::array<uint32_t, 3>& t : triangles) {
        const Vec2 a = points[t[0]], b = points[t[1]], c = points[t[2]];
        sum += ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2.0;
    }
    return sum;
}

const PanelMesh* ClothMesh::find(PanelId panel) const {
    for (const PanelMesh& mesh : panels)
        if (mesh.panel == panel) return &mesh;
    return nullptr;
}

ClothMesh meshOf(const Document& doc, const Result& shapes, MeshSettings settings) {
    ClothMesh cloth;
    std::vector<PanelId> refused;  // too large by their own outlines
    for (const PanelResult& result : shapes.panels)
        if (tooLongToMesh(result.pattern, distanceOf(doc, result.panel, settings.particleDistance))) refused.push_back(result.panel);
    const PieceCounts counts(doc, shapes, settings.particleDistance, refused);
    for (const PanelResult& result : shapes.panels) {
        if (!doc.contains(result.panel) || doc.get(result.panel).construction) continue;
        if (result.pattern.edges.empty()) continue;
        cloth.panels.push_back(meshPanel(result.panel, result.pattern, counts, distanceOf(doc, result.panel, settings.particleDistance)));
        cloth.panels.back().anchor = anchorOf(doc.get(result.panel).arrangement, result.pattern);
    }

    const auto indexOf = [&](PanelId panel) -> std::optional<uint32_t> {
        for (std::size_t i = 0; i < cloth.panels.size(); ++i)
            if (cloth.panels[i].panel == panel) return static_cast<uint32_t>(i);
        return std::nullopt;
    };
    for (const SeamResult& sewn : shapes.seams) {
        if (!sewn.sewn() || !doc.contains(sewn.seam)) continue;
        const Seam& seam = doc.get(sewn.seam);
        const std::optional<uint32_t> a = indexOf(seam.a.panel), b = indexOf(seam.b.panel);
        if (!a || !b) continue;
        const EdgeParticles* alongA = cloth.panels[*a].along(seam.a.edge);
        const EdgeParticles* alongB = cloth.panels[*b].along(seam.b.edge);
        if (!alongA || !alongB || alongA->points.size() != alongB->points.size()) continue;
        const std::size_t count = alongA->points.size();
        for (std::size_t i = 0; i < count; ++i) {
            // Start meets end, unless the seam is reversed.
            const uint32_t partner = alongB->points[seam.reversed ? i : count - 1 - i];
            if (*a == *b && alongA->points[i] == partner) continue;  // a shared corner is already one particle
            cloth.stitches.push_back({*a, alongA->points[i], *b, partner, sewn.seam, seam.reversed});
        }
    }
    return cloth;
}

} // namespace twintailor::sim
