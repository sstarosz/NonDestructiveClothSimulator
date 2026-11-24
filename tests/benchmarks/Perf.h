#pragma once
// Timing for the performance tests: how long things take, judged against
// what they took before.
//
//   perf::Report report(argc, argv);
//   report.time("mesh a skirt", 8.0, [&] { meshOf(doc, shapes); });
//   return report.finish();
//
// Machines differ, so times are not compared directly. Each run first times
// a fixed piece of arithmetic, the yardstick, and every measurement is
// expressed in yardsticks. Those are what the baseline file holds.
//
//   perf_tests baseline.txt             judge against the baseline
//   perf_tests baseline.txt --update    write the baseline from this run
//
// A measurement fails if it is more than `tolerance` times its baseline, or
// if it takes longer than its limit in milliseconds on a machine no slower
// than the one the limits were set on (see `limit` below). A build that is
// not optimised is not timed at all: its times say nothing.
//
// Every measurement is the best of several runs, because a busy machine
// only ever makes things slower.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace perf {

inline constexpr double tolerance = 1.4;  // slower than this times the baseline is a regression

template <class Work>
double bestMilliseconds(Work work, int runs) {
    double best = 1e300;
    for (int i = 0; i < runs; ++i) {
        const auto began = std::chrono::steady_clock::now();
        work();
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
    }
    return best;
}

// A fixed piece of arithmetic of the kind the program does: square roots and
// sums over a few thousand numbers that do not fit in the processor's
// smallest cache. About 10 ms on the machine the limits were set on.
inline double yardstickMilliseconds() {
    std::vector<double> numbers(60000);
    for (std::size_t i = 0; i < numbers.size(); ++i) numbers[i] = 1.0 + static_cast<double>(i % 977) * 0.01;
    volatile double keep = 0.0;
    return bestMilliseconds(
        [&] {
            double sum = 0.0;
            for (int round = 0; round < 40; ++round)
                for (std::size_t i = 1; i < numbers.size(); ++i) {
                    const double apart = numbers[i] - numbers[i - 1] * 0.5;
                    sum += apart / std::sqrt(apart * apart + numbers[i]);
                    numbers[i - 1] = numbers[i - 1] * 0.999 + 0.001 * sum / (1.0 + static_cast<double>(i));
                }
            keep = sum;
        },
        9);
}

// The times of a run of frames, one by one. What a drag feels like is its slow
// frames, not its average and not its best: one frame in twenty that takes
// three times as long is what a lag is.
//
//   perf::Frames frames;
//   for (...) frames.add(perf::bestMilliseconds([&] { oneFrame(); }, 1));
//   report.frames("dragging a corner", 16.0, frames);
class Frames {
public:
    void add(double milliseconds) { times_.push_back(milliseconds); }
    std::size_t count() const { return times_.size(); }
    double mean() const {
        double sum = 0.0;
        for (const double t : times_) sum += t;
        return times_.empty() ? 0.0 : sum / static_cast<double>(times_.size());
    }
    // The time that one frame in twenty is slower than.
    double slowestOneInTwenty() const {
        if (times_.empty()) return 0.0;
        std::vector<double> sorted = times_;
        std::sort(sorted.begin(), sorted.end());
        return sorted[static_cast<std::size_t>(static_cast<double>(sorted.size() - 1) * 0.95)];
    }
    double worst() const { return times_.empty() ? 0.0 : *std::max_element(times_.begin(), times_.end()); }

private:
    std::vector<double> times_;
};

// The yardstick on the machine the limits in milliseconds were set on.
inline constexpr double referenceYardstick = 10.0;

