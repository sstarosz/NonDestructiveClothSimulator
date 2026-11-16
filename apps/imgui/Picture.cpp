#include "Picture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <thread>

namespace clothcad::app {

namespace {

using sim::Vec3;

constexpr double nearPlane = 2.0;  // cm: nothing closer to the eye is drawn
constexpr std::uint32_t background = 0xFF221B18u;  // IM_COL32(24, 27, 34, 255)

std::uint32_t colourOf(float red, float green, float blue) {
    const auto byte = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return byte(red) | (byte(green) << 8) | (byte(blue) << 16) | 0xFF000000u;
}

// A point of the scene as it lands in the picture.
struct Spot {
    float x = 0.0f, y = 0.0f, nearness = 0.0f;  // nearness 0: behind the eye
    float light = 0.0f;
};
struct Corner {
    float x, y, nearness;
    float red, green, blue;
};
struct Face {
    Corner a, b, c;
    std::uint32_t thing;
};
struct Stroke {
    float x0, y0, n0, x1, y1, n1;  // ends in the picture, with their nearness
    std::uint32_t colour;          // its alpha blends it over what is there
};

struct Tint {
    float red, green, blue;
};

// Everything the bands need, prepared once.
struct Prepared {
    std::vector<Face> faces;
    std::vector<Stroke> under;  // drawn before the faces: the floor grid
    std::vector<Stroke> over;   // drawn after them, hidden where something nearer is in the way
};

struct Projector {
    const Eye& eye;
    float halfWidth, halfHeight;
    Vec3 light;

    Spot spot(Vec3 p, Vec3 normal, double dark) const {
        const Vec3 from = p - eye.at;
        const double depth = sim::dot(from, eye.forward);
        if (depth < nearPlane) return {};
        return {halfWidth + static_cast<float>(sim::dot(from, eye.right) / depth) * eye.focal, halfHeight - static_cast<float>(sim::dot(from, eye.up) / depth) * eye.focal,
                static_cast<float>(1.0 / depth), static_cast<float>(dark + (1.0 - dark) * std::abs(sim::dot(normal, light)))};
    }
    // A line in space, cut off where it passes behind the eye. `lift` above 1
    // brings it a little toward the eye, for lines lying on a surface.
    void stroke(std::vector<Stroke>& into, Vec3 from, Vec3 to, std::uint32_t colour, float lift) const {
        double depthFrom = sim::dot(from - eye.at, eye.forward), depthTo = sim::dot(to - eye.at, eye.forward);
        if (depthFrom < nearPlane && depthTo < nearPlane) return;
        if (depthFrom < nearPlane) {
            from = from + (to - from) * ((nearPlane - depthFrom) / (depthTo - depthFrom));
            depthFrom = nearPlane;
        } else if (depthTo < nearPlane) {
            to = to + (from - to) * ((nearPlane - depthTo) / (depthFrom - depthTo));
            depthTo = nearPlane;
        }
        const auto across = [&](Vec3 p, double depth) { return halfWidth + static_cast<float>(sim::dot(p - eye.at, eye.right) / depth) * eye.focal; };
        const auto down = [&](Vec3 p, double depth) { return halfHeight - static_cast<float>(sim::dot(p - eye.at, eye.up) / depth) * eye.focal; };
        into.push_back({across(from, depthFrom), down(from, depthFrom), static_cast<float>(1.0 / depthFrom) * lift, across(to, depthTo), down(to, depthTo),
                        static_cast<float>(1.0 / depthTo) * lift, colour});
    }
};

// The rows [firstRow, endRow) of the picture.
struct Band {
    int width, firstRow, endRow;
    std::uint32_t* colour;
    float* nearness;
    std::uint32_t* thing;

    void clear() const {
        const std::size_t from = static_cast<std::size_t>(firstRow) * width, to = static_cast<std::size_t>(endRow) * width;
        std::fill(colour + from, colour + to, background);
        std::fill(nearness + from, nearness + to, 0.0f);
        std::fill(thing + from, thing + to, Picture::nothing);
    }

