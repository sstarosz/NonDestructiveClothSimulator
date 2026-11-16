#include "View3D.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "Style.h"

namespace clothcad::app {

namespace {

using sim::Vec3;

constexpr double nearPlane = 2.0;  // cm: nothing closer to the camera is drawn
constexpr double fieldOfView = 40.0 * 3.14159265358979323846 / 180.0;

// The most pixels the picture is worked out at; larger views are stretched from this.
constexpr float mostPixels = 1.5e6f;

// The gizmo's arrows are this many pixels long (times pixel()), whatever the distance.
constexpr float gizmoArrow = 90.0f;
// How near the mouse must be to a gizmo handle to take hold of it, in pixels (times pixel()).
constexpr float gizmoReach = 8.0f;

const Vec3 axisOf[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
const char* const axisName[3] = {"x", "y", "z"};
const ImVec4* const axisColour[3] = {&color::axisX, &color::axisY, &color::axisZ};

int axisIndex(GizmoHandle handle) { return static_cast<int>(handle) - static_cast<int>(GizmoHandle::AlongX); }

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    const float t = lengthSquared > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    const float x = a.x + t * dx - p.x, y = a.y + t * dy - p.y;
    return std::sqrt(x * x + y * y);
}

} // namespace

View3D::Camera View3D::cameraNow(float originX, float originY, float width, float height) const {
    const double yaw = yaw_ * 3.14159265358979323846 / 180.0, pitch = pitch_ * 3.14159265358979323846 / 180.0;
    Camera camera;
    const Vec3 back{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    camera.eye = target_ + back * distance_;
    camera.forward = back * -1.0;
    camera.right = sim::normalized(sim::cross(camera.forward, Vec3{0.0, 1.0, 0.0}));
    camera.up = sim::cross(camera.right, camera.forward);
    camera.centreX = originX + width / 2.0f;
    camera.centreY = originY + height / 2.0f;
    camera.focal = height / 2.0f / static_cast<float>(std::tan(fieldOfView / 2.0));
    return camera;
}

std::optional<View3D::ScreenPoint> View3D::toScreen(Vec3 point) const {
    const Vec3 from = point - camera_.eye;
    const double depth = sim::dot(from, camera_.forward);
    if (depth < nearPlane) return std::nullopt;
    return ScreenPoint{camera_.centreX + static_cast<float>(sim::dot(from, camera_.right) / depth) * camera_.focal,
                       camera_.centreY - static_cast<float>(sim::dot(from, camera_.up) / depth) * camera_.focal};
}

std::optional<View3D::ScreenPoint> View3D::gizmoHandle(GizmoHandle handle) const {
    if (!gizmo_) return std::nullopt;
    if (handle == GizmoHandle::AcrossTheView) return gizmo_->centreOnScreen;
    const std::optional<ScreenPoint>& tip = gizmo_->tips[axisIndex(handle)];
    if (!tip) return std::nullopt;
    return ScreenPoint{(gizmo_->centreOnScreen.x + tip->x) / 2.0f, (gizmo_->centreOnScreen.y + tip->y) / 2.0f};
}

View3DEvents View3D::draw(const Document& doc, const sim::ClothMesh& cloth, const sim::Placement& placement,
                          const sim::Surface* body, std::optional<PanelId> selected, bool showWire, std::uint64_t sceneVersion) {
    View3DEvents events;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 50.0f);
    size.y = std::max(size.y, 50.0f);
    ImGui::InvisibleButton("view3d", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImGuiIO& io = ImGui::GetIO();
    const float px = pixel();

    // ---- Camera: fit, orbit, pan, zoom ----
    if (fitRequested_) {
        bool any = false;
        Vec3 low, high;
        for (const std::vector<Vec3>& positions : placement)
            for (const Vec3& p : positions) {
                if (!any) low = high = p;
                any = true;
                low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
            }
        if (body)
            for (const Vec3& p : body->points) {
                if (!any) low = high = p;
                any = true;
                low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
            }
        if (any) {
            target_ = (low + high) * 0.5;
            const double radius = std::max(sim::length(high - low) / 2.0, 10.0);
            const double narrow = std::min(1.0, static_cast<double>(size.x) / size.y);  // a narrow view sees less sideways
            distance_ = radius / std::sin(fieldOfView / 2.0) / narrow * 1.05;
            fitRequested_ = false;
        }
    }
    camera_ = cameraNow(origin.x, origin.y, size.x, size.y);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner{origin.x + size.x, origin.y + size.y};

    // ---- The picture: as many pixels as the view, up to a limit; worked out only when something changed ----
    const float scale = std::min(1.0f, std::sqrt(mostPixels / (size.x * size.y)));
    const int width = std::max(static_cast<int>(size.x * scale), 8), height = std::max(static_cast<int>(size.y * scale), 8);
    const float toPictureX = static_cast<float>(width) / size.x, toPictureY = static_cast<float>(height) / size.y;
    {
        Scene scene;
        scene.cloth = &cloth;
        scene.placement = &placement;
        scene.body = body;
        scene.wire = showWire;
        for (std::size_t i = 0; i < cloth.panels.size(); ++i)
            if (selected && cloth.panels[i].panel == *selected) scene.selected = i;
        scene.gridReach = std::clamp(std::ceil(distance_ * 1.2 / 100.0) * 100.0, 200.0, 2000.0);
        scene.gridStep = scene.gridReach > 800.0 ? 100.0 : 10.0;
        scene.gridX = std::round(target_.x / 100.0) * 100.0;
        scene.gridZ = std::round(target_.z / 100.0) * 100.0;
        const Eye eye{camera_.eye, camera_.right, camera_.up, camera_.forward, camera_.focal * scale};
        const DrawnFrom from{width, height, eye, sceneVersion, body, body ? body->triangles.size() : 0, scene.selected, showWire, scene.gridX, scene.gridZ, scene.gridReach, scene.gridStep};
        if (!drawnFrom_ || !(*drawnFrom_ == from) || texture_.empty()) {
            picture_.draw(width, height, eye, scene);
            texture_.set(picture_.colours().data(), picture_.width(), picture_.height());
            drawnFrom_ = from;
            ++picturesDrawn_;
        }
    }
    draw->AddImage(texture_.id(), origin, corner);
    draw->PushClipRect(origin, corner, true);

    // What is drawn at a place on the screen.
    const auto thingUnder = [&](ImVec2 at) { return picture_.thingAt(static_cast<int>((at.x - origin.x) * toPictureX), static_cast<int>((at.y - origin.y) * toPictureY)); };
    constexpr std::uint32_t bodyThing = Picture::body;
    const auto selectedUnder = [&](ImVec2 at) {
        const std::uint32_t thing = thingUnder(at);
        return selected && thing != Picture::nothing && thing != bodyThing && thing - 1u < cloth.panels.size() && cloth.panels[thing - 1u].panel == *selected;
    };
    const auto depthUnder = [&](ImVec2 at) {
        return picture_.depthAt(static_cast<int>((at.x - origin.x) * toPictureX), static_cast<int>((at.y - origin.y) * toPictureY)).value_or(distance_);
    };

    // ---- The move gizmo on the selected panel, at its centre in space ----
    gizmo_.reset();
    if (selected)
        if (const sim::PanelMesh* mesh = cloth.find(*selected)) {
            const Arrangement place = sim::arrangementOf(doc, *mesh);
            const Vec3 centre{place.x, place.y, place.z};
            if (const std::optional<ScreenPoint> at = toScreen(centre)) {
                Gizmo gizmo{centre, *at, {}};
                const double depth = sim::dot(centre - camera_.eye, camera_.forward);
                const double arrow = gizmoArrow * px * depth / camera_.focal;  // in cm, so that it is gizmoArrow pixels long
                for (int i = 0; i < 3; ++i)
                    if (const std::optional<ScreenPoint> tip = toScreen(centre + axisOf[i] * arrow)) {
                        // An arrow pointing almost at the camera is too short to take hold of.
                        if (std::hypot(tip->x - at->x, tip->y - at->y) > 2.0f * gizmoReach * px) gizmo.tips[i] = tip;
                    }
                gizmo_ = gizmo;
            }
        }
    // The gizmo handle under a place on the screen: the centre square first, then the nearest arrow.
    const auto handleUnder = [&](ImVec2 at) -> std::optional<GizmoHandle> {
        if (!gizmo_) return std::nullopt;
        const ImVec2 centre{gizmo_->centreOnScreen.x, gizmo_->centreOnScreen.y};
        if (std::abs(at.x - centre.x) <= gizmoReach * px && std::abs(at.y - centre.y) <= gizmoReach * px) return GizmoHandle::AcrossTheView;
        std::optional<GizmoHandle> nearest;
        float best = gizmoReach * px;
        for (int i = 0; i < 3; ++i)
            if (const std::optional<ScreenPoint>& tip = gizmo_->tips[i])
                if (const float d = distanceToSegment(at, centre, {tip->x, tip->y}); d <= best) {
                    best = d;
                    nearest = static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::AlongX) + i);
                }
        return nearest;
    };
    // Where along the line `origin + axis * t` the ray under a place on the
    // screen passes closest; empty when the line points almost along the ray.
    const auto along = [&](Vec3 from, Vec3 axis, ImVec2 at) -> std::optional<double> {
        const Vec3 ray = sim::normalized(camera_.forward * camera_.focal + camera_.right * (at.x - camera_.centreX) - camera_.up * (at.y - camera_.centreY));
        const double b = sim::dot(axis, ray), across = 1.0 - b * b;
        if (across < 1e-3) return std::nullopt;
        const Vec3 between = from - camera_.eye;
        return std::clamp((b * sim::dot(ray, between) - sim::dot(axis, between)) / across, -2000.0, 2000.0);
    };

