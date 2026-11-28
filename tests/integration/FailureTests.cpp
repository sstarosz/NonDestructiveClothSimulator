// What happens when work on another thread fails: the program is told, and
// carries on or hands the error to whoever asked, instead of ending. The
// failure is running out of memory, made on purpose: this program has its own
// operator new, so these tests are a program of their own.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "Change.h"
#include "Expect.h"
#include "twintailor/session/Autosave.h"
#include "twintailor/session/Session.h"
#include "twintailor/sim/Body.h"

namespace {

std::atomic<bool> outOfMemory{false};
thread_local bool spared = false;  // the thread the test runs on

// While there is one, nothing large can be had on any thread but this one.
// Small things still can: the library's own bookkeeping for its threads.
struct NoMemoryElsewhere {
    NoMemoryElsewhere() {
        spared = true;
        outOfMemory = true;
    }
    ~NoMemoryElsewhere() { outOfMemory = false; }
    NoMemoryElsewhere(const NoMemoryElsewhere&) = delete;
    NoMemoryElsewhere& operator=(const NoMemoryElsewhere&) = delete;
};

}  // namespace

void* operator new(std::size_t size) {
    if (size >= 4096 && outOfMemory && !spared) throw std::bad_alloc();
    if (void* made = std::malloc(size ? size : 1)) return made;
    throw std::bad_alloc();
}
void operator delete(void* made) noexcept { std::free(made); }
void operator delete(void* made, std::size_t) noexcept { std::free(made); }

using namespace twintailor;
using namespace twintailor::session;

namespace {

// A metre square: its mesh, and its cloth, are large things.
struct Square {
    Document doc;
    Param width;
    PanelId panel;
    Square() {
        width = doc.add(Parameter{.name = "width", .value = 100});
        panel = doc.add(Panel{.name = "square"});
        doc.add(panel, Rect{.width = width, .height = 100_cm});
    }
};

// A tetrahedron to wear: working out how to collide with it takes a large grid.
std::shared_ptr<const Avatar> body() {
    return std::make_shared<const Avatar>(
        Avatar{.name = "body.obj", .points = {{-20, 0, -20}, {20, 0, -20}, {0, 150, 0}, {0, 0, 20}}, .triangles = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}}});
}

// Frames until update() has something to tell, or a few seconds have passed.
std::optional<std::string> told(Session& session) {
    for (int frame = 0; frame < 1000; ++frame) {
        if (std::optional<std::string> news = session.update()) return news;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return std::nullopt;
}

bool says(const std::optional<std::string>& news, const char* words) { return news && news->find(words) != std::string::npos; }

}  // namespace

TEST_CASE("a body that cannot be worked out is said once; the cloth waits, and the rest goes on")
{
    Square start;
    start.doc.setAvatar(body());
    const NoMemoryElsewhere failing;
    Session session(start.doc, Threading::OneStepPerAdvance);
    session.simulate(true);
    const std::optional<std::string> news = told(session);
    EXPECT((says(news, "avatar could not be made ready") && says(news, "bad allocation")), "told, and why");
    EXPECT((!session.update() && !session.update()), "once");
    EXPECT((!session.bodyReady() && session.bodyProblem() == news && !session.bodyProgress()), "no body to collide with, and none being made");
    EXPECT((!session.clothShown() && session.simulating()), "so the cloth waits instead of falling through it");
    EXPECT((tests::change(session, [&](Document& doc) { doc.set(start.width, Expr(90)); })), "and the pattern can still be changed");
    EXPECT((!session.update() && !session.mesh()->panels.empty()), "and meshed");

    // Another avatar is another try.
    REQUIRE(tests::change(session, [](Document& doc) { doc.setAvatar(nullptr); }));
    session.update();
    EXPECT((session.bodyReady() && !session.bodyProblem()), "without the one that failed there is nothing in the way");
}