    // Fills a triangle, keeping for each pixel whatever is nearest.
    void fill(const Face& face) const {
        const Corner& a = face.a;
        const Corner& b = face.b;
        const Corner& c = face.c;
        const int y0 = std::max(static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))), firstRow), y1 = std::min(static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))), endRow - 1);
        if (y0 > y1) return;
        const int x0 = std::max(static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))), 0), x1 = std::min(static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))), width - 1);
        if (x0 > x1) return;
        const float whole = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (std::abs(whole) < 1e-6f) return;
        const float inverse = 1.0f / whole;
        // How much of each corner a pixel has, and how that changes one pixel to the right.
        const float stepA = (b.y - c.y) * inverse, stepB = (c.y - a.y) * inverse;
        // Nearness and colour change evenly across the triangle too.
        const float nearStep = stepA * (a.nearness - c.nearness) + stepB * (b.nearness - c.nearness);
        for (int y = y0; y <= y1; ++y) {
            const float py = static_cast<float>(y) + 0.5f, px = static_cast<float>(x0) + 0.5f;
            float wa = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) * inverse;
            float wb = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) * inverse;
            float closeness = wa * a.nearness + wb * b.nearness + (1.0f - wa - wb) * c.nearness;
            std::size_t at = static_cast<std::size_t>(y) * width + x0;
            for (int x = x0; x <= x1; ++x, ++at, wa += stepA, wb += stepB, closeness += nearStep) {
                const float wc = 1.0f - wa - wb;
                if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f) continue;
                if (closeness <= nearness[at]) continue;
                nearness[at] = closeness;
                thing[at] = face.thing;
                colour[at] = colourOf(wa * a.red + wb * b.red + wc * c.red, wa * a.green + wb * b.green + wc * c.green, wa * a.blue + wb * b.blue + wc * c.blue);
            }
        }
    }

    void draw(const Stroke& s) const {
        // Keep only the part inside the band, so a line running far off it costs nothing.
        float t0 = 0.0f, t1 = 1.0f;
        const auto clip = [&](float start, float change, float low, float high) {
            if (std::abs(change) < 1e-9f) return start >= low && start <= high;
            float in = (low - start) / change, out = (high - start) / change;
            if (in > out) std::swap(in, out);
            t0 = std::max(t0, in);
            t1 = std::min(t1, out);
            return t0 <= t1;
        };
        const float dx = s.x1 - s.x0, dy = s.y1 - s.y0, dn = s.n1 - s.n0;
        if (!clip(s.x0, dx, 0.0f, static_cast<float>(width - 1)) || !clip(s.y0, dy, static_cast<float>(firstRow), static_cast<float>(endRow) - 0.001f)) return;
        const int steps = std::max(static_cast<int>(std::max(std::abs(dx), std::abs(dy)) * (t1 - t0)) + 1, 1);
        const float alpha = static_cast<float>(s.colour >> 24) / 255.0f;
        const float red = static_cast<float>(s.colour & 255), green = static_cast<float>((s.colour >> 8) & 255), blue = static_cast<float>((s.colour >> 16) & 255);
        for (int i = 0; i <= steps; ++i) {
            const float t = t0 + (t1 - t0) * static_cast<float>(i) / static_cast<float>(steps);
            const int px = static_cast<int>(s.x0 + dx * t), py = static_cast<int>(s.y0 + dy * t);
            if (px < 0 || py < firstRow || px >= width || py >= endRow) continue;
            const std::size_t at = static_cast<std::size_t>(py) * width + px;
            if (s.n0 + dn * t < nearness[at]) continue;
            const std::uint32_t was = colour[at];
            const auto mix = [&](std::uint32_t under, float over) { return static_cast<std::uint32_t>(static_cast<float>(under) * (1.0f - alpha) + over * alpha); };
            colour[at] = mix(was & 255, red) | (mix((was >> 8) & 255, green) << 8) | (mix((was >> 16) & 255, blue) << 16) | 0xFF000000u;
        }
    }

    void draw(const Prepared& prepared) const {
        clear();
        for (const Stroke& stroke : prepared.under) draw(stroke);
        for (const Face& face : prepared.faces) fill(face);
        for (const Stroke& stroke : prepared.over) draw(stroke);
    }
};

void shadingDirections(std::vector<Vec3>& normals, const std::vector<Vec3>& points, const std::vector<std::array<uint32_t, 3>>& triangles) {
    // Each point is shaded by the average direction of the triangles around it, so the surface looks smooth.
    normals.assign(points.size(), Vec3{});
    for (const std::array<uint32_t, 3>& t : triangles) {
        const Vec3 normal = sim::cross(points[t[1]] - points[t[0]], points[t[2]] - points[t[0]]);
        for (uint32_t c : t) normals[c] = normals[c] + normal;
    }
    for (Vec3& n : normals) n = sim::normalized(n);
}

Corner tinted(const Spot& spot, Tint tint) { return {spot.x, spot.y, spot.nearness, tint.red * spot.light, tint.green * spot.light, tint.blue * spot.light}; }

} // namespace

std::uint32_t Picture::thingAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return nothing;
    return thing_[static_cast<std::size_t>(y) * width_ + x];
}

std::optional<double> Picture::depthAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return std::nullopt;
    const float nearness = nearness_[static_cast<std::size_t>(y) * width_ + x];
    if (nearness <= 0.0f) return std::nullopt;
    return 1.0 / nearness;
}