    // ---- The mouse: carry the selected panel, or orbit, pan and zoom ----
    if (ImGui::IsItemActivated()) {
        dragged_ = false;
        carrying_.reset();
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (const std::optional<GizmoHandle> handle = handleUnder(io.MousePos)) {
                Carrying grabbed{.panel = *selected, .pressedX = io.MousePos.x, .pressedY = io.MousePos.y,
                              .depth = sim::dot(gizmo_->centre - camera_.eye, camera_.forward), .handle = handle, .origin = gizmo_->centre};
                if (*handle == GizmoHandle::AcrossTheView) {
                    carrying_ = grabbed;
                } else if (const std::optional<double> pressed = along(gizmo_->centre, axisOf[axisIndex(*handle)], io.MousePos)) {
                    grabbed.axis = axisOf[axisIndex(*handle)];
                    grabbed.pressedAlong = grabbed.lastAlong = *pressed;
                    carrying_ = grabbed;
                }
            } else if (selectedUnder(io.MousePos)) {
                carrying_ = Carrying{.panel = *selected, .pressedX = io.MousePos.x, .pressedY = io.MousePos.y, .depth = depthUnder(io.MousePos)};
            }
        }
    }
    if (carrying_) {
        const bool released = !ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool moving = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f * px);
        std::optional<DragPhase> phase;
        if (released && carrying_->moved) phase = DragPhase::End;
        else if (!released && moving) phase = carrying_->moved ? DragPhase::Continue : DragPhase::Begin;
        if (phase) {
            const double step = io.KeyCtrl ? 1.0 : 0.1;  // whole centimetres with Ctrl, else millimetres
            const auto snapped = [step](double value) { return std::round(value / step) * step; };
            const bool onAnAxis = carrying_->handle && *carrying_->handle != GizmoHandle::AcrossTheView;
            Vec3 by;
            char label[64];
            if (onAnAxis) {
                // Along one axis only: the point on its line nearest the mouse.
                if (const std::optional<double> now = along(carrying_->origin, carrying_->axis, io.MousePos)) carrying_->lastAlong = *now;
                const double amount = snapped(carrying_->lastAlong - carrying_->pressedAlong);
                by = carrying_->axis * amount;
                std::snprintf(label, sizeof label, "%+.1f cm along %s", amount, axisName[axisIndex(*carrying_->handle)]);
            } else {
                const double perPixel = carrying_->depth / camera_.focal;
                by = camera_.right * ((io.MousePos.x - carrying_->pressedX) * perPixel) + camera_.up * (-(io.MousePos.y - carrying_->pressedY) * perPixel);
                by = {snapped(by.x), snapped(by.y), snapped(by.z)};
                std::snprintf(label, sizeof label, "%+.1f, %+.1f, %+.1f cm", by.x, by.y, by.z);
            }
            events.carried = CarryPanel{carrying_->panel, by, *phase};
            draw->AddText({io.MousePos.x + 14.0f * px, io.MousePos.y + 14.0f * px}, IM_COL32(110, 220, 200, 255), label);
            carrying_->moved = true;
            dragged_ = true;
        }
        if (released) carrying_.reset();
    } else {
        if (held && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f * px)) {
            yaw_ -= io.MouseDelta.x * 0.4;
            pitch_ = std::clamp(pitch_ + io.MouseDelta.y * 0.4, -89.0, 89.0);
            dragged_ = true;
        }
        if (held && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
            // Move the target so that what is under the cursor follows it.
            const double perPixel = distance_ / camera_.focal;
            target_ = target_ - camera_.right * (io.MouseDelta.x * perPixel) + camera_.up * (io.MouseDelta.y * perPixel);
            dragged_ = true;
        }
    }
    if (hovered && io.MouseWheel != 0.0f) distance_ = std::clamp(distance_ * std::pow(0.87, static_cast<double>(io.MouseWheel)), 5.0, 5000.0);
    const std::optional<GizmoHandle> hoveredHandle = hovered && !held ? handleUnder(io.MousePos) : std::nullopt;
    if (hoveredHandle == GizmoHandle::AcrossTheView) ImGui::SetTooltip("Drag to move the panel across the view.\nCtrl: whole centimetres.");
    else if (hoveredHandle) ImGui::SetTooltip("Drag to move the panel along %s only.\nCtrl: whole centimetres.", axisName[axisIndex(*hoveredHandle)]);
    else if (hovered && !held && selectedUnder(io.MousePos)) ImGui::SetTooltip("Drag to carry this panel. Turn and wrap it in the details.");

    // The gizmo is drawn over the picture, so it can always be reached. The
    // handle held or under the mouse is drawn in the selection colour.
    if (gizmo_) {
        const std::optional<GizmoHandle> lit = carrying_ && carrying_->handle ? carrying_->handle : hoveredHandle;
        const ImVec2 centre{gizmo_->centreOnScreen.x, gizmo_->centreOnScreen.y};
        const ImU32 shadow = IM_COL32(0, 0, 0, 140);
        for (int i = 0; i < 3; ++i) {
            const std::optional<ScreenPoint>& tip = gizmo_->tips[i];
            if (!tip) continue;
            const bool on = lit && axisIndex(*lit) == i;
            const ImU32 colour = ImGui::GetColorU32(on ? color::selected : *axisColour[i]);
            const ImVec2 end{tip->x, tip->y};
            const float dx = end.x - centre.x, dy = end.y - centre.y, length = std::hypot(dx, dy);
            const float ux = dx / length, uy = dy / length;
            const float head = 12.0f * px, half = 5.0f * px;
            const ImVec2 base{end.x - ux * head, end.y - uy * head};
            draw->AddLine(centre, base, shadow, 5.0f * px);
            draw->AddLine(centre, base, colour, (on ? 3.5f : 2.5f) * px);
            draw->AddTriangleFilled(end, {base.x - uy * half, base.y + ux * half}, {base.x + uy * half, base.y - ux * half}, colour);
            draw->AddText({end.x + 4.0f * px, end.y - 8.0f * px}, colour, axisName[i]);
        }
        const bool on = lit == GizmoHandle::AcrossTheView;
        const float half = 5.0f * px;
        draw->AddRectFilled({centre.x - half, centre.y - half}, {centre.x + half, centre.y + half}, on ? ImGui::GetColorU32(color::selected) : IM_COL32(235, 235, 235, 230));
        draw->AddRect({centre.x - half, centre.y - half}, {centre.x + half, centre.y + half}, shadow, 0.0f, 0, 1.0f * px);
    }

    // A click (not a drag) picks the nearest panel under the cursor.
    if (ImGui::IsItemDeactivated() && !dragged_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        events.clicked = true;
        if (const std::uint32_t thing = thingUnder(io.MousePos); thing != 0u && thing != bodyThing && thing - 1u < cloth.panels.size())
            events.panel = cloth.panels[thing - 1u].panel;
    }

    // ---- Which way is which, and what the mouse does ----
    {
        const ImVec2 at{origin.x + 34.0f * px, corner.y - 34.0f * px};
        for (int i = 0; i < 3; ++i) {
            const ImU32 colour = ImGui::GetColorU32(*axisColour[i]);
            const ImVec2 tip{at.x + static_cast<float>(sim::dot(axisOf[i], camera_.right)) * 22.0f * px,
                             at.y - static_cast<float>(sim::dot(axisOf[i], camera_.up)) * 22.0f * px};
            draw->AddLine(at, tip, colour, 2.0f * px);
            draw->AddText({tip.x + 2.0f * px, tip.y - 7.0f * px}, colour, axisName[i]);
        }
        if (cloth.panels.empty())
            draw->AddText({origin.x + 8.0f * px, origin.y + 46.0f * px}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          doc.panels().empty() ? "Draw a panel on the table to see it here." : "No cloth to show yet.");
        draw->AddText({origin.x + 8.0f * px, origin.y + 6.0f * px}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                      "3D: drag to orbit, right-drag to pan, wheel to zoom.\nDrag the selected panel or its gizmo arrows to move it.");
    }
    draw->PopClipRect();
    return events;
}

} // namespace clothcad::app
