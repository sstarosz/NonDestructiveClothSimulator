// Unit tests of FrameTimes.cpp: how far apart a view's pictures are, how long
// a frame's work takes, and the line the status bar shows.

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/session/FrameTimes.h"

using namespace clothcad;
using session::FrameTimes;

TEST_CASE("a view's frames are the spaces between its pictures; a pause is not a frame")
{
    FrameTimes times;
    EXPECT((times.of(FrameTimes::View::Table).count == 0), "nothing is counted before a picture");
    times.shown(FrameTimes::View::Table, 1000.0);
    EXPECT((times.of(FrameTimes::View::Table).count == 0), "one picture is not yet a frame");
    times.shown(FrameTimes::View::Table, 1016.0);
    times.shown(FrameTimes::View::Table, 1036.0);
    EXPECT_NEAR(times.of(FrameTimes::View::Table).mean, 18.0, "16 and 20 ms apart: 18 ms a frame");
    EXPECT((times.of(FrameTimes::View::Table).count == 2), "two frames between three pictures");

    times.shown(FrameTimes::View::Table, 5000.0);  // the table had nothing to show for four seconds
    EXPECT((times.of(FrameTimes::View::Table).count == 2), "a pause longer than a quarter second is not a frame");
    times.shown(FrameTimes::View::Table, 5010.0);
    EXPECT((times.of(FrameTimes::View::Table).count == 3), "the next picture after it is");
    EXPECT((times.of(FrameTimes::View::Space).count == 0), "and the 3D view counts its own");
}

TEST_CASE("only the latest frames are kept, and one in twenty slower than `slowest`")
{
    FrameTimes times;
    for (std::size_t i = 0; i < 100; ++i) times.took(FrameTimes::Work::Tick, 50.0);  // long ago
    for (std::size_t i = 0; i < FrameTimes::kept; ++i) times.took(FrameTimes::Work::Tick, i % 20 == 19 ? 30.0 : 10.0);
    const FrameTimes::Times tick = times.of(FrameTimes::Work::Tick);
    EXPECT((tick.count == FrameTimes::kept), "as many as are kept");
    EXPECT_NEAR(tick.mean, (57 * 10.0 + 3 * 30.0) / 60.0, "the mean of the latest only");
    EXPECT_NEAR(tick.slowest, 10.0, "three of sixty are slower than 10 ms: one in twenty");
    times.took(FrameTimes::Work::Tick, 30.0);
    times.took(FrameTimes::Work::Tick, 30.0);
    EXPECT_NEAR(times.of(FrameTimes::Work::Tick).slowest, 30.0, "five of sixty are slower than 10 ms: more than one in twenty");
}

TEST_CASE("the status bar's line gives every number its unit, and the cloth when there is one")
{
    FrameTimes times;
    EXPECT((times.text(std::nullopt) == "3D still  ·  table still"), "before any frame: both views still, nothing else");

    times.shown(FrameTimes::View::Space, 0.0);
    times.shown(FrameTimes::View::Space, 16.7);
    times.took(FrameTimes::Work::Tick, 0.4);
    times.took(FrameTimes::Work::SpaceDrawing, 2.0);
    const std::string line = times.text(FrameTimes::Cloth{.stepMilliseconds = 11.24, .particles = 2925, .running = true, .sewing = false});
    EXPECT((line.find("3D 16.7 ms a frame, 1 in 20 over 16.7 ms") != std::string::npos), "the 3D view's frames");
    EXPECT((line.find("table still") != std::string::npos), "the table showed nothing new");
    EXPECT((line.find("tick 0.40 ms") != std::string::npos && line.find("3D drawing 2.00 ms") != std::string::npos), "the work of a frame");
    EXPECT((line.find("change") == std::string::npos), "work that did not happen is left out");
    EXPECT((line.find("cloth step 11.2 ms, 2,925 particles, running") != std::string::npos), "the cloth's step, its size and what it does");
    EXPECT((times.text(FrameTimes::Cloth{.stepMilliseconds = 9.0, .particles = 800, .running = true, .sewing = true}).find("800 particles, sewing") != std::string::npos),
           "sewing says so");
    EXPECT((times.text(FrameTimes::Cloth{}).find("paused") != std::string::npos), "a cloth that does not run is paused");
}

TEST_CASE("the window ticks once for every picture its screen shows")
{
    EXPECT((FrameTimes::tickMilliseconds(60.0) == 16), "60 Hz: every 16 ms, as before");
    EXPECT((FrameTimes::tickMilliseconds(59.94) == 16), "a screen a little under 60 Hz too");
    EXPECT((FrameTimes::tickMilliseconds(144.0) == 6), "144 Hz: every 6 ms, not 7, so that no picture is missed");
    EXPECT((FrameTimes::tickMilliseconds(240.0) == 4), "240 Hz: every 4 ms, so that a panel carried in 3D is not shown at a quarter of the screen's rate");
    EXPECT((FrameTimes::tickMilliseconds(1000.0) == 2), "never faster than 2 ms");
    EXPECT((FrameTimes::tickMilliseconds(30.0) == 16), "never slower than 16 ms: the cloth and the docks still follow at 60 a second");
    EXPECT((FrameTimes::tickMilliseconds(0.0) == 16), "a screen that gives no rate is taken as 60 Hz");
}
