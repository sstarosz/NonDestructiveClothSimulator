#include "sim/Body.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>

namespace clothcad::sim {

namespace {

// Two round columns side by side make a trunk that is wider than it is
// deep. With their centres 0.9 radii apart, the distance around both is
// about 8.08 radii.
constexpr double apart = 0.9;
double radiusAround(double around) { return around / (2.0 * pi + 2.0 * apart); }

} // namespace

Body mannequin(MannequinSize size) {
    const double h = size.height;
    const double hips = radiusAround(size.hips), waist = radiusAround(size.waist), chest = radiusAround(size.chest);
    // Heights of the body's landmarks, as parts of its height.
    const double ankle = 0.045 * h, knee = 0.28 * h, crotch = 0.47 * h, hip = 0.53 * h, waistLine = 0.62 * h, chestLine = 0.72 * h,
                 shoulder = 0.80 * h, neckTop = 0.88 * h, headTop = h;

    Body body;
    const auto part = [&](Vec3 a, Vec3 b, double radiusA, double radiusB) { body.parts.push_back({a, b, radiusA, radiusB}); };
    const double limb = h / 170.0;  // limb thickness follows height

    for (double side : {-1.0, 1.0}) {
        // Trunk: one column each side, hips to waist to chest to shoulders.
        const auto column = [&](double radius) { return side * radius * apart / 2.0; };
        part({column(hips), hip, 0}, {column(waist), waistLine, 0}, hips, waist);
        part({column(waist), waistLine, 0}, {column(chest), chestLine, 0}, waist, chest);
        part({column(chest), chestLine, 0}, {column(chest) * 1.25, shoulder, 0}, chest, chest * 0.62);

        // Leg and foot.
        const double legX = side * (hips * apart / 2.0 + 0.2 * hips);
        part({legX, crotch + 4.0 * limb, 0}, {legX, knee, 0}, hips * 0.74, 5.6 * limb);
        part({legX, knee, 0}, {legX, ankle + 3.0 * limb, 0}, 5.6 * limb, 3.6 * limb);
        part({legX, ankle, 0}, {legX, 3.0 * limb, 13.0 * limb}, 3.6 * limb, 3.0 * limb);

        // Arm, held away from the side, and hand.
        const Vec3 shoulderJoint{side * (chest * (apart / 2.0 + 1.0) + 1.0 * limb), shoulder - 1.5 * limb, 0};
        const Vec3 elbow{shoulderJoint.x + side * 11.0 * limb, shoulderJoint.y - 0.165 * h, 0};
        const Vec3 wrist{elbow.x + side * 9.0 * limb, elbow.y - 0.14 * h, 3.0 * limb};
        part(shoulderJoint, elbow, 4.9 * limb, 4.1 * limb);
        part(elbow, wrist, 4.1 * limb, 2.9 * limb);
        part(wrist, {wrist.x + side * 3.0 * limb, wrist.y - 0.06 * h, wrist.z + 1.5 * limb}, 2.9 * limb, 3.3 * limb);
    }
    // Neck and head.
    part({0, shoulder, 0}, {0, neckTop, 0.5 * limb}, 5.6 * limb, 5.2 * limb);
    const double head = (headTop - neckTop) * 0.36;
    part({0, neckTop + head * 0.9, 1.0 * limb}, {0, headTop - head, 1.0 * limb}, head * 0.95, head);
    return body;
}

Contact contactWith(const Capsule& c, Vec3 point) {
    const Vec3 axis = c.b - c.a;
    const double lengthSquared = dot(axis, axis);
    const double t = lengthSquared > 0.0 ? std::clamp(dot(point - c.a, axis) / lengthSquared, 0.0, 1.0) : 0.0;
    const double radius = c.radiusA + (c.radiusB - c.radiusA) * t;
    const Vec3 from = point - (c.a + axis * t);
    const double away = length(from);
    // Exactly on the axis there is no way out to prefer: take any direction square to it.
    Vec3 outward = away > 1e-9 ? from * (1.0 / away) : normalized(cross(axis, std::abs(axis.y) < 0.9 * length(axis) ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
    if (away <= 1e-9 && lengthSquared <= 0.0) outward = {0.0, 1.0, 0.0};
    return {away - radius, outward};
}

double distanceTo(const Body& body, Vec3 point) {
    double nearest = 1e30;
    for (const Capsule& c : body.parts) nearest = std::min(nearest, contactWith(c, point).distance);
    if (body.scanned) nearest = std::min(nearest, body.scanned->contact(point).distance);
    return nearest;
}

Surface surfaceOf(const Body& body, int around) {
    around = std::max(around, 6);
    const int rings = 4;  // from an end's pole to its widest ring
    Surface surface;
    for (const Capsule& c : body.parts) {
        Vec3 along = c.b - c.a;
        const double span = length(along);
        along = span > 1e-9 ? along * (1.0 / span) : Vec3{0, 1, 0};
        // Two directions square to the limb.
        const Vec3 helper = std::abs(along.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        const Vec3 u = normalized(cross(helper, along)), v = cross(along, u);

        // Rings of points from the pole at a to the pole at b.
        struct Ring {
            Vec3 centre;
            double radius;
        };
        std::vector<Ring> profile;
        for (int k = 1; k <= rings; ++k) {
            const double angle = (pi / 2.0) * k / rings;
            profile.push_back({c.a - along * (c.radiusA * std::cos(angle)), c.radiusA * std::sin(angle)});
        }
        for (int k = rings; k >= 1; --k) {
            const double angle = (pi / 2.0) * k / rings;
            profile.push_back({c.b + along * (c.radiusB * std::cos(angle)), c.radiusB * std::sin(angle)});
        }
        const uint32_t first = static_cast<uint32_t>(surface.points.size());
        surface.points.push_back(c.a - along * c.radiusA);
        for (const Ring& ring : profile)
            for (int i = 0; i < around; ++i) {
                const double angle = 2.0 * pi * i / around;
                surface.points.push_back(ring.centre + (u * std::cos(angle) + v * std::sin(angle)) * ring.radius);
            }
        const uint32_t last = static_cast<uint32_t>(surface.points.size());
        surface.points.push_back(c.b + along * c.radiusB);

        const auto at = [&](std::size_t ring, int i) { return first + 1 + static_cast<uint32_t>(ring) * around + static_cast<uint32_t>(i % around); };
        for (int i = 0; i < around; ++i) {
            surface.triangles.push_back({first, at(0, i + 1), at(0, i)});
            for (std::size_t r = 0; r + 1 < profile.size(); ++r) {
                surface.triangles.push_back({at(r, i), at(r, i + 1), at(r + 1, i + 1)});
                surface.triangles.push_back({at(r, i), at(r + 1, i + 1), at(r + 1, i)});
            }
            surface.triangles.push_back({last, at(profile.size() - 1, i), at(profile.size() - 1, i + 1)});
        }
    }
    return surface;
}

// ---- A body from a file ----------------------------------------------------

LoadedSurface loadObj(const std::string& path) {
    LoadedSurface loaded;
    std::ifstream file(path);
    if (!file) {
        loaded.problem = "could not open \"" + path + "\"";
        return loaded;
    }
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        std::istringstream words(line);
        std::string kind;
        if (!(words >> kind)) continue;
        if (kind == "v") {
            Vec3 p;
            if (!(words >> p.x >> p.y >> p.z)) {
                loaded.problem = "line " + std::to_string(lineNumber) + ": a vertex needs three numbers";
                return loaded;
            }
            loaded.surface.points.push_back(p);
        } else if (kind == "f") {
            // Each corner is "vertex", "vertex/texture" or "vertex/texture/normal"; only the vertex is used.
            std::vector<uint32_t> corners;
            std::string corner;
            while (words >> corner) {
                long long index = 0;
                try {
                    index = std::stoll(corner.substr(0, corner.find('/')));
                } catch (const std::exception&) {
                    index = 0;
                }
                const long long count = static_cast<long long>(loaded.surface.points.size());
                if (index < 0) index = count + index + 1;  // counted back from the latest vertex
                if (index < 1 || index > count) {
                    loaded.problem = "line " + std::to_string(lineNumber) + ": a face names a vertex that is not there";
                    return loaded;
                }
                corners.push_back(static_cast<uint32_t>(index - 1));
            }
            for (std::size_t i = 2; i < corners.size(); ++i) loaded.surface.triangles.push_back({corners[0], corners[i - 1], corners[i]});
        }
    }
    if (loaded.surface.triangles.empty()) loaded.problem = "\"" + path + "\" has no faces";
    return loaded;
}

namespace {

// The point of triangle abc nearest to p.
Vec3 nearestOn(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;
    const Vec3 bp = p - b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + ab * (d1 / (d1 - d3));
    const Vec3 cp = p - c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double scale = 1.0 / (va + vb + vc);
    return a + ab * (vb * scale) + ac * (vc * scale);
}

} // namespace

DistanceField::DistanceField(const Surface& surface, double cell, double band) : cell_(std::max(cell, 0.05)), band_(std::max(band, 2.0 * cell)) {
    if (surface.points.empty() || surface.triangles.empty()) return;
    Vec3 low = surface.points.front(), high = low;
    for (const Vec3& p : surface.points) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    const double margin = band_ + 2.0 * cell_;
    origin_ = low - Vec3{margin, margin, margin};
    nx_ = static_cast<int>(std::ceil((high.x - low.x + 2.0 * margin) / cell_)) + 1;
    ny_ = static_cast<int>(std::ceil((high.y - low.y + 2.0 * margin) / cell_)) + 1;
    nz_ = static_cast<int>(std::ceil((high.z - low.z + 2.0 * margin) / cell_)) + 1;
    if (static_cast<double>(nx_) * ny_ * nz_ > 6.0e7) {  // too fine for a body this large: coarsen
        const double coarser = std::cbrt(static_cast<double>(nx_) * ny_ * nz_ / 6.0e7);
        *this = DistanceField(surface, cell_ * coarser * 1.01, band_);
        return;
    }
    const float unknown = std::numeric_limits<float>::quiet_NaN();
    distance_.assign(static_cast<std::size_t>(nx_) * ny_ * nz_, unknown);
    // How squarely each grid point faces its nearest triangle so far: between
    // two triangles equally near (across their shared edge), the one faced
    // more squarely tells the side correctly.
    std::vector<float> facing(distance_.size(), 0.0f);

    for (const std::array<uint32_t, 3>& t : surface.triangles) {
        const Vec3 a = surface.points[t[0]], b = surface.points[t[1]], c = surface.points[t[2]];
        const Vec3 normal = normalized(cross(b - a, c - a));
        if (length(normal) < 0.5) continue;  // a triangle with no area
        const auto index = [&](double v, double from, int most) { return std::clamp(static_cast<int>(std::floor((v - from) / cell_)), 0, most - 1); };
        const int x0 = index(std::min({a.x, b.x, c.x}) - band_, origin_.x, nx_), x1 = index(std::max({a.x, b.x, c.x}) + band_, origin_.x, nx_) + 1;
        const int y0 = index(std::min({a.y, b.y, c.y}) - band_, origin_.y, ny_), y1 = index(std::max({a.y, b.y, c.y}) + band_, origin_.y, ny_) + 1;
        const int z0 = index(std::min({a.z, b.z, c.z}) - band_, origin_.z, nz_), z1 = index(std::max({a.z, b.z, c.z}) + band_, origin_.z, nz_) + 1;
        for (int z = z0; z <= std::min(z1, nz_ - 1); ++z)
            for (int y = y0; y <= std::min(y1, ny_ - 1); ++y)
                for (int x = x0; x <= std::min(x1, nx_ - 1); ++x) {
                    const Vec3 p = origin_ + Vec3{x * cell_, y * cell_, z * cell_};
                    const Vec3 from = p - nearestOn(p, a, b, c);
                    const double away = length(from);
                    if (away > band_) continue;
                    const std::size_t i = (static_cast<std::size_t>(z) * ny_ + y) * nx_ + x;
                    const double squarely = std::abs(dot(from, normal));
                    const double sofar = std::abs(distance_[i]);
                    const bool nearer = std::isnan(distance_[i]) || away < sofar - 1e-6;
                    const bool asNear = !nearer && away < sofar + 1e-6;
                    if (nearer || (asNear && squarely > facing[i])) {
                        distance_[i] = static_cast<float>(dot(from, normal) >= 0.0 ? away : -away);
                        facing[i] = static_cast<float>(squarely);
                    }
                }
    }

    // Everything further than the band from the surface: outside if it can be
    // reached from a corner of the grid through points that are outside,
    // inside otherwise.
    std::vector<char> seen(distance_.size(), 0);
    std::vector<uint32_t> reached{0};
    seen[0] = 1;
    distance_[0] = static_cast<float>(band_);
    while (!reached.empty()) {
        const uint32_t from = reached.back();
        reached.pop_back();
        const int x = static_cast<int>(from % nx_), y = static_cast<int>((from / nx_) % ny_), z = static_cast<int>(from / (static_cast<std::size_t>(nx_) * ny_));
        const int next[6][3] = {{x - 1, y, z}, {x + 1, y, z}, {x, y - 1, z}, {x, y + 1, z}, {x, y, z - 1}, {x, y, z + 1}};
        for (const auto& n : next) {
            if (n[0] < 0 || n[1] < 0 || n[2] < 0 || n[0] >= nx_ || n[1] >= ny_ || n[2] >= nz_) continue;
            const std::size_t i = (static_cast<std::size_t>(n[2]) * ny_ + n[1]) * nx_ + n[0];
            if (seen[i]) continue;
            if (std::isnan(distance_[i])) distance_[i] = static_cast<float>(band_);
            else if (distance_[i] <= 0.0f) continue;  // the surface: no way through
            seen[i] = 1;
            reached.push_back(static_cast<uint32_t>(i));
        }
    }
    for (float& d : distance_)
        if (std::isnan(d)) d = static_cast<float>(-band_);
}

Contact DistanceField::contact(Vec3 point) const {
    Contact contact;
    if (distance_.empty()) return contact;
    const Vec3 local = (point - origin_) * (1.0 / cell_);
    if (local.x < 0.0 || local.y < 0.0 || local.z < 0.0 || local.x >= nx_ - 1 || local.y >= ny_ - 1 || local.z >= nz_ - 1) return contact;  // far away
    const int x = static_cast<int>(local.x), y = static_cast<int>(local.y), z = static_cast<int>(local.z);
    const double fx = local.x - x, fy = local.y - y, fz = local.z - z;
    const double c000 = at(x, y, z), c100 = at(x + 1, y, z), c010 = at(x, y + 1, z), c110 = at(x + 1, y + 1, z);
    const double c001 = at(x, y, z + 1), c101 = at(x + 1, y, z + 1), c011 = at(x, y + 1, z + 1), c111 = at(x + 1, y + 1, z + 1);
    const auto mix = [](double a, double b, double t) { return a + (b - a) * t; };
    contact.distance = mix(mix(mix(c000, c100, fx), mix(c010, c110, fx), fy), mix(mix(c001, c101, fx), mix(c011, c111, fx), fy), fz);
    // The way out is the way the distance grows.
    const Vec3 grows{mix(mix(c100 - c000, c110 - c010, fy), mix(c101 - c001, c111 - c011, fy), fz),
                     mix(mix(c010 - c000, c110 - c100, fx), mix(c011 - c001, c111 - c101, fx), fz),
                     mix(mix(c001 - c000, c101 - c100, fx), mix(c011 - c010, c111 - c110, fx), fy)};
    const double steep = length(grows);
    if (steep > 1e-6) contact.outward = grows * (1.0 / steep);
    return contact;
}

Body bodyFrom(const Surface& surface) {
    Body body;
    body.scanned = std::make_shared<const DistanceField>(surface);
    return body;
}

} // namespace clothcad::sim
