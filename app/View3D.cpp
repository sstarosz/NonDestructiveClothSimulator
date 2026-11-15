#include "app/View3D.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "app/Style.h"

namespace clothcad::app {

namespace {

using sim::Vec3;

constexpr double nearPlane = 2.0;  // cm: nothing closer to the camera is drawn
constexpr double fieldOfView = 40.0 * 3.14159265358979323846 / 180.0;

// The most pixels the picture is worked out at; larger views are stretched from this.
constexpr float mostPixels = 1.5e6f;

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

// Fills a triangle of the picture, keeping for each pixel whatever is nearest.
void View3D::fill(const Corner& a, const Corner& b, const Corner& c, std::uint32_t thing) {
    const float whole = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (std::abs(whole) < 1e-6f) return;
    const int x0 = std::max(static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))), 0), x1 = std::min(static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))), width_ - 1);
    const int y0 = std::max(static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))), 0), y1 = std::min(static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))), height_ - 1);
    if (x0 > x1 || y0 > y1) return;
    const float inverse = 1.0f / whole;
    // How much of each corner a pixel has, and how that changes one pixel to the right.
    const float stepA = (b.y - c.y) * inverse, stepB = (c.y - a.y) * inverse;
    for (int y = y0; y <= y1; ++y) {
        const float py = static_cast<float>(y) + 0.5f, px = static_cast<float>(x0) + 0.5f;
        float wa = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) * inverse;
        float wb = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) * inverse;
        std::size_t at = static_cast<std::size_t>(y) * width_ + x0;
        for (int x = x0; x <= x1; ++x, ++at, wa += stepA, wb += stepB) {
            const float wc = 1.0f - wa - wb;
            if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f) continue;
            const float nearness = wa * a.nearness + wb * b.nearness + wc * c.nearness;
            if (nearness <= nearness_[at]) continue;
            nearness_[at] = nearness;
            thing_[at] = thing;
            const float red = wa * a.red + wb * b.red + wc * c.red, green = wa * a.green + wb * b.green + wc * c.green, blue = wa * a.blue + wb * b.blue + wc * c.blue;
            colour_[at] = IM_COL32(static_cast<int>(std::clamp(red, 0.0f, 1.0f) * 255.0f), static_cast<int>(std::clamp(green, 0.0f, 1.0f) * 255.0f),
                                   static_cast<int>(std::clamp(blue, 0.0f, 1.0f) * 255.0f), 255);
        }
    }
}

// Draws a line in space into the picture, hidden where something nearer is
// in the way. `lift` above 1 brings it a little toward the camera, for lines
// lying on a surface. The colour's alpha blends it over what is there.
void View3D::stroke(Vec3 from, Vec3 to, std::uint32_t colour, float lift, float scale) {
    double depthFrom = sim::dot(from - camera_.eye, camera_.forward), depthTo = sim::dot(to - camera_.eye, camera_.forward);
    if (depthFrom < nearPlane && depthTo < nearPlane) return;
    if (depthFrom < nearPlane) {
        from = from + (to - from) * ((nearPlane - depthFrom) / (depthTo - depthFrom));
        depthFrom = nearPlane;
    } else if (depthTo < nearPlane) {
        to = to + (from - to) * ((nearPlane - depthTo) / (depthFrom - depthTo));
        depthTo = nearPlane;
    }
    const float halfWidth = static_cast<float>(width_) / 2.0f, halfHeight = static_cast<float>(height_) / 2.0f, focal = camera_.focal * scale;
    const auto across = [&](Vec3 p, double depth) { return halfWidth + static_cast<float>(sim::dot(p - camera_.eye, camera_.right) / depth) * focal; };
    const auto down = [&](Vec3 p, double depth) { return halfHeight - static_cast<float>(sim::dot(p - camera_.eye, camera_.up) / depth) * focal; };
    float x = across(from, depthFrom), y = down(from, depthFrom), nearness = static_cast<float>(1.0 / depthFrom) * lift;
    const float x1 = across(to, depthTo), y1 = down(to, depthTo), nearness1 = static_cast<float>(1.0 / depthTo) * lift;
    // Keep only the part inside the picture, so a line running far off it costs nothing.
    float t0 = 0.0f, t1 = 1.0f;
    const auto clip = [&](float start, float change, float low, float high) {
        if (std::abs(change) < 1e-9f) return start >= low && start <= high;
        float in = (low - start) / change, out = (high - start) / change;
        if (in > out) std::swap(in, out);
        t0 = std::max(t0, in);
        t1 = std::min(t1, out);
        return t0 <= t1;
    };
    if (!clip(x, x1 - x, 0.0f, static_cast<float>(width_ - 1)) || !clip(y, y1 - y, 0.0f, static_cast<float>(height_ - 1))) return;
    const float dx = x1 - x, dy = y1 - y, dn = nearness1 - nearness;
    const int steps = std::max(static_cast<int>(std::max(std::abs(dx), std::abs(dy)) * (t1 - t0)) + 1, 1);
    const float alpha = static_cast<float>(colour >> 24) / 255.0f;
    const float red = static_cast<float>(colour & 255), green = static_cast<float>((colour >> 8) & 255), blue = static_cast<float>((colour >> 16) & 255);
    for (int i = 0; i <= steps; ++i) {
        const float t = t0 + (t1 - t0) * static_cast<float>(i) / static_cast<float>(steps);
        const int px = static_cast<int>(x + dx * t), py = static_cast<int>(y + dy * t);
        if (px < 0 || py < 0 || px >= width_ || py >= height_) continue;
        const std::size_t at = static_cast<std::size_t>(py) * width_ + px;
        if (nearness + dn * t < nearness_[at]) continue;
        const std::uint32_t under = colour_[at];
        colour_[at] = IM_COL32(static_cast<int>(static_cast<float>(under & 255) * (1.0f - alpha) + red * alpha),
                               static_cast<int>(static_cast<float>((under >> 8) & 255) * (1.0f - alpha) + green * alpha),
                               static_cast<int>(static_cast<float>((under >> 16) & 255) * (1.0f - alpha) + blue * alpha), 255);
    }
}

