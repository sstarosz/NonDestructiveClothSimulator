// Integration tests of the recovery copy (libs/session, Autosave.cpp): it is
// written to disk, so it is tested here and not beside the library, whose
// unit tests read and write no files. The window's part is in
// QtWindowTests.cpp.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/session/Autosave.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

// An empty folder of this test's own, beside the test program.
std::string emptyFolder(const char* name) {
    const std::filesystem::path folder = filePath(TWINTAILOR_TEST_FOLDER) / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::u8string utf8 = folder.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

struct Band {
    Document doc;
    Param waist;
    PanelId panel;
    Band() {
        waist = doc.add(Parameter{.name = "waist", .value = 80});
        panel = doc.add(Panel{.name = "band"});
        doc.add(panel, Rect{.width = waist, .height = 4_cm});
    }
};

// An avatar made in memory, as one read from a file is: a ball at chest height.
std::shared_ptr<const Avatar> ball(double radius) {
    sim::Body body;
    body.parts.push_back({{0, 100, 0}, {0, 100, 0}, radius, radius});
    return std::make_shared<const Avatar>(sim::avatarOf(sim::surfaceOf(body), "ball.obj"));
}

// A band being worked on, and the copy of it in a folder of its own.
struct Copied {
    std::string folder;
    Band start;
    Session session;
    Autosave autosave{folder};
    explicit Copied(const char* name, Band band = {}) : folder(emptyFolder(name)), start(std::move(band)), session(start.doc, Threading::OneStepPerAdvance) {}
    // A frame of the program, and the copy it began written.
    void frame(double now = 0.0) {
        session.update();
        const std::optional<std::string> problem = autosave.update(session, now, "band", "C:/patterns/band.twintail");
        EXPECT((!problem && !autosave.wait()), "no problem");
    }
    void waist(int to) { REQUIRE(session.run(SetField{start.waist, Expr(to)})); }
    std::optional<double> copiedWaist() const {
        const std::expected<Recovery, std::string> copy = readRecovery(folder);
        REQUIRE(copy);
        return asNumber(copy->scene.document.get(start.waist).value);
    }
    SceneState copiedState() const {
        const std::expected<Recovery, std::string> copy = readRecovery(folder);
        REQUIRE(copy);
        return copy->scene.state;
    }
    void removeFiles() const {
        for (const auto& file : std::filesystem::directory_iterator(filePath(folder))) std::filesystem::remove(file.path());
    }
};

}  // namespace

TEST_CASE("a copy is written after each change, with whose it is, and goes when the work is saved")
{
    Copied band("change");
    Session& session = band.session;
    band.frame();
    EXPECT((!hasRecovery(band.folder)), "saved work needs no copy");

    band.waist(90);
    band.frame();
    REQUIRE(hasRecovery(band.folder));
    const std::expected<Recovery, std::string> copy = readRecovery(band.folder);
    REQUIRE(copy);
    EXPECT((copy->title == "band" && copy->path == "C:/patterns/band.twintail"), "whose it is");
    EXPECT((band.copiedWaist() == 90.0), "the change, at once");
    band.waist(100);
    band.frame();
    EXPECT((band.copiedWaist() == 100.0), "and the next one");
    REQUIRE(session.undo());
    band.frame();
    EXPECT((band.copiedWaist() == 90.0), "an undo is a change too");

    band.removeFiles();
    band.frame();
    EXPECT((!hasRecovery(band.folder)), "work that has not changed since is not written again");

    session.setPanelShown(band.start.panel, false);
    band.frame();
    EXPECT((hasRecovery(band.folder) && band.copiedState().hidden.size() == 1), "what is hidden is part of the work");

    session.markSaved();
    band.frame();
    EXPECT((!hasRecovery(band.folder) && std::filesystem::is_empty(filePath(band.folder))), "saved, the copy goes");
}