class Report {
public:
    Report(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string word = argv[i];
            if (word == "--update") update_ = true;
            else if (word == "--anyway") anyway_ = true;
            else baselinePath_ = word;
        }
#ifdef NDEBUG
        judged_ = true;
#endif
        yardstick_ = yardstickMilliseconds();
        std::ifstream file(baselinePath_);
        std::string line;
        while (std::getline(file, line)) {
            const std::size_t tab = line.find('\t');
            if (tab == std::string::npos || line.empty() || line[0] == '#') continue;
            baseline_[line.substr(0, tab)] = std::stod(line.substr(tab + 1));
        }
        if (!worthRunning()) {
            std::printf("Not an optimised build: its times say nothing, so nothing is timed. (--anyway times it all the same.)\n");
            return;
        }
        std::printf("yardstick %.2f ms%s\n", yardstick_, judged_ ? "" : "   (not an optimised build: timed, not judged)");
        std::printf("%-46s %9s %9s %9s  %s\n", "", "ms", "limit", "x before", "");
    }

    // Whether this run means anything: false in a build that is not optimised.
    // The tests then stop at once, unless asked to go on with --anyway.
    bool worthRunning() const { return judged_ || anyway_; }

    // Times `work` (best of `runs`) and judges it. `limit` is the most
    // milliseconds it may take on the reference machine; it is scaled by how
    // this machine's yardstick compares, and is a coarse net: the baseline
    // is the fine one.
    template <class Work>
    double time(const std::string& name, double limit, Work work, int runs = 7) {
        return record(name, limit, bestMilliseconds(work, runs));
    }

    // Judges a time that was measured some other way. `againstBaseline` false:
    // held to the limit alone, and not kept in the baseline (a statistic of
    // the worst frames moves too much from run to run for a ratio to mean anything).
    double record(const std::string& name, double limit, double milliseconds, bool againstBaseline = true) {
        const double units = milliseconds / yardstick_;
        const double scaledLimit = limit * std::max(yardstick_ / referenceYardstick, 1.0);
        const auto before = baseline_.find(name);
        const double ratio = !againstBaseline || before == baseline_.end() || before->second <= 0.0 ? 0.0 : units / before->second;
        // Very short times are mostly noise: they are held to the limit alone.
        const bool regressed = ratio > tolerance && milliseconds > 0.05;
        const bool over = milliseconds > scaledLimit;
        const bool failed = judged_ && !update_ && (regressed || over);
        char ratioText[32] = "      new";
        if (ratio > 0.0) std::snprintf(ratioText, sizeof ratioText, "%9.2f", ratio);
        std::printf("%-46s %9.3f %9.2f %s  %s\n", name.c_str(), milliseconds, scaledLimit, ratioText,
                    failed ? (over ? "FAIL: over its limit" : "FAIL: slower than before") : "ok");
        if (againstBaseline) measured_.push_back({name, units});
        if (failed) ++failures_;
        return milliseconds;
    }

    // Judges a run of frames: their mean against `limit` and the baseline, and
    // the time one frame in twenty is slower than against twice the limit (a
    // row of its own, so that a stutter is caught that the mean hides). The
    // worst frame is shown.
    void frames(const std::string& name, double limit, const Frames& frames) {
        record(name, limit, frames.mean());
        record(name + ", slowest 1 in 20", limit * 2.0, frames.slowestOneInTwenty(), false);
        std::printf("%-46s %9.3f   (worst of %zu frames)\n", "", frames.worst(), frames.count());
    }

    // A plain condition, for things that must hold whatever the machine.
    void expect(bool holds, const std::string& what) {
        std::printf("%s  %s\n", holds ? "ok  " : "FAIL", what.c_str());
        if (!holds) ++failures_;
    }

    int finish() {
        if (update_ && !baselinePath_.empty()) {
            if (!judged_) {
                std::printf("The baseline is not written from a build that is not optimised.\n");
                return 1;
            }
            std::ofstream file(baselinePath_, std::ios::trunc);
            file << "# Times in yardsticks (see tests/Perf.h). Written by --update; do not edit by hand.\n";
            for (const Measured& m : measured_) file << m.name << '\t' << m.units << '\n';
            std::printf("baseline written to %s\n", baselinePath_.c_str());
            return file ? 0 : 1;
        }
        std::printf("%d failed\n", failures_);
        return failures_ == 0 ? 0 : 1;
    }

private:
    struct Measured {
        std::string name;
        double units;
    };
    std::string baselinePath_;
    bool update_ = false;
    bool anyway_ = false;
    bool judged_ = false;
    double yardstick_ = 1.0;
    std::map<std::string, double> baseline_;
    std::vector<Measured> measured_;
    int failures_ = 0;
};

} // namespace perf
