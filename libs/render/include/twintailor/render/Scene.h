#pragma once
// What the 3D view shows, as plain data: lit surfaces of triangles, lines,
// and how they are seen. It knows nothing of Vulkan or windows: the Renderer
// draws it, tests read it, and render/Picking finds what is under the mouse
// in it.
//
//   render::Scene scene;
//   scene.viewProjection = camera.viewProjection(aspect);
//   scene.toLight = camera.toEye();
//   scene.surfaces.push_back({.points = ..., .normals = ..., .triangles = ..., .colour = {0.8f, 0.8f, 0.8f}});

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "twintailor/core/Ids.h"

namespace twintailor::render {

struct Colour {
    float red = 0.0f, green = 0.0f, blue = 0.0f, alpha = 1.0f;
};

struct Point {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// A 4 by 4 matrix, column after column, as the shaders read it.
struct Matrix {
    std::array<float, 16> values{};
    float& at(int row, int column) { return values[static_cast<std::size_t>(column * 4 + row)]; }
    float at(int row, int column) const { return values[static_cast<std::size_t>(column * 4 + row)]; }
};

// Geometry that stays as it was made, such as a body's: made once and shared by
// every scene that shows it. The renderer hands it to the graphics card once,
// not with every picture, and keeps it there while anything holds the shape.
//
//   auto shape = std::make_shared<const render::Shape>(render::Shape{.points = ..., .normals = ..., .triangles = ...});
//   scene.surfaces.push_back({.colour = grey, .shape = shape});
struct Shape {
    std::vector<Point> points;
    std::vector<Point> normals;            // one per point
    std::vector<std::uint32_t> triangles;  // three points each
};

// Triangles lit from the light's side and from behind alike: cloth has two sides.
// The front of a surface is the side its triangles run counter-clockwise on.
// One whose colour lets light through (alpha below 1) is drawn after the
// others and the lines, so that what is behind it shows; it hides nothing
// itself. One that is `nearestOnly` besides comes after those, and only its
// side nearest the eye is blended in, once: a closed shape (the body) then
// looks like one sheet of glass, without its far side or its folds showing
// through it.
struct Surface {
    std::vector<Point> points;
    std::vector<Point> normals;              // one per point
    std::vector<std::uint32_t> triangles;    // three points each
    Colour colour;
    std::optional<PanelId> panel{};          // the panel it shows, for picking; none for the body
    bool lit = true;                         // false: drawn in its colour alone, without light and shade (the gizmo)
    std::optional<Colour> back{};            // its colour seen from behind; none: the same as from the front
    bool nearestOnly = false;                // of one that lets light through: only its nearest side is blended in
    std::shared_ptr<const Shape> shape{};    // its geometry, in place of the three lists above, which are then empty

    // The geometry that is drawn and picked, wherever it is held.
    const std::vector<Point>& shownPoints() const { return shape ? shape->points : points; }
    const std::vector<Point>& shownNormals() const { return shape ? shape->normals : normals; }
    const std::vector<std::uint32_t>& shownTriangles() const { return shape ? shape->triangles : triangles; }
};

// Straight lines, blended by their alpha.
struct Lines {
    std::vector<Point> points;  // two per line
    Colour colour;
    float width = 1.0f;  // in picture pixels; one where the card draws no wider (Renderer::widestLine)
    float lift = 0.0f;   // drawn this many cm nearer the eye, on the same spot of the picture, so that a surface they lie on does not cut them
};

struct Scene {
    Matrix viewProjection;          // from space (cm) to the picture
    Point toLight{0.0f, 0.0f, 1.0f};  // the direction the light comes from
    Colour background{0.094f, 0.106f, 0.133f};
    std::vector<Surface> surfaces;
    std::vector<Lines> lines;
    // Drawn last, in front of everything else whatever its depth (the move
    // gizmo), so that it can always be seen and taken hold of.
    std::vector<Surface> overlay;
};

} // namespace twintailor::render
