#include "twintailor/session/FrameTimes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace twintailor::session {

namespace {

std::size_t indexOf(FrameTimes::View view) { return static_cast<std::size_t>(view); }
std::size_t indexOf(FrameTimes::Work work) { return static_cast<std::size_t>(work); }

std::string milliseconds(double value) {
    char text[32];
    std::snprintf(text, sizeof text, value < 10.0 ? "%.2f ms" : "%.1f ms", value);
    return text;
}

// 2925 as "2,925".
std::string count(std::size_t value) {
    std::string digits = std::to_string(value);
    for (std::size_t at = digits.size(); at > 3; at -= 3) digits.insert(at - 3, ",");
    return digits;
}

} // namespace

void FrameTimes::Latest::add(double milliseconds) {
    times_[next_] = milliseconds;
    next_ = (next_ + 1) % kept;
    count_ = std::min(count_ + 1, kept);
}

FrameTimes::Times FrameTimes::Latest::times() const {
    if (count_ == 0) return {};
    std::array<double, kept> sorted = times_;
    double sum = 0.0;
    for (std::size_t i = 0; i < count_; ++i) sum += times_[i];
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count_));
    // As Perf.h's Frames: the time that one in twenty is slower than.
    const double slowest = sorted[static_cast<std::size_t>(static_cast<double>(count_ - 1) * 0.95)];
    return {.mean = sum / static_cast<double>(count_), .slowest = slowest, .count = count_};
}

int FrameTimes::tickMilliseconds(double hertz) {
    if (!(hertz > 0.0)) hertz = 60.0;
    return std::clamp(static_cast<int>(std::floor(1000.0 / hertz)), 2, 16);
}

void FrameTimes::shown(View view, double atMilliseconds) {
    std::optional<double>& last = lastShown_[indexOf(view)];
    if (last && atMilliseconds - *last <= pause && atMilliseconds >= *last) frames_[indexOf(view)].add(atMilliseconds - *last);
    last = atMilliseconds;
}

void FrameTimes::took(Work work, double milliseconds) { work_[indexOf(work)].add(milliseconds); }

FrameTimes::Times FrameTimes::of(View view) const { return frames_[indexOf(view)].times(); }
FrameTimes::Times FrameTimes::of(Work work) const { return work_[indexOf(work)].times(); }

std::string FrameTimes::text(const std::optional<Cloth>& cloth) const {
    const auto frames = [&](const char* name, View view) {
        const Times t = of(view);
        return std::string(name) + (t.count == 0 ? " still" : " " + milliseconds(t.mean) + " a frame, 1 in 20 over " + milliseconds(t.slowest));
    };
    const auto work = [&](const char* name, Work kind) {
        const Times t = of(kind);
        return t.count == 0 ? std::string() : std::string("  ·  ") + name + " " + milliseconds(t.mean);
    };
    std::string line = frames("3D", View::Space) + "  ·  " + frames("table", View::Table);
    line += work("change", Work::Change) + work("tick", Work::Tick) + work("table painting", Work::TablePainting) + work("3D drawing", Work::SpaceDrawing);
    if (cloth) {
        line += "  ·  cloth step " + milliseconds(cloth->stepMilliseconds) + ", " + count(cloth->particles) + " particles, ";
        line += cloth->sewing ? "sewing" : cloth->running ? "running" : "paused";
    }
    return line;
}

} // namespace twintailor::session
