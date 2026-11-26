// Integration tests of the recovery copy (libs/session, Autosave.cpp): it is
// written to disk, so it is tested here and not beside the library, whose
// unit tests read and write no files. The window's part is in
// QtWindowTests.cpp.

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "clothcad/session/Autosave.h"

using namespace clothcad;
using namespace clothcad::session;

namespace {

// An empty folder of this test's own, beside the test program.
std::string emptyFolder(const char* name) {
    const std::filesystem::path folder = filePath(CLOTHCAD_TEST_FOLDER) / name;
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

}  // namespace

TEST_CASE("a copy is written when unsaved work is due one, with whose it is, and goes when the work is saved")
{
    const std::string folder = emptyFolder("due");
    Band start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    Autosave autosave(folder, 30.0);
    const auto update = [&](double now) {
        const std::optional<std::string> problem = autosave.update(session, now, "band", "C:/patterns/band.clothcad");
        EXPECT((!problem && !autosave.wait()), "no problem");
    };
    const auto set = [&](int waist) { REQUIRE(session.editor().change([&](Document& doc) { doc.set(start.waist, Expr(waist)); })); };

    update(0.0);
    update(100.0);
    EXPECT((!hasRecovery(folder)), "saved work needs no copy");
    set(90);
    update(100.0);
    update(129.0);
    EXPECT((!hasRecovery(folder)), "not before half a minute of unsaved work");
    update(130.0);
    REQUIRE(hasRecovery(folder));
    std::expected<Recovery, std::string> copy = readRecovery(folder);
    REQUIRE(copy);
    EXPECT((copy->title == "band" && copy->path == "C:/patterns/band.clothcad"), "whose it is");
    EXPECT((asNumber(copy->scene.document.get(start.waist).value) == 90.0), "the work as it was");

    set(100);
    update(150.0);
    EXPECT((asNumber(readRecovery(folder)->scene.document.get(start.waist).value) == 90.0), "the next is not due yet");
    update(160.0);
    EXPECT((asNumber(readRecovery(folder)->scene.document.get(start.waist).value) == 100.0), "and then it is");
    std::filesystem::remove(filePath(folder + "/pattern.clothcad"));
    update(200.0);
    EXPECT((!hasRecovery(folder)), "work that has not changed since is not written again");

    session.setPanelShown(start.panel, false);
    session.simulate(true);
    session.update();
    session.update();
    update(240.0);
    copy = readRecovery(folder);
    REQUIRE(copy);
    EXPECT((copy->scene.state.hidden.size() == 1 && copy->scene.state.drape.has_value()), "the scene is in it, cloth and all");

    session.markSaved();
    update(241.0);
    EXPECT((!hasRecovery(folder)), "saved, the copy goes");
}

TEST_CASE("a copy stays when the program stops, goes when forgotten, and one that cannot be written is told once")
{
    const std::string folder = emptyFolder("stays");
    Band start;
    Session session(start.doc, Threading::OneStepPerAdvance);
    REQUIRE(session.editor().change([&](Document& doc) { doc.set(start.waist, Expr(90)); }));
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

    std::ofstream(filePath(folder + "/pattern.clothcad")) << "broken";
    EXPECT((!readRecovery(folder)), "a copy that cannot be read says so");

    Autosave autosave(folder + "/no such folder", 0.0);
    autosave.write(session, "band", "");
    const std::optional<std::string> first = autosave.wait();
    EXPECT((first && first->find("recovery copy") != std::string::npos), "why there is none");
    autosave.write(session, "band", "");
    EXPECT((!autosave.wait()), "said once");
}
