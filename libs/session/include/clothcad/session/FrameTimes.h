#pragma once
// How long the window's frames take, as they are in the real window and on
// the owner's screen (View > Frame times; #56): how far apart the pictures
// each view shows are, and how long the work in a frame takes. The last
// `kept` of each are kept, and give their mean and the time that one frame in
// twenty is slower than, as the benchmarks judge a drag
// (tests/benchmarks/Perf.h). The window measures; this only keeps the count.
//
//   times.shown(FrameTimes::View::Table, clock.nsecsElapsed() / 1e6);
//   times.took(FrameTimes::Work::Tick, milliseconds);
//   label->setText(QString::fromStdString(times.text(cloth)));

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace clothcad::session {

class FrameTimes {
public:
    enum class View : std::uint8_t { Space, Table };
    enum class Work : std::uint8_t {
        Change,        // a change of the document, shown again in every dock
        Tick,          // the window's frame: the session catches up, the views are told
        TablePainting, // the table paints itself
        SpaceDrawing,  // the 3D view draws its picture
    };

    // How many of each are kept.
    static constexpr std::size_t kept = 60;
    // Pictures further apart than this are not frames of one motion: the view
    // had nothing new to show in between.
    static constexpr double pause = 250.0;

    // A view showed a new picture at `atMilliseconds`, on any steady clock.
    void shown(View view, double atMilliseconds);
    void took(Work work, double milliseconds);

    struct Times {
        double mean = 0.0;
        double slowest = 0.0;   // one in twenty is slower than this
        std::size_t count = 0;  // how many are kept, up to `kept`
    };
    Times of(View view) const;
    Times of(Work work) const;

    // What the cloth is doing, shown beside the frames.
    struct Cloth {
        double stepMilliseconds = 0.0;  // the latest step
        std::size_t particles = 0;
        bool running = false;
        bool sewing = false;
    };
    // One line for the status bar, every number with its unit: the frames of
    // each view, the work of a frame, and the cloth if there is one.
    std::string text(const std::optional<Cloth>& cloth) const;

private:
    // The latest times of one kind, oldest overwritten first; nothing is
    // allocated as frames come.
    class Latest {
    public:
        void add(double milliseconds);
        Times times() const;

    private:
        std::array<double, kept> times_{};
        std::size_t count_ = 0;
        std::size_t next_ = 0;
    };
    std::array<Latest, 2> frames_;
    std::array<std::optional<double>, 2> lastShown_;
    std::array<Latest, 4> work_;
};

} // namespace clothcad::session