TEST_CASE("what an unfinished edit changes is copied when it is finished, or after the wait")
{
    Copied band("edit");
    Session& session = band.session;
    const Edit drag = session.begin();
    REQUIRE(session.preview(drag, SetField{band.start.waist, Expr(90)}));
    band.frame(0.0);
    band.frame(29.0);
    EXPECT((!hasRecovery(band.folder)), "a drag that goes on is not copied");
    session.commit(drag);
    band.frame(29.0);
    EXPECT((band.copiedWaist() == 90.0), "finished, it is copied at once");

    const Edit longer = session.begin();
    REQUIRE(session.preview(longer, SetField{band.start.waist, Expr(100)}));
    band.frame(40.0);
    EXPECT((band.copiedWaist() == 90.0), "not yet");
    band.frame(70.0);
    EXPECT((band.copiedWaist() == 100.0), "going on for the wait, it is copied all the same");
    REQUIRE(session.cancel(longer));
    band.frame(70.0);
    EXPECT((band.copiedWaist() == 90.0), "and taken back, so is the copy");
}

TEST_CASE("the cloth is copied as it comes to rest, not as it moves")
{
    Copied band("cloth");
    Session& session = band.session;
    band.waist(90);
    band.frame();
    session.simulate(true);
    for (int i = 0; i < 20; ++i) band.frame();
    EXPECT((!band.copiedState().drape), "the cloth in motion writes nothing: the copy from before Play stays");

    band.waist(100);
    band.frame();
    REQUIRE(band.copiedState().drape);
    const double atChange = band.copiedState().drape->time;
    for (int i = 0; i < 10; ++i) band.frame();
    EXPECT((band.copiedState().drape->time == atChange), "a change while it plays is copied, with the cloth of that moment");

    session.simulate(false);
    band.frame();
    band.frame();
    const double paused = session.cloth().time;
    EXPECT((paused > atChange && band.copiedState().drape->time == paused), "paused, the cloth is copied");

    const Edit pull = session.begin();
    REQUIRE(session.preview(pull, PullCloth{band.start.panel, 0, session.cloth().positions.front()[0] + Vec3{0, 0, 10}}));
    for (int i = 0; i < 10; ++i) band.frame();
    EXPECT((band.copiedState().drape->time == paused), "not while it is pulled");
    session.commit(pull);
    band.frame();
    band.frame();
    EXPECT((session.cloth().time > paused && band.copiedState().drape->time == session.cloth().time), "let go, it is");

    REQUIRE(session.run(ResetCloth{}));
    band.frame();
    EXPECT((!band.copiedState().drape), "a Reset is copied");
    REQUIRE(session.undo());
    band.frame();
    band.frame();
    EXPECT((band.copiedState().drape.has_value()), "and its undo");
}

TEST_CASE("the avatar of a copy is in a file of its own, written only when it is another one")
{
    const std::shared_ptr<const Avatar> small = ball(15), large = ball(25);
    Band start;
    start.doc.setAvatar(small);
    Copied band("avatar", start);
    Session& session = band.session;
    const auto in = [&](const char* name) { return filePath(band.folder + "/" + name); };
    const auto there = [&](const char* name) { return std::filesystem::exists(in(name)); };
    const auto copied = [&] {
        const std::expected<Recovery, std::string> copy = readRecovery(band.folder);
        REQUIRE(copy);
        return copy->scene.document.avatar();
    };

    band.waist(90);
    band.frame();
    REQUIRE((there("pattern-1.twintail") && there("avatar-1.twintail")));
    EXPECT((copied() && *copied() == *small), "put together again, the avatar is the one of the pattern");
    EXPECT((std::filesystem::file_size(in("pattern-1.twintail")) < std::filesystem::file_size(in("avatar-1.twintail")) / 4),
           "and is not in the pattern's file");

    const auto longAgo = std::filesystem::last_write_time(in("avatar-1.twintail")) - std::chrono::hours(24);
    std::filesystem::last_write_time(in("avatar-1.twintail"), longAgo);
    band.waist(100);
    band.frame();
    EXPECT((band.copiedWaist() == 100.0 && std::filesystem::last_write_time(in("avatar-1.twintail")) == longAgo),
           "a change of the pattern leaves the avatar's file");

    REQUIRE(session.run(SetAvatar{large}));
    band.frame();
    EXPECT((there("pattern-2.twintail") && there("avatar-2.twintail") && !there("pattern-1.twintail") && !there("avatar-1.twintail")),
           "another avatar is another pair of files, and the old pair goes");
    EXPECT((*copied() == *large), "with the other avatar");
    REQUIRE(session.run(SetAvatar{}));
    band.frame();
    EXPECT((!copied()), "or with none");
    REQUIRE(session.undo());
    band.frame();
    EXPECT((copied() && *copied() == *large), "and undone, with it again");
}

