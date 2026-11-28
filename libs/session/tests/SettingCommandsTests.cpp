// Unit tests of SettingCommands.cpp: panel names and settings, parameters and
// the avatar.

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/session/Commands.h"
#include "twintailor/session/Editor.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

struct Box {
    Document doc;
    PanelId panel;
    Box() {
        panel = doc.add(Panel{.name = "box"});
        doc.add(panel, Rect{.width = 20_cm, .height = 10_cm});
    }
};

} // namespace

TEST_CASE("a panel is renamed, but not to nothing")
{
    Box start;
    Editor editor(start.doc);
    REQUIRE(editor.run(RenamePanel{start.panel, "bodice"}));
    EXPECT((editor.document().get(start.panel).name == "bodice"), "renamed");
    const auto empty = editor.run(RenamePanel{start.panel, "  "});
    EXPECT((!empty && empty.error().find("needs a name") != std::string::npos && editor.document().get(start.panel).name == "bodice"), "a name of spaces is none");
}

TEST_CASE("a panel is made a base for instances, and given a particle distance within its limits")
{
    Box start;
    Editor editor(start.doc);
    REQUIRE(editor.run(SetConstruction{start.panel, true}));
    EXPECT((editor.document().get(start.panel).construction), "a base");
    REQUIRE(editor.run(SetParticleDistance{start.panel, 1.5_cm}));
    EXPECT((editor.document().get(start.panel).particleDistance == 1.5), "finer");
    const auto fine = editor.run(SetParticleDistance{start.panel, 0.1_cm});
    EXPECT((!fine && fine.error().find("Between") == 0), "too fine");
    const Param wide = start.doc.add(Parameter{.name = "wide", .value = 2.0});
    EXPECT((!editor.run(SetParticleDistance{start.panel, wide}) && editor.document().get(start.panel).particleDistance == 1.5), "a formula is not taken");
}

TEST_CASE("a parameter is added with the first free name, renamed to one that formulas can use, and removed")
{
    Box start;
    Editor editor(start.doc);
    const auto first = editor.run(AddParameter{});
    const auto second = editor.run(AddParameter{});
    const auto waist = editor.run(AddParameter{.name = "waist", .value = 70_cm});
    REQUIRE((first && second && waist));
    EXPECT((editor.document().get(first->parameter).name == "p1" && editor.document().get(second->parameter).name == "p2"), "numbered");
    EXPECT((editor.shapes().ok() && editor.document().get(waist->parameter).name == "waist" && asNumber(editor.document().get(waist->parameter).value) == 70.0), "or as asked");

    const auto taken = editor.run(RenameParameter{first->parameter, "waist"});
    EXPECT((!taken && taken.error().find("already a parameter") != std::string::npos), "a name is used once");
    const auto odd = editor.run(RenameParameter{first->parameter, "two words"});
    EXPECT((!odd && odd.error().find("can't be a parameter name") != std::string::npos), "and a formula must be able to say it");
    EXPECT((!editor.run(AddParameter{.name = "pi"})), "also for a new one");
    REQUIRE(editor.run(RenameParameter{first->parameter, "hip"}));
    REQUIRE(editor.run(RemoveParameter{first->parameter}));
    EXPECT((editor.document().parameters().size() == 2), "removed");
}

TEST_CASE("putting on an avatar is a command and an undo step, and no outline is worked out again")
{
    Box start;
    Editor editor(start.doc);
    const std::uint64_t shapes = editor.shapesVersion();
    const auto avatar = std::make_shared<const Avatar>();
    REQUIRE(editor.run(SetAvatar{avatar}));
    EXPECT((editor.document().avatar() == avatar && editor.shapesVersion() == shapes), "worn");
    EXPECT((editor.undo() && !editor.document().avatar()), "undo takes it off");
}
