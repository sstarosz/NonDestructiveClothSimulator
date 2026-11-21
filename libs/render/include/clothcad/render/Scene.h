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
#include <optional>
#include <vector>

#include "clothcad/core/Ids.h"

namespace clothcad::render {

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

// Triangles lit from the light's side and from behind alike: cloth has two sides.
struct Surface {
    std::vector<Point> points;
    std::vector<Point> normals;              // one per point
    std::vector<std::uint32_t> triangles;    // three points each
    Colour colour;
    std::optional<PanelId> panel{};          // the panel it shows, for picking; none for the body
    bool lit = true;                         // false: drawn in its colour alone, without light and shade (the gizmo)
};

// Straight lines, one pixel wide, blended by their alpha.
struct Lines {
    std::vector<Point> points;  // two per line
    Colour colour;
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

} // namespace clothcad::render
