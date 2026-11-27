#include "clothcad/sim/Body.h"

#include "clothcad/core/File.h"
#include "clothcad/sim/AvatarImport.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>

namespace clothcad::sim {

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

// ---- Work spread over the cores --------------------------------------------

namespace {

// All the cores but one, which is left for the window.
std::size_t threadsToUse() {
    const unsigned cores = std::thread::hardware_concurrency();
    return cores > 2 ? cores - 1 : 1;
}

// Calls work(0) ... work(count - 1), each once, on threads that take the next
// as they finish one, and returns when all are done. If one throws (out of
// memory), no more are begun, and the first error comes out here, on the
// caller's thread: one that left another thread would end the program.
template <class Work>
void spread(std::size_t count, const Work& work) {
    std::atomic<std::size_t> next{0};
    std::once_flag once;
    std::exception_ptr failed;
    const auto take = [&] {
        try {
            for (std::size_t i = next++; i < count; i = next++) work(i);
        } catch (...) {
            std::call_once(once, [&] { failed = std::current_exception(); });
            next = count;
        }
    };
    {
        std::vector<std::jthread> others;
        for (std::size_t k = 1; k < std::min(threadsToUse(), count); ++k) others.emplace_back(take);
        take();
    }  // the others are waited for as they go
    if (failed) std::rethrow_exception(failed);
}

} // namespace

// ---- A body from a file ----------------------------------------------------

namespace {

// The words of one line of an OBJ file, read in place: a scanned body has
// millions of lines, so nothing is copied for any of them.
class ObjLine {
public:
    explicit ObjLine(std::string_view line) : at_(line.data()), end_(at_ + line.size()) {}

    // The next word; empty at the end of the line.
    std::string_view word() {
        while (at_ != end_ && blank(*at_)) ++at_;
        const char* const from = at_;
        while (at_ != end_ && !blank(*at_)) ++at_;
        return {from, at_};
    }

private:
    static bool blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }

    const char* at_;
    const char* end_;
};

// The number a word begins with, a "+" before it or not; false if there is none.
template <class Number>
bool startsWith(std::string_view word, Number& value) {
    if (word.starts_with('+')) word.remove_prefix(1);
    return std::from_chars(word.data(), word.data() + word.size(), value).ec == std::errc{};
}

// Calls each(line) for the lines of a text, one by one, until it says false.
template <class Each>
void forLines(std::string_view text, const Each& each) {
    while (!text.empty()) {
        const auto* newline = static_cast<const char*>(std::memchr(text.data(), '\n', text.size()));
        const std::size_t length = newline ? static_cast<std::size_t>(newline - text.data()) : text.size();
        if (!each(text.substr(0, length))) return;
        text.remove_prefix(newline ? length + 1 : length);
    }
}

// A stretch of whole lines of an OBJ file, read by itself, and what it holds.
struct ObjPiece {
    std::string_view text;
    std::size_t lines = 0, points = 0;              // counted before any is read
    std::size_t linesBefore = 0, pointsBefore = 0;  // in the pieces before it
    std::vector<std::array<uint32_t, 3>> triangles;
    std::string problem;
};

void count(ObjPiece& piece) {
    forLines(piece.text, [&](std::string_view line) {
        ++piece.lines;
        if (ObjLine(line).word() == "v") ++piece.points;
        return true;
    });
}

// Reads a piece: its vertices into their places in `points`, which has room for
// the whole file's, and its faces into its own triangles.
void read(ObjPiece& piece, std::vector<Vec3>& points) {
    std::vector<uint32_t> corners;  // of the face being read
    std::size_t lineNumber = piece.linesBefore;
    std::size_t known = piece.pointsBefore;  // vertices up to this line
    const auto refuse = [&](const char* why) {
        piece.problem = "line " + std::to_string(lineNumber) + ": " + why;
        return false;
    };
    forLines(piece.text, [&](std::string_view text) {
        ++lineNumber;
        ObjLine line(text);
        const std::string_view kind = line.word();
        if (kind == "v") {
            Vec3& p = points[known];
            for (double* coordinate : {&p.x, &p.y, &p.z})
                if (!startsWith(line.word(), *coordinate) || !std::isfinite(*coordinate)) return refuse("a vertex needs three numbers");
            ++known;
        } else if (kind == "f") {
            // Each corner is "vertex", "vertex/texture" or "vertex/texture/normal"; only the vertex is used.
            corners.clear();
            for (std::string_view corner = line.word(); !corner.empty(); corner = line.word()) {
                long long index = 0;
                if (!startsWith(corner, index)) index = 0;
                const long long before = static_cast<long long>(known);
                if (index < 0) index = before + index + 1;  // counted back from the latest vertex
                if (index < 1 || index > before) return refuse("a face names a vertex that is not there");
                corners.push_back(static_cast<uint32_t>(index - 1));
            }
            for (std::size_t i = 2; i < corners.size(); ++i) piece.triangles.push_back({corners[0], corners[i - 1], corners[i]});
        }
        return true;
    });
}

} // namespace

