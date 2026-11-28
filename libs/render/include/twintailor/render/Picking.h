#pragma once
// What is under a place in the 3D view's picture: the ray through it, the
// panel or the spot of cloth it meets (the body in front hides what is
// behind it), and how a spot held there moves with the mouse across the
// view. In picture pixels (from the top left, y down), so it is tested
// without a window.
//
//   const render::ViewSize size{.width = 1200, .height = 900};
//   std::optional<PanelId> panel = render::panelAt(scene, camera, size, {600, 450});
//   std::optional<render::ClothHit> hit = render::clothAt(scene, camera, size, pressed);
//   std::optional<Vec3> by = render::acrossTheView(camera, size, hit->at, pressed, now);

#include <cstdint>
#include <optional>

#include "twintailor/core/Document.h"
#include "twintailor/render/Camera.h"
#include "twintailor/render/Scene.h"

namespace twintailor::render {

// The picture's size, in its own pixels.
struct ViewSize {
    int width = 1, height = 1;
};

// A place in the picture, in its pixels from the top left, y down.
struct PicturePoint {
    double x = 0.0, y = 0.0;
};

struct Ray {
    Vec3 from;       // the eye
    Vec3 direction;  // of unit length
};

// The ray from the eye through a place in the picture.
Ray rayThrough(const OrbitCamera& camera, ViewSize size, PicturePoint at);
// Where a point in space is drawn; empty when it is behind the camera.
std::optional<PicturePoint> pictureOf(const OrbitCamera& camera, ViewSize size, Vec3 point);

// The panel drawn at a place, if the nearest surface there is one (not the
// body, and never the overlay).
std::optional<PanelId> panelAt(const Scene& scene, const OrbitCamera& camera, ViewSize size, PicturePoint at);

// The cloth at a place: the panel, its particle nearest the spot, and the spot.
struct ClothHit {
    PanelId panel;
    std::uint32_t point = 0;
    Vec3 at;
};
std::optional<ClothHit> clothAt(const Scene& scene, const OrbitCamera& camera, ViewSize size, PicturePoint at);

// How far a spot `held`, taken hold of at `pressed`, moves when the mouse is
// at `now`: in the plane through it that faces the camera, so that it stays
// under the mouse. Empty when the mouse's ray runs along that plane.
std::optional<Vec3> acrossTheView(const OrbitCamera& camera, ViewSize size, Vec3 held, PicturePoint pressed, PicturePoint now);

} // namespace twintailor::render
