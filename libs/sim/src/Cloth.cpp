#include "clothcad/sim/Cloth.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <map>
#include <utility>

namespace clothcad::sim {

namespace {

constexpr uint32_t none = 0xFFFFFFFFu;
constexpr int subSteps = 30;
constexpr double gravity = 981.0;     // cm per second squared
constexpr double seamSpeed = 40.0;    // cm per second: how fast a stitch's thread is drawn in
constexpr double neighbourly = 2.1;        // particles closer than this many collision distances in the flat pattern do not collide
constexpr double closedWithin = 0.2;   // cm: a stitch this short is closed
constexpr double leadSpeed = 300.0;    // cm per second: how fast a held particle follows the hand
constexpr double givingUpAfter = 3.0;  // seconds after the last thread is drawn in, a stitch that will not close is left as it is
constexpr double heldWhileSewing = 0.03;  // how much of its weight cloth feels while its seams are closing
constexpr double topSpeed = 600.0;    // cm per second: nothing moves faster than this

double distance(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

} // namespace

Cloth::Cloth(const ClothMesh& mesh, const Placement& start, Fabric fabric) : fabric_(fabric) {
    firstOf_.assign(mesh.panels.size(), none);
    positions_.resize(mesh.panels.size());
    for (std::size_t p = 0; p < mesh.panels.size(); ++p) {
        const PanelMesh& panel = mesh.panels[p];
        if (p >= start.size() || start[p].size() != panel.points.size() || panel.triangles.empty()) continue;
        const uint32_t first = static_cast<uint32_t>(at_.size());
        firstOf_[p] = first;
        at_.insert(at_.end(), start[p].begin(), start[p].end());
        panelOf_.insert(panelOf_.end(), panel.points.size(), static_cast<uint32_t>(p));
        flat_.insert(flat_.end(), panel.points.begin(), panel.points.end());
        positions_[p] = start[p];

        // A particle weighs a third of each triangle it is a corner of.
        std::vector<double> mass(panel.points.size(), 0.0);
        // Each side of a triangle keeps its flat length. A side shared by two
        // triangles also keeps their far corners apart, which resists folding.
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> farCorner;
        for (const std::array<uint32_t, 3>& t : panel.triangles) {
            const Vec2 a = panel.points[t[0]], b = panel.points[t[1]], c = panel.points[t[2]];
            const double area = std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2.0;
            for (uint32_t corner : t) mass[corner] += area * fabric_.weight / 3.0;
            for (int i = 0; i < 3; ++i) {
                const uint32_t from = t[i], to = t[(i + 1) % 3], far = t[(i + 2) % 3];
                const auto side = std::minmax(from, to);
                const auto seen = farCorner.find(side);
                if (seen == farCorner.end()) {
                    farCorner.emplace(side, far);
                    edges_.push_back({first + from, first + to, distance(panel.points[from], panel.points[to])});
                } else {
                    folds_.push_back({first + seen->second, first + far, distance(panel.points[seen->second], panel.points[far])});
                }
            }
        }
        for (double m : mass) lightness_.push_back(m > 0.0 ? 1.0 / m : 0.0);
    }
    before_ = at_;
    speed_.assign(at_.size(), Vec3{});
    own_ = lightness_;
    out_.assign(mesh.panels.size(), 0);
    for (const Stitch& stitch : mesh.stitches) {
        if (stitch.panelA >= firstOf_.size() || stitch.panelB >= firstOf_.size()) continue;
        if (firstOf_[stitch.panelA] == none || firstOf_[stitch.panelB] == none) continue;
        const uint32_t a = firstOf_[stitch.panelA] + stitch.pointA, b = firstOf_[stitch.panelB] + stitch.pointB;
        threads_.push_back({a, b, length(at_[b] - at_[a])});
    }

    // Particles are kept six tenths of a typical triangle side apart: close
    // enough that neighbours in the mesh are left alone, far enough that one
    // layer's particles cannot slip between another's.
    checkTheSeams();
    findAlone();
    if (!edges_.empty()) {
        std::vector<double> lengths;
        lengths.reserve(edges_.size());
        for (const Link& edge : edges_) lengths.push_back(edge.rest);
        std::nth_element(lengths.begin(), lengths.begin() + lengths.size() / 2, lengths.end());
        apart_ = 0.6 * lengths[lengths.size() / 2];
    }
}

void Cloth::findAlone() {
    alone_.assign(at_.size(), 0);
    for (const Thread& thread : threads_)
        if (thread.holds) alone_[thread.a] = alone_[thread.b] = 1;
    for (std::size_t i = 0; i < at_.size(); ++i)
        if (out_[panelOf_[i]]) alone_[i] = 1;
}

void Cloth::leaveOut(const std::vector<uint32_t>& panels) {
    std::vector<char> out(out_.size(), 0);
    for (const uint32_t panel : panels)
        if (panel < out.size()) out[panel] = 1;
    if (out == out_) return;
    if (held_ && out[panelOf_[held_->particle]]) letGo();
    for (std::size_t i = 0; i < at_.size(); ++i) {
        const uint32_t panel = panelOf_[i];
        if (out[panel] == out_[panel]) continue;
        lightness_[i] = out[panel] ? 0.0 : own_[i];
        speed_[i] = {};
        before_[i] = at_[i];
    }
    out_ = std::move(out);
    bool joined = false;
    for (Thread& thread : threads_) {
        const bool holds = !out_[panelOf_[thread.a]] && !out_[panelOf_[thread.b]];
        if (holds && !thread.holds) {  // taken back: drawn closed again from where its two sides are
            thread.left = length(at_[thread.b] - at_[thread.a]);
            joined = true;
        }
        thread.holds = holds;
    }
    if (joined) {
        seamsClosed_ = false;
        drawnInAt_ = -1.0;
    }
    findAlone();
}

// Lists the pairs of particles that could come within `apart_` of each other
// during a step of `seconds`, using a grid of cells to look only nearby.
void Cloth::findClosePairs(double seconds) {
    close_.clear();
    if (!fabric_.collidesWithItself || apart_ <= 0.0 || at_.empty()) return;
    double fastest = 0.0;
    for (const Vec3& v : speed_) fastest = std::max(fastest, dot(v, v));
    // Two particles approach at most twice the fastest speed; beyond a few cm of slack the search would cost too much.
    const double range = apart_ + std::min(2.0 * std::sqrt(fastest) * seconds, 2.0 * apart_);
    const double cell = range;

    // Particles sorted by cell: the classic counting sort into a hashed table.
    // Its size is a power of two, so that a slot is found with a mask rather
    // than a division (27 of them for every particle), and it is kept from
    // step to step.
    const std::size_t table = std::bit_ceil(at_.size() * 2 + 1);
    const auto slot = [mask = table - 1](std::int64_t x, std::int64_t y, std::int64_t z) {
        std::uint64_t h = static_cast<std::uint64_t>(x) * 92837111ull ^ static_cast<std::uint64_t>(y) * 689287499ull ^ static_cast<std::uint64_t>(z) * 283923481ull;
        h ^= h >> 29;  // the high bits too, which a mask alone would not see
        return static_cast<std::size_t>(h & mask);
    };
    cellOf_.resize(at_.size());
    for (std::size_t i = 0; i < at_.size(); ++i)
        cellOf_[i] = {static_cast<std::int64_t>(std::floor(at_[i].x / cell)), static_cast<std::int64_t>(std::floor(at_[i].y / cell)),
                      static_cast<std::int64_t>(std::floor(at_[i].z / cell))};
    std::vector<uint32_t>& first = slotFirst_;
    first.assign(table + 1, 0);
    bySlot_.resize(at_.size());
    lookedBy_.assign(table, 0);
    for (const auto& [x, y, z] : cellOf_) ++first[slot(x, y, z)];
    for (std::size_t i = 1; i <= table; ++i) first[i] += first[i - 1];
    for (std::size_t i = at_.size(); i-- > 0;) bySlot_[--first[slot(cellOf_[i][0], cellOf_[i][1], cellOf_[i][2])]] = static_cast<uint32_t>(i);
    const std::vector<uint32_t>& sorted = bySlot_;
    const double neighbours = neighbourly * apart_;

    for (uint32_t i = 0; i < at_.size(); ++i) {
        if (alone_[i]) continue;
        const auto [cx, cy, cz] = cellOf_[i];
        // Two of the 27 cells around can share a slot of the table: it is
        // looked through once, so that no pair is listed, and pushed apart, twice.
        const uint32_t mark = i + 1;
        for (std::int64_t x = cx - 1; x <= cx + 1; ++x)
            for (std::int64_t y = cy - 1; y <= cy + 1; ++y)
                for (std::int64_t z = cz - 1; z <= cz + 1; ++z) {
                    const std::size_t s = slot(x, y, z);
                    if (lookedBy_[s] == mark) continue;
                    lookedBy_[s] = mark;
                    for (uint32_t n = first[s]; n < first[s + 1]; ++n) {
                        const uint32_t j = sorted[n];
                        if (j <= i || alone_[j]) continue;
                        if (lightness_[i] == 0.0 && lightness_[j] == 0.0) continue;
                        const Vec3 between = at_[j] - at_[i];
                        if (dot(between, between) > range * range) continue;
                        // Particles this close in the flat pattern are neighbours, held
                        // apart by the mesh itself: an edge joins them, or nearly.
                        if (panelOf_[i] == panelOf_[j]) {
                            const double fx = flat_[j].x - flat_[i].x, fy = flat_[j].y - flat_[i].y;
                            if (fx * fx + fy * fy < neighbours * neighbours) continue;
                        }
                        close_.push_back({i, j});
                    }
                }
    }
    // A slot can also hold a cell further away: its particles are out of range and left out.
}

namespace {

// Finds, for a spot on a panel, the triangle of a mesh it lies in, and how
// its three corners share it (weights adding up to one). Off the mesh, it is
// the nearest triangle, and the weights carry on beyond it.
class Locator {
public:
    explicit Locator(const PanelMesh& mesh) : mesh_(mesh) {
        if (mesh.points.empty() || mesh.triangles.empty()) return;
        low_ = high_ = mesh.points.front();
        for (const Vec2& p : mesh.points) {
            low_ = {std::min(low_.x, p.x), std::min(low_.y, p.y)};
            high_ = {std::max(high_.x, p.x), std::max(high_.y, p.y)};
        }
        // About two triangles to a cell.
        const double area = std::max((high_.x - low_.x) * (high_.y - low_.y), 1e-9);
        cell_ = std::max(std::sqrt(area / static_cast<double>(mesh.triangles.size())) * 1.5, 1e-6);
        columns_ = std::clamp(static_cast<int>((high_.x - low_.x) / cell_) + 1, 1, 2000);
        rows_ = std::clamp(static_cast<int>((high_.y - low_.y) / cell_) + 1, 1, 2000);
        cells_.resize(static_cast<std::size_t>(columns_) * rows_);
        for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
            const auto& corners = mesh.triangles[t];
            Vec2 from = mesh.points[corners[0]], to = from;
            for (uint32_t c : corners) {
                from = {std::min(from.x, mesh.points[c].x), std::min(from.y, mesh.points[c].y)};
                to = {std::max(to.x, mesh.points[c].x), std::max(to.y, mesh.points[c].y)};
            }
            for (int row = rowOf(from.y); row <= rowOf(to.y); ++row)
                for (int column = columnOf(from.x); column <= columnOf(to.x); ++column)
                    cells_[static_cast<std::size_t>(row) * columns_ + column].push_back(static_cast<uint32_t>(t));
        }
    }