LoadedSurface readObj(std::string_view text) {
    // Cut at line ends into pieces of about a megabyte or more, for the cores to read side by side.
    const std::size_t aim = std::max<std::size_t>(text.size() / (4 * threadsToUse()), 1 << 20);
    std::vector<ObjPiece> pieces;
    for (std::string_view rest = text; !rest.empty();) {
        const std::size_t newline = rest.size() > aim ? rest.find('\n', aim) : std::string_view::npos;
        const std::size_t length = newline == std::string_view::npos ? rest.size() : newline + 1;
        pieces.push_back({.text = rest.substr(0, length), .triangles = {}, .problem = {}});
        rest.remove_prefix(length);
    }
    spread(pieces.size(), [&](std::size_t i) { count(pieces[i]); });
    std::size_t lines = 0, points = 0;
    for (ObjPiece& piece : pieces) {
        piece.linesBefore = lines;
        piece.pointsBefore = points;
        lines += piece.lines;
        points += piece.points;
    }

    LoadedSurface loaded;
    loaded.surface.points.resize(points);
    spread(pieces.size(), [&](std::size_t i) { read(pieces[i], loaded.surface.points); });
    std::size_t triangles = 0;
    for (const ObjPiece& piece : pieces) {
        if (!piece.problem.empty()) return {.surface = {}, .problem = piece.problem};  // the first in the file
        triangles += piece.triangles.size();
    }
    loaded.surface.triangles.reserve(triangles);
    for (const ObjPiece& piece : pieces) loaded.surface.triangles.insert(loaded.surface.triangles.end(), piece.triangles.begin(), piece.triangles.end());
    if (loaded.surface.triangles.empty()) loaded.problem = "it has no faces";
    return loaded;
}

LoadedSurface loadObj(const std::string& path) {
    // All of it at once, straight into the string it is read from.
    std::ifstream file(filePath(path), std::ios::binary | std::ios::ate);
    const std::streamoff size = file ? static_cast<std::streamoff>(file.tellg()) : -1;
    if (size < 0) return {.surface = {}, .problem = "could not open \"" + path + "\""};
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(text.data(), size)) return {.surface = {}, .problem = "could not read \"" + path + "\""};
    LoadedSurface loaded = readObj(text);
    if (!loaded.problem.empty()) loaded.problem = "\"" + path + "\": " + loaded.problem;
    return loaded;
}

// ---- The avatar of a document ----------------------------------------------

Avatar avatarOf(const Surface& file, std::string name, const AvatarImport& readAs) {
    const Surface surface = imported(file, readAs);
    Avatar avatar{.name = std::move(name), .points = {}, .triangles = surface.triangles, .readAs = readAs};
    avatar.points.reserve(surface.points.size());
    for (const Vec3& p : surface.points) avatar.points.push_back({p.x, p.y, p.z});
    return avatar;
}

