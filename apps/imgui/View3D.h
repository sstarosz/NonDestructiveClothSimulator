#pragma once
// The 3D view: the cloth meshes in space, seen through a camera that orbits
// a point. It draws and reports clicks; it changes nothing itself.
//
// Drag with the left button to orbit, with the right or middle button to
// pan, and roll the wheel to zoom. Dragging the selected panel carries it
// instead of orbiting. The selected panel also shows a move gizmo at its
// centre: an arrow along each axis moves it along that axis only, and the
// square in the middle moves it across the view.
//
// The picture itself is worked out by Picture, pixel by pixel with a depth
// for every pixel, and handed to the graphics card as one image. That only
// happens when something in it has changed; an unchanged view costs nothing.
// Very large views are worked out at a lower resolution and stretched.

#include <optional>
#include <vector>

#include "Picture.h"
#include "Texture.h"
#include "Tools.h"
#include "clothcad/core/Document.h"
#include "clothcad/sim/Body.h"
#include "clothcad/sim/Layout.h"
#include "clothcad/sim/Mesh.h"

namespace clothcad::app {

// The selected panel dragged through space: how far it has been carried
// since the drag began, across the camera's view.
struct CarryPanel {
    PanelId panel;
    sim::Vec3 by;
    DragPhase phase;
};

// The parts of the move gizmo that can be taken hold of.
enum class GizmoHandle { AcrossTheView, AlongX, AlongY, AlongZ };

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
    // `sceneVersion` must change whenever the cloth, its placement or the
    // body's shape has changed: the picture is only worked out again when
    // it, the camera or the view's size has.
    View3DEvents draw(const Document& doc, const sim::ClothMesh& cloth, const sim::Placement& placement,
                      const sim::Surface* body, std::optional<PanelId> selected, bool showWire, std::uint64_t sceneVersion);

    // Where a point in space was drawn at the last draw, in screen pixels;
    // empty if it was behind the camera.
    struct ScreenPoint {
        float x, y;
    };
    std::optional<ScreenPoint> toScreen(sim::Vec3 point) const;

    // Where a handle of the move gizmo was drawn at the last draw, in screen
    // pixels: the middle of an arrow, or the centre square. Empty when no
    // panel is selected, or the arrow points almost straight at the camera.
    std::optional<ScreenPoint> gizmoHandle(GizmoHandle handle) const;

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
        // Held by a gizmo arrow: the line it moves along, as it was when pressed.
        std::optional<GizmoHandle> handle = {};
        sim::Vec3 origin = {}, axis = {};
        double pressedAlong = 0.0;  // where along that line the press was
        double lastAlong = 0.0;     // the last place along it that could be worked out
    };
    std::optional<Carrying> carrying_;

    // The move gizmo as it was drawn at the last draw.
    struct Gizmo {
        sim::Vec3 centre;
        ScreenPoint centreOnScreen;
        std::optional<ScreenPoint> tips[3];  // x, y, z; empty when pointing at the camera
    };
    std::optional<Gizmo> gizmo_;

    // The picture, and what it was last drawn from: it is only worked out
    // again when one of these has changed.
    Picture picture_;
    Texture texture_;
    struct DrawnFrom {
        int width = 0, height = 0;
        Eye eye;
        std::uint64_t sceneVersion = 0;
        const sim::Surface* body = nullptr;
        std::size_t bodySize = 0;
        std::optional<std::size_t> selected;
        bool wire = false;
        double gridX = 0.0, gridZ = 0.0, gridReach = 0.0, gridStep = 0.0;
        friend bool operator==(const DrawnFrom&, const DrawnFrom&) = default;
    };
    std::optional<DrawnFrom> drawnFrom_;
    std::uint64_t picturesDrawn_ = 0;

public:
    // The picture as it was last worked out, and how often that has happened, for tests.
    int pictureWidth() const { return picture_.width(); }
    int pictureHeight() const { return picture_.height(); }
    const std::vector<std::uint32_t>& picture() const { return picture_.colours(); }
    std::uint64_t picturesDrawn() const { return picturesDrawn_; }

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
