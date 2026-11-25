#pragma once
// Panels as triangle meshes, ready to be simulated as cloth.
//
//   Result shapes = evaluate(doc);
//   sim::ClothMesh cloth = sim::meshOf(doc, shapes, {.particleDistance = 2_cm});
//   cloth.panels[0].points, .triangles     // one mesh per panel, in the panel's own flat coordinates
//   cloth.stitches                         // which points of which panels are sewn together
//
// Every point is a cloth particle. Along a seam both edges get the same
// number of particles, so they pair up one to one even when the edges differ
// in length. Like evaluate(), this recomputes everything and never throws.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"

namespace clothcad::sim {

struct MeshSettings {
    // Roughly how far apart particles are in a panel that has no distance of
    // its own (Panel::particleDistance); smaller is finer and slower.
    double particleDistance = defaultParticleDistance;
};

// The particles along one edge of a panel, from the edge's start to its end.
struct EdgeParticles {
    EdgeRef edge;
    std::vector<uint32_t> points;  // indices into PanelMesh::points
};

struct PanelMesh {
    PanelId panel;
    std::vector<Vec2> points;                         // flat, in the panel's own coordinates, in cm
    std::vector<std::array<uint32_t, 3>> triangles;   // counter-clockwise
    std::vector<EdgeParticles> edges;                 // the outline's edges; not a full ring's closing slit
    std::string problem;                              // why the panel has no triangles, if it has none
    // The spot its arrangement places (Arrangement::anchor), as it was when
    // the mesh was made: a mesh a few frames old is still placed as it was.
    Vec2 anchor;

    // The particles along an edge; nullptr if the panel has no such edge.
    const EdgeParticles* along(const EdgeRef& edge) const;
    double area() const;
};

// Two particles sewn together.
struct Stitch {
    uint32_t panelA, pointA;  // panelA and panelB index ClothMesh::panels
    uint32_t panelB, pointB;
    SeamId seam;
    bool reversed = false;  // its seam sews start to start: the two panels' fronts face opposite ways across it
};

struct ClothMesh {
    std::vector<PanelMesh> panels;  // every panel cut from cloth that has an outline, in document order
    std::vector<Stitch> stitches;

    const PanelMesh* find(PanelId panel) const;
};

ClothMesh meshOf(const Document& doc, const Result& shapes, MeshSettings settings = {});

} // namespace clothcad::sim
