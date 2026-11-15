#pragma once
// The 3D view: the cloth meshes in space, seen through a camera that orbits
// a point. It draws and reports clicks; it changes nothing itself.
//
// Drag with the left button to orbit, with the right or middle button to
// pan, and roll the wheel to zoom. Dragging the selected panel carries it
// instead of orbiting.
//
// The picture is worked out here, pixel by pixel, with a depth for every
// pixel, so whatever is nearer hides what is behind it exactly. It is then
// handed to the graphics card as one image. Very large views are worked out
// at a lower resolution and stretched.

#include <optional>
#include <vector>

#include "app/Texture.h"
#include "app/Tools.h"
#include "core/Document.h"
#include "sim/Body.h"
#include "sim/Layout.h"
#include "sim/Mesh.h"

namespace clothcad::app {

// The selected panel dragged through space: how far it has been carried
// since the drag began, across the camera's view.
struct CarryPanel {
    PanelId panel;
    sim::Vec3 by;
    DragPhase phase;
};

struct View3DEvents {
    bool clicked = false;           // a click (not a drag) in the view
    std::optional<PanelId> panel;   // the panel under it, if any
    std::optional<CarryPanel> carried;
};

class View3D {
public:
    // Turns and moves the camera to show everything the next time it draws.
    void fit() { fitRequested_ = true; }

    // Draws into the current ImGui window, filling what is left of it.
    // `placement` runs parallel to cloth.panels. `body` may be nullptr.
    View3DEvents draw(const Document& doc, const sim::ClothMesh& cloth, const sim::Placement& placement,
                      const sim::Surface* body, std::optional<PanelId> selected, bool showWire);

    // Where a point in space was drawn at the last draw, in screen pixels;
    // empty if it was behind the camera.
    struct ScreenPoint {
        float x, y;
    };
    std::optional<ScreenPoint> toScreen(sim::Vec3 point) const;

    // The camera, for tests: angles in degrees, distance in cm.
    double yaw() const { return yaw_; }
    double pitch() const { return pitch_; }
    double distance() const { return distance_; }

private:
    // The camera looks at `target_` from `distance_` away. Yaw turns it
    // about the vertical axis, pitch raises it.
    sim::Vec3 target_{0.0, 80.0, 0.0};
    double yaw_ = 20.0;
    double pitch_ = 12.0;
    double distance_ = 300.0;
    bool fitRequested_ = true;
    bool dragged_ = false;  // whether the button now held has moved the camera or a panel

    // The selected panel being carried, from the press that took hold of it.
    struct Carrying {
        PanelId panel;
        float pressedX, pressedY;
        double depth;  // how far in front of the camera it was taken hold of
        bool moved = false;
    };
    std::optional<Carrying> carrying_;

    // The picture being worked out: a colour, a depth and the thing drawn for every pixel.
    int width_ = 0, height_ = 0;
    std::vector<std::uint32_t> colour_;
    std::vector<float> nearness_;        // one over the distance from the camera; zero where nothing is
    std::vector<std::uint32_t> thing_;   // 0 nothing, 1 + the cloth panel's index, or the body
    Texture texture_;

    struct Corner {
        float x, y, nearness;  // in the picture
        float red, green, blue;
    };
    void fill(const Corner& a, const Corner& b, const Corner& c, std::uint32_t thing);
    void stroke(sim::Vec3 from, sim::Vec3 to, std::uint32_t colour, float lift, float scale);

    // The body's shading directions, worked out once per body.
    const sim::Surface* shadedBody_ = nullptr;
    std::size_t shadedBodySize_ = 0;
    std::vector<sim::Vec3> bodyNormals_;

public:
    // The picture as it was last worked out, for tests.
    int pictureWidth() const { return width_; }
    int pictureHeight() const { return height_; }
    const std::vector<std::uint32_t>& picture() const { return colour_; }

private:

    // The camera as it was at the last draw.
    struct Camera {
        sim::Vec3 eye, right, up, forward;
        float centreX = 0.0f, centreY = 0.0f, focal = 1.0f;
    };
    Camera camera_;
    Camera cameraNow(float originX, float originY, float width, float height) const;
};

} // namespace clothcad::app