void Picture::draw(int width, int height, const Eye& eye, const Scene& scene, unsigned threads) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    const std::size_t pixels = static_cast<std::size_t>(width_) * height_;
    colour_.resize(pixels);
    nearness_.resize(pixels);
    thing_.resize(pixels);

    const Projector project{eye, static_cast<float>(width_) / 2.0f, static_cast<float>(height_) / 2.0f, sim::normalized(eye.forward * -1.0 + eye.up * 0.35 + eye.right * 0.25)};
    Prepared prepared;

    // ---- The floor grid ----
    for (double at = -scene.gridReach; at <= scene.gridReach + 1e-9; at += scene.gridStep) {
        const bool metre = std::abs(std::remainder(at, 100.0)) < 1e-6;
        const std::uint32_t colour = metre ? 0x2EFFFFFFu : 0x10FFFFFFu;
        project.stroke(prepared.under, {scene.gridX + at, 0.0, scene.gridZ - scene.gridReach}, {scene.gridX + at, 0.0, scene.gridZ + scene.gridReach}, colour, 1.0f);
        project.stroke(prepared.under, {scene.gridX - scene.gridReach, 0.0, scene.gridZ + at}, {scene.gridX + scene.gridReach, 0.0, scene.gridZ + at}, colour, 1.0f);
    }

    std::vector<Spot> spots;
    // In the picture y runs downward, so a triangle that faces the eye turns clockwise there.
    const auto facesEye = [](const Spot& a, const Spot& b, const Spot& c) { return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) < 0.0f; };

    // ---- The body ----
    if (scene.body) {
        const sim::Surface& surface = *scene.body;
        if (shadedBody_ != scene.body || shadedBodySize_ != surface.triangles.size()) {
            shadingDirections(bodyNormals_, surface.points, surface.triangles);
            shadedBody_ = scene.body;
            shadedBodySize_ = surface.triangles.size();
        }
        spots.resize(surface.points.size());
        for (std::size_t i = 0; i < surface.points.size(); ++i) spots[i] = project.spot(surface.points[i], bodyNormals_[i], 0.34);
        const Tint skin{0.62f, 0.59f, 0.57f};
        for (const std::array<uint32_t, 3>& t : surface.triangles) {
            const Spot& a = spots[t[0]];
            const Spot& b = spots[t[1]];
            const Spot& c = spots[t[2]];
            if (a.nearness <= 0.0f || b.nearness <= 0.0f || c.nearness <= 0.0f) continue;
            if (!facesEye(a, b, c)) continue;  // facing away: its near side hides it
            prepared.faces.push_back({tinted(a, skin), tinted(b, skin), tinted(c, skin), body});
        }
    }

    // ---- The cloth ----
    if (scene.cloth && scene.placement) {
        const Tint front{0.80f, 0.82f, 0.88f}, back{0.56f, 0.48f, 0.53f}, frontChosen{1.00f, 0.80f, 0.45f}, backChosen{0.72f, 0.52f, 0.30f};
        std::vector<Vec3> normals;
        for (std::size_t i = 0; i < scene.cloth->panels.size() && i < scene.placement->size(); ++i) {
            const sim::PanelMesh& mesh = scene.cloth->panels[i];
            const std::vector<Vec3>& positions = (*scene.placement)[i];
            if (positions.size() != mesh.points.size()) continue;
            const bool chosen = scene.selected && *scene.selected == i;
            shadingDirections(normals, positions, mesh.triangles);
            spots.resize(positions.size());
            for (std::size_t k = 0; k < positions.size(); ++k) spots[k] = project.spot(positions[k], normals[k], 0.40);
            for (const std::array<uint32_t, 3>& t : mesh.triangles) {
                const Spot& a = spots[t[0]];
                const Spot& b = spots[t[1]];
                const Spot& c = spots[t[2]];
                if (a.nearness <= 0.0f || b.nearness <= 0.0f || c.nearness <= 0.0f) continue;
                const Tint tint = facesEye(a, b, c) ? (chosen ? frontChosen : front) : (chosen ? backChosen : back);
                prepared.faces.push_back({tinted(a, tint), tinted(b, tint), tinted(c, tint), static_cast<std::uint32_t>(i) + 1u});
            }
            if (scene.wire)
                for (const std::array<uint32_t, 3>& t : mesh.triangles)
                    for (int k = 0; k < 3; ++k)
                        if (t[k] < t[(k + 1) % 3]) project.stroke(prepared.over, positions[t[k]], positions[t[(k + 1) % 3]], 0x6E1C1614u, 1.004f);
        }
        // Stitches: a thread between each pair of sewn particles.
        for (const sim::Stitch& stitch : scene.cloth->stitches) {
            if (stitch.panelA >= scene.placement->size() || stitch.panelB >= scene.placement->size()) continue;
            const std::vector<Vec3>& a = (*scene.placement)[stitch.panelA];
            const std::vector<Vec3>& b = (*scene.placement)[stitch.panelB];
            if (stitch.pointA >= a.size() || stitch.pointB >= b.size()) continue;
            project.stroke(prepared.over, a[stitch.pointA], b[stitch.pointB], 0xB0FFAD73u, 1.004f);
        }
    }

    // ---- The pixels, in bands: one per thread ----
    if (threads == 0) threads = pixels < 150000 ? 1u : std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
    threads = std::clamp(threads, 1u, static_cast<unsigned>(height_));
    const auto band = [&](unsigned index) { return Band{width_, static_cast<int>(static_cast<long long>(height_) * index / threads), static_cast<int>(static_cast<long long>(height_) * (index + 1) / threads), colour_.data(), nearness_.data(), thing_.data()}; };
    std::vector<std::future<void>> working;
    for (unsigned i = 1; i < threads; ++i) working.push_back(std::async(std::launch::async, [&, i] { band(i).draw(prepared); }));
    band(0).draw(prepared);
    for (std::future<void>& done : working) done.get();
}

} // namespace clothcad::app
