#pragma once
// The 3D picture, worked out pixel by pixel on the processor: for every
// pixel a colour, how near the thing drawn there is, and which thing it is.
// Whatever is nearer hides what is behind it exactly.
//
//   Picture picture;
//   picture.draw(800, 600, eye, scene);
//   picture.colours()            // 800 * 600 colours, top row first
//   picture.thingAt(x, y)        // what is drawn at a pixel
//
// It knows nothing of windows or the mouse, so it can be tested and timed on
// its own. Large pictures are drawn in horizontal bands on several threads.

#include <cstdint>
#include <optional>
#include <vector>

#include "clothcad/sim/Body.h"
#include "clothcad/sim/Layout.h"
#include "clothcad/sim/Mesh.h"

namespace clothcad::app {

// Where the picture is taken from. `right`, `up` and `forward` are unit
// directions square to each other; `focal` is how many pixels a thing one cm
// across covers when it is one cm away.
struct Eye {
    sim::Vec3 at, right, up, forward;
    float focal = 1.0f;
    friend bool operator==(const Eye& a, const Eye& b) {
        const auto same = [](sim::Vec3 p, sim::Vec3 q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
        return same(a.at, b.at) && same(a.right, b.right) && same(a.up, b.up) && same(a.forward, b.forward) && a.focal == b.focal;
    }
};

// What is in the picture. The pointers must stay valid during draw().
struct Scene {
    const sim::ClothMesh* cloth = nullptr;
    const sim::Placement* placement = nullptr;  // parallel to cloth->panels
    const sim::Surface* body = nullptr;         // may be nullptr
    std::optional<std::size_t> selected;        // index of the selected panel in cloth->panels
    bool wire = false;                          // draw the triangles' sides on the cloth
    // The floor grid: lines every `gridStep` cm out to `gridReach` from its centre, heavier every metre.
    double gridX = 0.0, gridZ = 0.0, gridReach = 200.0, gridStep = 10.0;
};

class Picture {
public:
    static constexpr std::uint32_t nothing = 0u;           // thingAt: empty background
    static constexpr std::uint32_t body = 0xFFFFFFFFu;     // thingAt: the body
    // thingAt for cloth: 1 + the panel's index in the scene's cloth.

    // `threads` 0 lets the picture decide; 1 draws on the calling thread alone.
    void draw(int width, int height, const Eye& eye, const Scene& scene, unsigned threads = 0);

    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<std::uint32_t>& colours() const { return colour_; }  // in ImGui's IM_COL32 layout
    std::uint32_t thingAt(int x, int y) const;
    // How far from the eye the thing at a pixel is, in cm; empty where there is nothing.
    std::optional<double> depthAt(int x, int y) const;

private:
    int width_ = 0, height_ = 0;
    std::vector<std::uint32_t> colour_;
    std::vector<float> nearness_;       // one over the distance from the eye; zero where nothing is
    std::vector<std::uint32_t> thing_;

    // The body's shading directions, worked out once per body.
    const sim::Surface* shadedBody_ = nullptr;
    std::size_t shadedBodySize_ = 0;
    std::vector<sim::Vec3> bodyNormals_;
};

} // namespace clothcad::app