    struct Found {
        uint32_t triangle = 0;
        double weight[3] = {1.0, 0.0, 0.0};
    };
    Found find(Vec2 spot) const {
        Found found;
        if (cells_.empty()) return found;
        if (spot.x >= low_.x && spot.x <= high_.x && spot.y >= low_.y && spot.y <= high_.y)
            for (uint32_t t : cells_[static_cast<std::size_t>(rowOf(spot.y)) * columns_ + columnOf(spot.x)]) {
                weights(t, spot, found.weight);
                if (found.weight[0] >= -1e-9 && found.weight[1] >= -1e-9 && found.weight[2] >= -1e-9) {
                    found.triangle = t;
                    return found;
                }
            }
        // Off the mesh (or in a hole): the triangle that comes nearest.
        double nearest = 1e300;
        for (std::size_t t = 0; t < mesh_.triangles.size(); ++t) {
            const auto& c = mesh_.triangles[t];
            const double away = std::min({toSegment(spot, mesh_.points[c[0]], mesh_.points[c[1]]), toSegment(spot, mesh_.points[c[1]], mesh_.points[c[2]]),
                                          toSegment(spot, mesh_.points[c[2]], mesh_.points[c[0]])});
            if (away < nearest) {
                nearest = away;
                found.triangle = static_cast<uint32_t>(t);
            }
        }
        weights(found.triangle, spot, found.weight);
        return found;
    }

private:
    int columnOf(double x) const { return std::clamp(static_cast<int>((x - low_.x) / cell_), 0, columns_ - 1); }
    int rowOf(double y) const { return std::clamp(static_cast<int>((y - low_.y) / cell_), 0, rows_ - 1); }
    void weights(uint32_t t, Vec2 p, double out[3]) const {
        const auto& c = mesh_.triangles[t];
        const Vec2 a = mesh_.points[c[0]], b = mesh_.points[c[1]], d = mesh_.points[c[2]];
        const double whole = (b.x - a.x) * (d.y - a.y) - (d.x - a.x) * (b.y - a.y);
        if (std::abs(whole) < 1e-14) {
            out[0] = 1.0, out[1] = out[2] = 0.0;
            return;
        }
        out[1] = ((p.x - a.x) * (d.y - a.y) - (d.x - a.x) * (p.y - a.y)) / whole;
        out[2] = ((b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y)) / whole;
        out[0] = 1.0 - out[1] - out[2];
    }
    static double toSegment(Vec2 p, Vec2 a, Vec2 b) {
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double lengthSquared = dx * dx + dy * dy;
        const double t = lengthSquared > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0, 1.0) : 0.0;
        return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
    }

