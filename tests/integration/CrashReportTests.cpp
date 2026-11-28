// Integration tests of the log of actions and the crash report (libs/session,
// CrashReport.cpp): they are files, so they are tested here and not beside the
// library, whose unit tests read and write no files. The window's part is in
// QtWindowTests.cpp.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/session/Autosave.h"
#include "twintailor/session/CrashDump.h"
#include "twintailor/session/CrashReport.h"

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

std::vector<std::string> linesOf(const std::string& file) {
    std::ifstream in(filePath(file));
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
}

// What the lines say, without their times.
std::vector<std::string> saidIn(const std::string& file) {
    std::vector<std::string> said = linesOf(file);
    for (std::size_t i = 1; i < said.size(); ++i) said[i] = said[i].substr(said[i].find(" s  ") + 4);
    return said;
}

bool there(const std::string& file) { return std::filesystem::exists(filePath(file)); }

} // namespace

TEST_CASE("a line of the log is on disk at once, and old lines go while the heading stays")
{
    const std::string file = actionLogIn(emptyFolder("log"));
    ActionLog log(file, "Twintailor 1.2.3", 3);
    log.note("first");
    log.note("an error\nin two lines");
    EXPECT((saidIn(file) == std::vector<std::string>{"Twintailor 1.2.3", "first", "an error in two lines"}), "written while the log is open");
    EXPECT((linesOf(file)[1].find("0.0") != std::string::npos), "with the time since it began");
    for (int i = 0; i < 20; ++i) log.note("line " + std::to_string(i));
    const std::vector<std::string> said = saidIn(file);
    EXPECT((said.front() == "Twintailor 1.2.3" && said.back() == "line 19" && said.size() >= 4 && said.size() <= 7),
           "the heading and the latest lines, never many more than asked for");
}

TEST_CASE("the session notes what is done before it is done, a drag once, and no names")
{
    const std::string file = actionLogIn(emptyFolder("session"));
    Document doc;
    const Param waist = doc.add(Parameter{.name = "secret waist", .value = 80});
    const PanelId panel = doc.add(Panel{.name = "secret jacket"});
    doc.add(panel, Rect{.width = waist, .height = 20_cm});
    Session session(doc, Threading::OneStepPerAdvance);
    session.keepLog(file, "heading");

    REQUIRE(session.run(SetField{waist, Expr(90)}));
    const Edit drag = session.begin();
    REQUIRE(session.preview(drag, MovePanels{{panel}, {1.0, 0.0}}));
    REQUIRE(session.preview(drag, MovePanels{{panel}, {2.0, 0.0}}));
    session.commit(drag);
    const Edit given = session.begin();
    REQUIRE(session.preview(given, MovePanels{{panel}, {3.0, 0.0}}));
    session.cancel(given);
    REQUIRE((session.undo() && session.redo()));
    session.simulate(true);
    session.simulate(true);
    session.update();
    const Edit pull = session.begin();
    const Vec3 up = session.cloth().positions.front()[0] + Vec3{0, 0, 5};
    REQUIRE((session.preview(pull, PullCloth{panel, 0, up}) && session.preview(pull, PullCloth{panel, 0, up})));
    session.commit(pull);
    session.simulate(false);
    session.setPanelShown(panel, false);
    session.setPanelShown(panel, false);
    session.setPanelActive(panel, false);
    session.setBodySeeThrough(true);
    REQUIRE(session.run(ResetCloth{}));
    static_cast<void>(session.obj());  // nothing to export of a hidden panel: noted all the same
    session.note("Save");
    session.open(Document{});

    const std::vector<std::string> expected{"heading",       "SetField",        "MovePanels, previewed", "Commit",          "MovePanels, previewed",
                                            "Cancel",        "Undo",            "Redo",                  "Play",            "PullCloth",
                                            "Commit",        "Pause",           "Hide a panel",          "Deactivate a panel", "See through the body",
                                            "ResetCloth",    "Export OBJ",      "Save",                  "Open a pattern"};
    std::string all = "each thing once, in order:";
    for (const std::string& said : saidIn(file)) all += "\n  " + said;
    EXPECT((saidIn(file) == expected), all);
    for (const std::string& line : linesOf(file)) EXPECT((line.find("secret") == std::string::npos), "no name of the pattern's");

    session.endLog();
    EXPECT((!there(file)), "the run ends as it should: no log");
    session.note("after");
    EXPECT((!there(file)), "and nothing more is noted");
}

TEST_CASE("a run that left a log gets a report with it and the pattern last copied; the last few stay")
{
    const std::string folder = emptyFolder("report"), run = folder + "/run", reports = folder + "/crash-reports";
    std::filesystem::create_directories(filePath(run));
    EXPECT((!keepCrashReport(run, reports, "2026-01-01")), "no log, no report");
    EXPECT((!there(reports)), "and nothing made");

    Document doc;
    doc.add(Panel{.name = "band"});
    {
        Session session(doc, Threading::OneStepPerAdvance);
        session.keepLog(actionLogIn(run), "heading");
        session.note("Play");
        Autosave autosave(run);
        session.markUnsaved();
        REQUIRE(autosave.writeNow(session, "band", ""));
    }
    std::ofstream(filePath(crashDumpIn(run))) << "a dump";

    const std::optional<std::string> report = keepCrashReport(run, reports, "2026-01-02");
    REQUIRE(report);
    EXPECT((*report == reports + "/2026-01-02" && saidIn(actionLogIn(*report)).back() == "Play" && !there(actionLogIn(run))), "the log is moved into it");
    const std::variant<SavedScene, FileError> kept = twintailor::open(*report + "/pattern.twintail");
    EXPECT((std::holds_alternative<SavedScene>(kept) && std::get<SavedScene>(kept).document.panels().size() == 1), "with the pattern, which opens");
    EXPECT((there(crashDumpIn(*report)) && !there(crashDumpIn(run))), "and the crash dump");
    EXPECT((hasRecovery(run)), "and the recovery copy is left to be offered");
    EXPECT((!keepCrashReport(run, reports, "2026-01-02")), "reported once");

    std::ofstream(filePath(crashDumpIn(run))) << "a dump";
    const std::optional<std::string> alone = keepCrashReport(run, reports, "2025-12-31");
    EXPECT((alone && there(crashDumpIn(*alone))), "a dump alone is a report too");

    std::filesystem::create_directories(filePath(reports + "/mine"));
    for (const char* name : {"2026-01-02", "2026-01-03", "2026-01-04", "2026-01-05"}) {
        std::ofstream(filePath(actionLogIn(run))) << "heading\n";
        REQUIRE(keepCrashReport(run, reports, name, 3));
    }
    EXPECT((there(reports + "/2026-01-05") && there(reports + "/2026-01-04") && there(reports + "/2026-01-03")), "the last three stay");
    EXPECT((!there(reports + "/2026-01-02") && !there(reports + "/2026-01-02 (2)") && !there(reports + "/2025-12-31")),
           "older ones go, also the second of one name and one with a dump alone");
    EXPECT((there(reports + "/mine")), "a folder that is no report is left alone");
}