Surface surfaceOf(const Avatar& avatar) {
    Surface surface{.points = {}, .triangles = avatar.triangles};
    surface.points.reserve(avatar.points.size());
    for (const Point3& p : avatar.points) surface.points.push_back({p.x, p.y, p.z});
    return surface;
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

DistanceField::DistanceField(const Surface& surface, double cell, double band, Progress* progress)
    : cell_(std::max(cell, 0.05)), band_(std::max(band, 2.0 * cell)) {
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
        *this = DistanceField(surface, cell_ * coarser * 1.01, band_, progress);
        return;
    }
    const float unknown = std::numeric_limits<float>::quiet_NaN();
    distance_.assign(static_cast<std::size_t>(nx_) * ny_ * nz_, unknown);
    if (!measure(surface, progress)) {  // called off: nothing of it is kept
        distance_.clear();
        return;
    }

    // Everything further than the band from the surface: outside if it can be
    // reached from a corner of the grid through points that are outside,
    // inside otherwise.
    const auto calledOff = [&] { return progress && progress->stop; };
    std::size_t walked = 0;
    std::vector<char> seen(distance_.size(), 0);
    std::vector<uint32_t> reached{0};
    seen[0] = 1;
    distance_[0] = static_cast<float>(band_);
    while (!reached.empty()) {
        if (++walked % 4096 == 0 && calledOff()) {
            distance_.clear();
            return;
        }
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

    // How much the distance changes from one grid point to the next, at most.
    double most = 0.0;
    for (int z = 0; z < nz_; ++z)
        for (int y = 0; y < ny_; ++y)
            for (int x = 0; x < nx_; ++x) {
                const double here = at(x, y, z);
                if (x + 1 < nx_) most = std::max(most, std::abs(at(x + 1, y, z) - here));
                if (y + 1 < ny_) most = std::max(most, std::abs(at(x, y + 1, z) - here));
                if (z + 1 < nz_) most = std::max(most, std::abs(at(x, y, z + 1) - here));
            }
    steepest_ = most / cell_;
    if (progress) progress->done = 1.0f;
}

// Every grid point within the band of a triangle gets its distance to the
// nearest one. The grid is cut into slabs along y, which threads take one at a
// time: each point is worked out by one thread, from the triangles in the
// order the surface has them, so the field is the same however many there are.
bool DistanceField::measure(const Surface& surface, Progress* progress) {
    // How squarely each grid point faces its nearest triangle so far: between
    // two triangles equally near (across their shared edge), the one faced
    // more squarely tells the side correctly.
    std::vector<float> facing(distance_.size(), 0.0f);

    const int height = std::max(1, ny_ / static_cast<int>(threadsToUse() * 4));  // of a slab, in grid points; several per thread, as some take longer
    const int slabs = (ny_ + height - 1) / height;
    const auto index = [&](double v, double from, int most) { return std::clamp(static_cast<int>(std::floor((v - from) / cell_)), 0, most - 1); };
    // Which triangles reach into each slab.
    std::vector<std::vector<uint32_t>> inSlab(static_cast<std::size_t>(slabs));
    std::size_t listed = 0;
    for (std::size_t n = 0; n < surface.triangles.size(); ++n) {
        const std::array<uint32_t, 3>& t = surface.triangles[n];
        const double a = surface.points[t[0]].y, b = surface.points[t[1]].y, c = surface.points[t[2]].y;
        const int y0 = index(std::min({a, b, c}) - band_, origin_.y, ny_), y1 = std::min(index(std::max({a, b, c}) + band_, origin_.y, ny_) + 1, ny_ - 1);
        for (int slab = y0 / height; slab <= y1 / height; ++slab, ++listed) inSlab[static_cast<std::size_t>(slab)].push_back(static_cast<uint32_t>(n));
    }

    std::atomic<std::size_t> finished{0};
    spread(static_cast<std::size_t>(slabs), [&](std::size_t slab) {
        const int first = static_cast<int>(slab) * height, last = std::min(first + height, ny_) - 1;
        std::size_t counted = 0;
        // The last twentieth of the whole is what follows the triangles.
        const auto report = [&] {
            const float now = 0.95f * static_cast<float>(finished += counted) / static_cast<float>(listed);
            counted = 0;
            // Threads report side by side: only ever forwards.
            for (float was = progress->done; was < now && !progress->done.compare_exchange_weak(was, now);) {}
        };
        for (const uint32_t n : inSlab[slab]) {
            if (progress) {
                if (progress->stop) return;
                if (++counted == 512) report();
            }
            const std::array<uint32_t, 3>& t = surface.triangles[n];
            const Vec3 a = surface.points[t[0]], b = surface.points[t[1]], c = surface.points[t[2]];
            const Vec3 normal = normalized(cross(b - a, c - a));
            if (length(normal) < 0.5) continue;  // a triangle with no area
            // A ball round the triangle: no point of it is nearer to a grid
            // point than the ball is, which is quick to ask.
            const Vec3 middle = (a + b + c) * (1.0 / 3.0);
            const double radius = std::max({length(a - middle), length(b - middle), length(c - middle)});
            constexpr double slack = 1e-5;  // more than the stored distances' rounding and the tie's 1e-6
            const double farthest = band_ + radius + slack;
            const int x0 = index(std::min({a.x, b.x, c.x}) - band_, origin_.x, nx_), x1 = index(std::max({a.x, b.x, c.x}) + band_, origin_.x, nx_) + 1;
            const int y0 = index(std::min({a.y, b.y, c.y}) - band_, origin_.y, ny_), y1 = index(std::max({a.y, b.y, c.y}) + band_, origin_.y, ny_) + 1;
            const int z0 = index(std::min({a.z, b.z, c.z}) - band_, origin_.z, nz_), z1 = index(std::max({a.z, b.z, c.z}) + band_, origin_.z, nz_) + 1;
            for (int z = z0; z <= std::min(z1, nz_ - 1); ++z)
                for (int y = std::max(y0, first); y <= std::min(y1, last); ++y) {
                    const double dy = origin_.y + y * cell_ - middle.y, dz = origin_.z + z * cell_ - middle.z;
                    const double across = dy * dy + dz * dz;
                    const double room = farthest * farthest - across;
                    if (room < 0.0) continue;
                    // Only the stretch of the row that the band round the ball reaches.
                    const double half = std::sqrt(room);
                    const int from = std::max(x0, static_cast<int>(std::ceil((middle.x - half - origin_.x) / cell_)));
                    const int to = std::min({x1, nx_ - 1, static_cast<int>(std::floor((middle.x + half - origin_.x) / cell_))});
                    for (int x = from; x <= to; ++x) {
                        const std::size_t i = (static_cast<std::size_t>(z) * ny_ + y) * nx_ + x;
                        const bool unset = std::isnan(distance_[i]);
                        const double sofar = std::abs(distance_[i]);
                        // The ball is farther than the distance found already: nothing here can change it.
                        const double reach = (unset ? band_ : std::min(band_, sofar)) + radius + slack;
                        const double dx = origin_.x + x * cell_ - middle.x;
                        if (dx * dx + across > reach * reach) continue;
                        const Vec3 p = origin_ + Vec3{x * cell_, y * cell_, z * cell_};
                        const Vec3 apart = p - nearestOn(p, a, b, c);
                        const double away = length(apart);
                        if (away > band_) continue;
                        const double squarely = std::abs(dot(apart, normal));
                        const bool nearer = unset || away < sofar - 1e-6;
                        const bool asNear = !nearer && away < sofar + 1e-6;
                        if (nearer || (asNear && squarely > facing[i])) {
                            distance_[i] = static_cast<float>(dot(apart, normal) >= 0.0 ? away : -away);
                            facing[i] = static_cast<float>(squarely);
                        }
                    }
                }
        }
        if (progress) report();
    });
    return !progress || !progress->stop;
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

Body bodyFrom(const Surface& surface, Progress* progress) {
    Body body;
    body.scanned = std::make_shared<const DistanceField>(surface, 1.0_cm, 4.0_cm, progress);
    return body;
}

} // namespace clothcad::sim