    const PanelMesh& mesh_;
    Vec2 low_, high_;
    double cell_ = 1.0;
    int columns_ = 0, rows_ = 0;
    std::vector<std::vector<uint32_t>> cells_;
};

} // namespace

Cloth Cloth::resumed(const ClothMesh& mesh, const Placement& positions, double time, bool sewn, Fabric fabric) {
    Cloth cloth(mesh, positions, fabric);
    cloth.time_ = time;
    if (sewn) {
        for (Thread& thread : cloth.threads_) thread.left = 0.0;
        cloth.seamsClosed_ = true;
    } else if (cloth.drawnInAt_ >= 0.0) {
        cloth.drawnInAt_ = time;  // the threads were drawn in just now, not at time zero
    }
    return cloth;
}

Cloth Cloth::continued(const ClothMesh& mesh, const Placement& arrangedNow, const Cloth& before, const ClothMesh& beforeMesh,
                       const std::vector<Moved>& moved) {
    // Where every particle starts, and how fast it is moving.
    Placement start = arrangedNow;
    start.resize(mesh.panels.size());
    std::vector<std::vector<Vec3>> speed(mesh.panels.size());
    for (std::size_t p = 0; p < mesh.panels.size(); ++p) {
        const PanelMesh& panel = mesh.panels[p];
        speed[p].assign(panel.points.size(), Vec3{});
        if (start[p].size() != panel.points.size()) continue;
        const auto motion = std::find_if(moved.begin(), moved.end(), [&](const Moved& m) { return m.panel == panel.panel; });
        // The same panel in the cloth so far, if it was part of it.
        std::size_t old = 0;
        while (old < beforeMesh.panels.size() && !(beforeMesh.panels[old].panel == panel.panel)) ++old;
        if (old >= beforeMesh.panels.size() || old >= before.firstOf_.size() || before.firstOf_[old] == none) continue;
        const PanelMesh& was = beforeMesh.panels[old];
        const Vec3* wasAt = before.at_.data() + before.firstOf_[old];
        const Vec3* wasSpeed = before.speed_.data() + before.firstOf_[old];
        const Locator locator(was);
        // Spots are matched relative to the anchor, which moves with the
        // panel's sewn edges: a longer skirt keeps its waist and grows at the hem.
        const Vec2 shift{was.anchor.x - panel.anchor.x, was.anchor.y - panel.anchor.y};
        for (std::size_t i = 0; i < panel.points.size(); ++i) {
            const Locator::Found found = locator.find({panel.points[i].x + shift.x, panel.points[i].y + shift.y});
            const auto& corners = was.triangles[found.triangle];
            start[p][i] = wasAt[corners[0]] * found.weight[0] + wasAt[corners[1]] * found.weight[1] + wasAt[corners[2]] * found.weight[2];
            speed[p][i] = wasSpeed[corners[0]] * found.weight[0] + wasSpeed[corners[1]] * found.weight[1] + wasSpeed[corners[2]] * found.weight[2];
            if (motion != moved.end()) {
                start[p][i] = motion->motion(start[p][i]);
                speed[p][i] = motion->motion.turned(speed[p][i]);
            }
        }
    }
    Cloth cloth(mesh, start, before.fabric_);
    for (std::size_t p = 0; p < mesh.panels.size(); ++p)
        if (cloth.firstOf_[p] != none) std::copy(speed[p].begin(), speed[p].end(), cloth.speed_.begin() + cloth.firstOf_[p]);
    cloth.time_ = before.time_;
    return cloth;
}