std::optional<View3D::ScreenPoint> View3D::toScreen(Vec3 point) const {
    const Vec3 from = point - camera_.eye;
    const double depth = sim::dot(from, camera_.forward);
    if (depth < nearPlane) return std::nullopt;
    return ScreenPoint{camera_.centreX + static_cast<float>(sim::dot(from, camera_.right) / depth) * camera_.focal,
                       camera_.centreY - static_cast<float>(sim::dot(from, camera_.up) / depth) * camera_.focal};
}

View3DEvents View3D::draw(const Document& doc, const sim::ClothMesh& cloth, const sim::Placement& placement,
                          const sim::Surface* body, std::optional<PanelId> selected, bool showWire) {
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

    // ---- The picture: as many pixels as the view, up to a limit ----
    const float scale = std::min(1.0f, std::sqrt(mostPixels / (size.x * size.y)));
    width_ = std::max(static_cast<int>(size.x * scale), 8);
    height_ = std::max(static_cast<int>(size.y * scale), 8);
    const std::size_t pixels = static_cast<std::size_t>(width_) * height_;
    colour_.assign(pixels, IM_COL32(24, 27, 34, 255));
    nearness_.assign(pixels, 0.0f);
    thing_.assign(pixels, 0u);
    const float toPictureX = static_cast<float>(width_) / size.x, toPictureY = static_cast<float>(height_) / size.y;

    // A point in space as a corner in the picture; empty behind the camera.
    const auto corner3 = [&](Vec3 p, ImVec4 colour, double light) -> std::optional<Corner> {
        const Vec3 from = p - camera_.eye;
        const double depth = sim::dot(from, camera_.forward);
        if (depth < nearPlane) return std::nullopt;
        const float l = static_cast<float>(std::clamp(light, 0.0, 1.0));
        return Corner{(size.x / 2.0f + static_cast<float>(sim::dot(from, camera_.right) / depth) * camera_.focal) * toPictureX,
                      (size.y / 2.0f - static_cast<float>(sim::dot(from, camera_.up) / depth) * camera_.focal) * toPictureY,
                      static_cast<float>(1.0 / depth), colour.x * l, colour.y * l, colour.z * l};
    };
    const Vec3 light = sim::normalized(camera_.forward * -1.0 + camera_.up * 0.35 + camera_.right * 0.25);
    const auto lit = [&](Vec3 normal, double dark) { return dark + (1.0 - dark) * std::abs(sim::dot(normal, light)); };

    // ---- The floor: a grid every 10 cm, heavier every metre ----
    {
        const double reach = std::clamp(std::ceil(distance_ * 1.2 / 100.0) * 100.0, 200.0, 2000.0);
        const double step = reach > 800.0 ? 100.0 : 10.0;
        const double centreX = std::round(target_.x / 100.0) * 100.0, centreZ = std::round(target_.z / 100.0) * 100.0;
        for (double at = -reach; at <= reach + 1e-9; at += step) {
            const bool metre = std::abs(std::remainder(at, 100.0)) < 1e-6;
            const ImU32 colour = metre ? IM_COL32(255, 255, 255, 46) : IM_COL32(255, 255, 255, 16);
            stroke({centreX + at, 0.0, centreZ - reach}, {centreX + at, 0.0, centreZ + reach}, colour, 1.0f, scale);
            stroke({centreX - reach, 0.0, centreZ + at}, {centreX + reach, 0.0, centreZ + at}, colour, 1.0f, scale);
        }
    }

    // ---- The body ----
    constexpr std::uint32_t bodyThing = 0xFFFFFFFFu;
    if (body) {
        if (shadedBody_ != body || shadedBodySize_ != body->triangles.size()) {
            // Each point is shaded by the average direction of the triangles around it, so the surface looks smooth.
            bodyNormals_.assign(body->points.size(), Vec3{});
            for (const std::array<uint32_t, 3>& t : body->triangles) {
                const Vec3 normal = sim::cross(body->points[t[1]] - body->points[t[0]], body->points[t[2]] - body->points[t[0]]);
                for (uint32_t c : t) bodyNormals_[c] = bodyNormals_[c] + normal;
            }
            for (Vec3& n : bodyNormals_) n = sim::normalized(n);
            shadedBody_ = body;
            shadedBodySize_ = body->triangles.size();
        }
        const ImVec4 skin{0.62f, 0.59f, 0.57f, 1.0f};
        for (const std::array<uint32_t, 3>& t : body->triangles) {
            const Vec3 a = body->points[t[0]], b = body->points[t[1]], c = body->points[t[2]];
            if (sim::dot(sim::cross(b - a, c - a), camera_.eye - a) <= 0.0) continue;  // facing away: its near side hides it
            const std::optional<Corner> ca = corner3(a, skin, lit(bodyNormals_[t[0]], 0.34)), cb = corner3(b, skin, lit(bodyNormals_[t[1]], 0.34)),
                                        cc = corner3(c, skin, lit(bodyNormals_[t[2]], 0.34));
            if (ca && cb && cc) fill(*ca, *cb, *cc, bodyThing);
        }
    }

    // ---- The cloth ----
    const ImVec4 front{0.80f, 0.82f, 0.88f, 1.0f}, back{0.56f, 0.48f, 0.53f, 1.0f};
    const ImVec4 frontChosen{1.00f, 0.80f, 0.45f, 1.0f}, backChosen{0.72f, 0.52f, 0.30f, 1.0f};
    std::vector<Vec3> normals;
    for (std::size_t i = 0; i < cloth.panels.size() && i < placement.size(); ++i) {
        const sim::PanelMesh& mesh = cloth.panels[i];
        const std::vector<Vec3>& positions = placement[i];
        if (positions.size() != mesh.points.size()) continue;
        const bool chosen = selected && mesh.panel == *selected;
        normals.assign(positions.size(), Vec3{});
        for (const std::array<uint32_t, 3>& t : mesh.triangles) {
            const Vec3 normal = sim::cross(positions[t[1]] - positions[t[0]], positions[t[2]] - positions[t[0]]);
            for (uint32_t c : t) normals[c] = normals[c] + normal;
        }
        for (Vec3& n : normals) n = sim::normalized(n);
        for (const std::array<uint32_t, 3>& t : mesh.triangles) {
            const Vec3 a = positions[t[0]], b = positions[t[1]], c = positions[t[2]];
            const bool facing = sim::dot(sim::cross(b - a, c - a), camera_.eye - a) > 0.0;
            const ImVec4 base = facing ? (chosen ? frontChosen : front) : (chosen ? backChosen : back);
            const std::optional<Corner> ca = corner3(a, base, lit(normals[t[0]], 0.40)), cb = corner3(b, base, lit(normals[t[1]], 0.40)),
                                        cc = corner3(c, base, lit(normals[t[2]], 0.40));
            if (ca && cb && cc) fill(*ca, *cb, *cc, static_cast<std::uint32_t>(i) + 1u);
        }
        if (showWire)
            for (const std::array<uint32_t, 3>& t : mesh.triangles)
                for (int k = 0; k < 3; ++k)
                    if (t[k] < t[(k + 1) % 3]) stroke(positions[t[k]], positions[t[(k + 1) % 3]], IM_COL32(20, 22, 28, 110), 1.004f, scale);
    }

    // ---- Stitches: a thread between each pair of sewn particles ----
    const ImU32 thread = (ImGui::GetColorU32(color::seam) & 0x00FFFFFF) | 0xB0000000;
    for (const sim::Stitch& stitch : cloth.stitches) {
        if (stitch.panelA >= placement.size() || stitch.panelB >= placement.size()) continue;
        const std::vector<Vec3>& a = placement[stitch.panelA];
        const std::vector<Vec3>& b = placement[stitch.panelB];
        if (stitch.pointA >= a.size() || stitch.pointB >= b.size()) continue;
        stroke(a[stitch.pointA], b[stitch.pointB], thread, 1.004f, scale);
    }

    texture_.set(colour_.data(), width_, height_);
    draw->AddImage(texture_.id(), origin, corner);
    draw->PushClipRect(origin, corner, true);

    // What is drawn at a place on the screen: 0 nothing, 1 + a cloth panel's index, or the body.
    const auto thingUnder = [&](ImVec2 at) -> std::uint32_t {
        const int x = static_cast<int>((at.x - origin.x) * toPictureX), y = static_cast<int>((at.y - origin.y) * toPictureY);
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0u;
        return thing_[static_cast<std::size_t>(y) * width_ + x];
    };
    const auto selectedUnder = [&](ImVec2 at) {
        const std::uint32_t thing = thingUnder(at);
        return selected && thing != 0u && thing != bodyThing && thing - 1u < cloth.panels.size() && cloth.panels[thing - 1u].panel == *selected;
    };
    const auto depthUnder = [&](ImVec2 at) {
        const int x = std::clamp(static_cast<int>((at.x - origin.x) * toPictureX), 0, width_ - 1), y = std::clamp(static_cast<int>((at.y - origin.y) * toPictureY), 0, height_ - 1);
        const float nearness = nearness_[static_cast<std::size_t>(y) * width_ + x];
        return nearness > 0.0f ? 1.0 / nearness : distance_;
    };

    // ---- The mouse: carry the selected panel, or orbit, pan and zoom ----
    if (ImGui::IsItemActivated()) {
        dragged_ = false;
        carrying_.reset();
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && selectedUnder(io.MousePos))
            carrying_ = Carrying{*selected, io.MousePos.x, io.MousePos.y, depthUnder(io.MousePos), false};
    }
    if (carrying_) {
        const bool released = !ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool moving = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f * px);
        std::optional<DragPhase> phase;
        if (released && carrying_->moved) phase = DragPhase::End;
        else if (!released && moving) phase = carrying_->moved ? DragPhase::Continue : DragPhase::Begin;
        if (phase) {
            const double perPixel = carrying_->depth / camera_.focal;
            Vec3 by = camera_.right * ((io.MousePos.x - carrying_->pressedX) * perPixel) + camera_.up * (-(io.MousePos.y - carrying_->pressedY) * perPixel);
            const double step = io.KeyCtrl ? 1.0 : 0.1;  // whole centimetres with Ctrl, else millimetres
            by = {std::round(by.x / step) * step, std::round(by.y / step) * step, std::round(by.z / step) * step};
            events.carried = CarryPanel{carrying_->panel, by, *phase};
            char label[64];
            std::snprintf(label, sizeof label, "%+.1f, %+.1f, %+.1f cm", by.x, by.y, by.z);
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
    if (hovered && !held && selectedUnder(io.MousePos)) ImGui::SetTooltip("Drag to carry this panel. Turn and wrap it in the details.");

    // A click (not a drag) picks the nearest panel under the cursor.
    if (ImGui::IsItemDeactivated() && !dragged_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        events.clicked = true;
        if (const std::uint32_t thing = thingUnder(io.MousePos); thing != 0u && thing != bodyThing && thing - 1u < cloth.panels.size())
            events.panel = cloth.panels[thing - 1u].panel;
    }

    // ---- Which way is which, and what the mouse does ----
    {
        const ImVec2 at{origin.x + 34.0f * px, corner.y - 34.0f * px};
        const struct {
            Vec3 axis;
            ImU32 colour;
            const char* name;
        } axes[] = {{{1, 0, 0}, IM_COL32(230, 100, 100, 255), "x"}, {{0, 1, 0}, IM_COL32(120, 210, 120, 255), "y"}, {{0, 0, 1}, IM_COL32(110, 150, 240, 255), "z"}};
        for (const auto& axis : axes) {
            const ImVec2 tip{at.x + static_cast<float>(sim::dot(axis.axis, camera_.right)) * 22.0f * px,
                             at.y - static_cast<float>(sim::dot(axis.axis, camera_.up)) * 22.0f * px};
            draw->AddLine(at, tip, axis.colour, 2.0f * px);
            draw->AddText({tip.x + 2.0f * px, tip.y - 7.0f * px}, axis.colour, axis.name);
        }
        if (cloth.panels.empty())
            draw->AddText({origin.x + 8.0f * px, origin.y + 46.0f * px}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          doc.panels().empty() ? "Draw a panel on the table to see it here." : "No cloth to show yet.");
        draw->AddText({origin.x + 8.0f * px, origin.y + 6.0f * px}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                      "3D: drag to orbit, right-drag to pan, wheel to zoom.\nDrag the selected panel to carry it.");
    }
    draw->PopClipRect();
    return events;
}

} // namespace clothcad::app