TEST_CASE("a mesh that cannot be made is said once; the one before stays, and the next change is tried again")
{
    Square start;
    Session session(start.doc, Threading::OwnThread);
    const auto frames = [&](auto done) {
        for (int frame = 0; frame < 1000 && !done(); ++frame) {
            session.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(done());
    };
    frames([&] { return session.meshesMade() == 1; });
    const std::shared_ptr<const sim::ClothMesh> before = session.mesh();
    {
        const NoMemoryElsewhere failing;
        REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.width, Expr(90)); }));
        const std::optional<std::string> news = told(session);
        EXPECT((says(news, "mesh could not be made") && says(news, "Undo")), "told, with what to do");
        for (int frame = 0; frame < 20; ++frame) EXPECT((!session.update()), "once, and not tried again for the same outlines");
        EXPECT((session.mesh() == before && session.meshesMade() == 1), "the one before stays");
        const std::expected<sim::Obj, std::string> obj = session.obj();
        EXPECT((!obj && says(obj.error(), "mesh could not be made")), "an export does not pass it off as the pattern as it is");
    }
    REQUIRE(tests::change(session, [&](Document& doc) { doc.set(start.width, Expr(80)); }));
    frames([&] { return session.meshesMade() == 2; });
    EXPECT((session.obj().has_value()), "and all is well again");
}

TEST_CASE("an error on the cloth's thread drops the cloth and is said; the cloth can be started again")
{
    Square start;
    Session session(start.doc, Threading::OwnThread);
    for (int frame = 0; frame < 1000 && session.meshesMade() == 0; ++frame) {
        session.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(session.meshesMade() == 1);
    {
        const NoMemoryElsewhere failing;
        session.simulate(true);
        const std::optional<std::string> news = told(session);
        EXPECT((says(news, "simulation stopped") && says(news, "bad allocation")), "told, and why");
        EXPECT((!session.clothShown() && !session.simulating()), "the cloth is gone");
        for (int frame = 0; frame < 20; ++frame) EXPECT((!session.update()), "said once");
    }
    session.simulate(true);
    for (int frame = 0; frame < 1000 && !(session.clothShown() && session.cloth().time > 0.0); ++frame) {
        EXPECT((!session.update()), "nothing more to tell");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT((session.clothShown() && session.cloth().time > 0.0), "and runs again");
}

TEST_CASE("a recovery copy that runs out of memory is a copy that could not be written")
{
    const std::filesystem::path folder = filePath(TWINTAILOR_TEST_FOLDER);
    std::filesystem::create_directories(folder);
    const std::u8string utf8 = folder.generic_u8string();
    Square start;
    for (int i = 0; i < 200; ++i) start.doc.add(Parameter{.name = "parameter " + std::to_string(i), .value = i});  // a large file
    Session session(start.doc, Threading::OneStepPerAdvance);
    Autosave autosave(std::string(utf8.begin(), utf8.end()), 0.0);
    {
        const NoMemoryElsewhere failing;
        autosave.write(session, "square", "");
        const std::optional<std::string> problem = autosave.wait();
        EXPECT((says(problem, "recovery copy could not be written") && says(problem, "bad allocation")), "told, and why");
        autosave.write(session, "square", "");
        autosave.forget();  // waits for it, and must not fail with it
    }
    const std::expected<void, std::string> written = autosave.writeNow(session, "square", "");
    EXPECT((written.has_value() && hasRecovery(std::string(utf8.begin(), utf8.end()))), "with memory again, it is written at once");
}

TEST_CASE("an avatar file that runs out of memory on one of the cores gives the error to whoever read it")
{
    if (std::thread::hardware_concurrency() <= 2) SKIP("with so few cores a file is read on this thread alone");
    // Large enough to be read in many pieces, side by side.
    std::string text = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    while (text.size() < 40u << 20) text += "f 1 2 3\n";
    const NoMemoryElsewhere failing;
    CHECK_THROWS_AS(sim::readObj(text), std::bad_alloc);
}