void Cloth::hold(uint32_t panel, uint32_t point, Vec3 to) {
    if (panel >= firstOf_.size() || firstOf_[panel] == none || point >= positions_[panel].size() || out_[panel]) return;
    const uint32_t particle = firstOf_[panel] + point;
    if (held_ && held_->particle == particle) {
        held_->to = to;
        return;
    }
    letGo();
    held_ = Held{particle, lightness_[particle], to};
    lightness_[particle] = 0.0;  // the hand moves it; the cloth does not
}

void Cloth::letGo() {
    if (!held_) return;
    lightness_[held_->particle] = held_->lightness;
    held_.reset();
}

void Cloth::pin(uint32_t panel, uint32_t point) {
    if (panel >= firstOf_.size() || firstOf_[panel] == none || point >= positions_[panel].size()) return;
    own_[firstOf_[panel] + point] = lightness_[firstOf_[panel] + point] = 0.0;
}

// How a correction to a link is shared between its two ends: by their
// lightness, less a little for a compliant link, which gives instead. Worked
// out once per step, so that the many solves in it need no division.
void Cloth::share(std::vector<Link>& links, double compliance, double stepSquared) const {
    for (Link& link : links) {
        const double wa = lightness_[link.a], wb = lightness_[link.b];
        const double whole = wa + wb + compliance / stepSquared;
        link.shareA = wa + wb > 0.0 ? wa / whole : 0.0;
        link.shareB = wa + wb > 0.0 ? wb / whole : 0.0;
    }
}