TEST_CASE("the newest whole copy is read, also one an earlier version left in one file")
{
    Band start;
    start.doc.setAvatar(ball(15));
    Copied band("earlier", start);
    // As an earlier version left it: one file with all of it.
    REQUIRE(save(start.doc, band.folder + "/pattern.twintail").empty());
    const std::expected<Recovery, std::string> left = readRecovery(band.folder);
    EXPECT((hasRecovery(band.folder) && left && left->scene.document.avatar()), "it is found and read");

    band.waist(90);
    band.frame();
    EXPECT((!std::filesystem::exists(filePath(band.folder + "/pattern.twintail")) && band.copiedWaist() == 90.0), "a new copy takes its place");

    // Another Autosave in the folder counts on from what is there.
    Autosave next(band.folder);
    band.waist(100);
    next.write(band.session, "band", "");
    REQUIRE(!next.wait());
    EXPECT((band.copiedWaist() == 100.0), "so that its copy is the newest");

    std::ofstream(filePath(band.folder + "/avatar-9.twintail")) << "half";
    EXPECT((band.copiedWaist() == 100.0), "an avatar's file with no pattern, as a stop between the two leaves, is passed over");
    std::filesystem::remove(filePath(band.folder + "/avatar-2.twintail"));
    const std::expected<Recovery, std::string> without = readRecovery(band.folder);
    EXPECT((!without && without.error().find("avatar") != std::string::npos), "a copy without its avatar says so");
    next.write(band.session, "band", "");
    REQUIRE(!next.wait());
    EXPECT((readRecovery(band.folder).has_value()), "and the next copy writes the avatar again");
}

TEST_CASE("a copy that could not be written is tried again after the wait")
{
    const std::string folder = emptyFolder("later") + "/not there yet";
    Band start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    REQUIRE(session.run(SetField{start.waist, Expr(90)}));
    Autosave autosave(folder, 30.0);
    const auto update = [&](double now) {
        autosave.update(session, now, "band", "");
        autosave.wait();
    };
    update(0.0);
    std::filesystem::create_directories(filePath(folder));
    update(10.0);
    update(39.0);
    EXPECT((!hasRecovery(folder)), "not at once, and not again and again");
    update(40.0);
    EXPECT((hasRecovery(folder)), "after the wait");
}

TEST_CASE("a copy stays when the program stops, goes when forgotten, and one that cannot be written is told once")
{
    const std::string folder = emptyFolder("stays");
    Band start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    REQUIRE(session.run(SetField{start.waist, Expr(90)}));
    {
        Autosave autosave(folder, 0.0);
        REQUIRE(!autosave.update(session, 0.0, "band", ""));
    }
    EXPECT((hasRecovery(folder)), "gone without being told to forget, the copy is on disk, whole");
    const std::expected<Recovery, std::string> copy = readRecovery(folder);
    EXPECT((copy && copy->path.empty()), "a pattern that never had a file");
    {
        Autosave autosave(folder, 0.0);
        autosave.forget();
        EXPECT((!hasRecovery(folder) && std::filesystem::is_empty(filePath(folder))), "forgotten, nothing is left");
    }

    std::ofstream(filePath(folder + "/pattern.twintail")) << "broken";
    EXPECT((!readRecovery(folder)), "a copy that cannot be read says so");

    Autosave autosave(folder + "/no such folder", 0.0);
    autosave.write(session, "band", "");
    const std::optional<std::string> first = autosave.wait();
    EXPECT((first && first->find("recovery copy") != std::string::npos), "why there is none");
    autosave.write(session, "band", "");
    EXPECT((!autosave.wait()), "said once");
}