// Moves the two ends of every link so that it is its rest length again.
void Cloth::solve(const std::vector<Link>& links) {
    Vec3* const at = at_.data();
    for (const Link& link : links) {
        const Link l = link;  // read once: the writes below cannot change it
        const Vec3 a = at[l.a], b = at[l.b];
        const double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        const double squared = dx * dx + dy * dy + dz * dz;
        if (squared < 1e-18) continue;
        const double over = 1.0 - l.rest / std::sqrt(squared);  // the part of the link that is too much
        const double toA = over * l.shareA, toB = over * l.shareB;
        at[l.a] = {a.x + dx * toA, a.y + dy * toA, a.z + dz * toA};
        at[l.b] = {b.x - dx * toB, b.y - dy * toB, b.z - dz * toB};
    }
}

void Cloth::step(const Body& body, double seconds) {
    seconds = std::clamp(seconds, 1.0 / 240.0, 1.0 / 30.0);
    const double h = seconds / subSteps;
    const double thickness = fabric_.thickness;

    // What each particle could reach during this step: only that is looked at
    // in the sub-steps. How far a particle can get follows from how fast the
    // cloth is moving now, with room for what gravity and the seams can add.
    double fastest = 0.0;
    for (const Vec3& v : speed_) fastest = std::max(fastest, dot(v, v));
    const double reach = thickness + 0.2 + 1.5 * seconds * (std::sqrt(fastest) + gravity * seconds + seamSpeed);
    near_.clear();
    firstNear_.assign(at_.size() + 1, 0);
    nearScan_.assign(at_.size(), 0);
    clearance_.assign(at_.size(), 0.0);
    stepFrom_ = at_;
    // How much nearer the body can come, at most, for each cm a particle
    // moves: 1 for limbs; for a scanned surface, what its grid allows.
    const double nearing = body.scanned ? std::max(1.0, std::sqrt(3.0) * body.scanned->steepest()) : 1.0;
    for (std::size_t i = 0; i < at_.size(); ++i) {
        firstNear_[i] = static_cast<uint32_t>(near_.size());
        if (lightness_[i] == 0.0) continue;
        double nearest = 1e30;
        for (std::size_t limb = 0; limb < body.parts.size(); ++limb)
            if (const double away = contactWith(body.parts[limb], at_[i]).distance; away < reach) {
                near_.push_back(static_cast<uint32_t>(limb));
                nearest = std::min(nearest, away);
            }
        if (body.scanned)
            if (const double away = body.scanned->contact(at_[i]).distance; away < reach) {
                nearScan_[i] = 1;
                nearest = std::min(nearest, away);
            }
        clearance_[i] = nearest;
    }
    firstNear_[at_.size()] = static_cast<uint32_t>(near_.size());
    share(edges_, fabric_.stretch, h * h);
    share(folds_, fabric_.bend, h * h);
    findClosePairs(seconds);
    // While stitches are still being drawn closed the cloth is as good as
    // held in place: it barely feels its weight, so the pieces meet where
    // they were arranged instead of falling to the floor first.
    const double weight = gravity * (sewing() ? heldWhileSewing : 1.0);
    // Nor does the body's friction hold it back then: a seam pulling cloth
    // along the body would be held open by it.
    const double friction = sewing() ? 0.0 : fabric_.friction;
    for (int sub = 0; sub < subSteps; ++sub) {
        // Move freely.
        for (std::size_t i = 0; i < at_.size(); ++i) {
            before_[i] = at_[i];
            if (lightness_[i] == 0.0) continue;
            speed_[i].y -= weight * h;
            at_[i] = at_[i] + speed_[i] * h;
        }
        // The hand: the held particle goes towards where it is led.
        if (held_) {
            Vec3& at = at_[held_->particle];
            const Vec3 left = held_->to - at;
            const double far = length(left), most = leadSpeed * h;
            at = far > most ? at + left * (most / far) : held_->to;
        }
        // Then put right what that broke, twice over: lengths, folds and
        // stitches first, then the body and the floor, which always win.
        for (int pass = 0; pass < 2; ++pass) {
            solve(edges_);
            if (pass == 0) solve(folds_);
            for (Thread& thread : threads_) {
                if (!thread.holds) continue;
                // A stitch is a thread being drawn in: it may be no longer than
                // the thread has left, and it carries weight like any thread.
                if (pass == 0) thread.left = std::max(0.0, thread.left - seamSpeed * h);
                const double wa = lightness_[thread.a], wb = lightness_[thread.b];
                if (wa + wb <= 0.0) continue;
                const Vec3 apart = at_[thread.b] - at_[thread.a];
                const double gap = length(apart);
                if (gap < 1e-9 || gap <= thread.left) continue;
                const Vec3 along = apart * ((gap - thread.left) / gap / (wa + wb));
                at_[thread.a] = at_[thread.a] + along * wa;
                at_[thread.b] = at_[thread.b] - along * wb;
            }
            // Cloth keeps clear of cloth. Most listed pairs are not touching: those cost one comparison.
            {
                Vec3* const at = at_.data();
                const double* const light = lightness_.data();
                const double apartSquared = apart_ * apart_;
                for (const Pair& pair : close_) {
                    Vec3& a = at[pair.a];
                    Vec3& b = at[pair.b];
                    const double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                    const double squared = dx * dx + dy * dy + dz * dz;
                    if (squared >= apartSquared || squared < 1e-18) continue;
                    const double gap = std::sqrt(squared);
                    const double wa = light[pair.a], wb = light[pair.b];
                    const double push = (apart_ - gap) / gap / (wa + wb);
                    a.x -= dx * push * wa, a.y -= dy * push * wa, a.z -= dz * push * wa;
                    b.x += dx * push * wb, b.y += dy * push * wb, b.z += dz * push * wb;
                }
            }
            // Nothing may be inside the body or under the floor. What is
            // pushed out is also held back sideways, by friction.
            for (std::size_t i = 0; i < at_.size(); ++i) {
                if (lightness_[i] == 0.0) continue;
                if (!nearScan_[i] && firstNear_[i] == firstNear_[i + 1] && at_[i].y >= thickness) continue;  // nothing within reach
                const auto pushOut = [&](Vec3 outward, double depth) {
                    at_[i] = at_[i] + outward * depth;
                    const Vec3 moved = at_[i] - before_[i];
                    const Vec3 sideways = moved - outward * dot(moved, outward);
                    const double slid = length(sideways);
                    if (slid > 1e-12) at_[i] = at_[i] - sideways * (std::min(slid, friction * depth) / slid);
                };
                // Still clear of the body: since the step began it has not
                // moved far enough to touch it, so nothing asked here could
                // push it out. Most of a garment is clear most of the time.
                const Vec3 moved = at_[i] - stepFrom_[i];
                const double room = clearance_[i] - thickness;
                const bool clear = room > 0.0 && nearing * nearing * dot(moved, moved) < room * room;
                for (uint32_t n = firstNear_[i]; !clear && n < firstNear_[i + 1]; ++n) {
                    const Contact contact = contactWith(body.parts[near_[n]], at_[i]);
                    if (contact.distance < thickness) pushOut(contact.outward, thickness - contact.distance);
                }
                if (nearScan_[i] && !clear) {
                    const Contact contact = body.scanned->contact(at_[i]);
                    if (contact.distance < thickness) pushOut(contact.outward, thickness - contact.distance);
                }
                if (at_[i].y < thickness) pushOut({0.0, 1.0, 0.0}, thickness - at_[i].y);
            }
        }
        // The speed is whatever movement is left.
        const double keep = std::max(0.0, 1.0 - fabric_.airDrag * h);
        for (std::size_t i = 0; i < at_.size(); ++i) {
            if (lightness_[i] == 0.0) {
                speed_[i] = {};
                continue;
            }
            speed_[i] = (at_[i] - before_[i]) * (keep / h);
            const double fast = length(speed_[i]);
            if (fast > topSpeed) speed_[i] = speed_[i] * (topSpeed / fast);
        }
    }
    time_ += seconds;
    checkTheSeams();
    for (std::size_t p = 0; p < firstOf_.size(); ++p)
        if (firstOf_[p] != none) std::copy_n(at_.begin() + firstOf_[p], positions_[p].size(), positions_[p].begin());
}

double Cloth::fastest() const {
    double fast = 0.0;
    for (const Vec3& v : speed_) fast = std::max(fast, length(v));
    return fast;
}

void Cloth::checkTheSeams() {
    if (seamsClosed_) return;
    if (std::any_of(threads_.begin(), threads_.end(), [](const Thread& t) { return t.holds && t.left > 0.0; })) return;
    if (drawnInAt_ < 0.0) drawnInAt_ = time_;
    const bool allClosed = std::all_of(threads_.begin(), threads_.end(),
                                       [&](const Thread& t) { return !t.holds || length(at_[t.b] - at_[t.a]) <= closedWithin; });
    seamsClosed_ = allClosed || time_ - drawnInAt_ >= givingUpAfter;
}

bool Cloth::sound() const {
    for (const Vec3& p : at_)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    return true;
}

} // namespace clothcad::sim
