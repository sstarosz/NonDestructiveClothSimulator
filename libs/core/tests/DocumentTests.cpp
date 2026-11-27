// Unit tests of Document.cpp: the document's verbs and reading.

#include <cmath>
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/Document.h"
#include "twintailor/core/Drawing.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/core/Fields.h"
#include "twintailor/core/File.h"
#include "twintailor/core/Text.h"

using namespace twintailor;

TEST_CASE("copy keeps expressions and is otherwise independent")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId band = doc.add(Panel{.name = "waistband"});
    OpRef<Rect> bandRect = doc.add(band, Rect{.width = waist, .height = 4_cm});

    PanelCopy skirt = doc.copy(band, Panel{.name = "skirt"});
    OpRef<Rect> skirtRect = skirt[bandRect];
    doc.set(skirtRect, &Rect::height, 60_cm);

    EXPECT((!(OpId(skirtRect) == OpId(bandRect))), "the copy has its own operation");
    EXPECT((doc.get(skirt).name == "skirt"), "the copy has its own name");

    doc.set(waist, 80_cm);
    Result result = evaluate(doc);
    EXPECT_NEAR((result[band].length(bandRect.top())), (80.0), "original follows waist");
    EXPECT_NEAR((result[skirt].length(skirtRect.top())), (80.0), "copy follows waist");
    EXPECT_NEAR((result[band].length(bandRect.left())), (4.0), "original keeps its height");
    EXPECT_NEAR((result[skirt].length(skirtRect.left())), (60.0), "copy has its own height");
    EXPECT_THROWS([&] { (void)skirt[skirtRect]; }, std::out_of_range, "only originals can be looked up in a copy");
}

TEST_CASE("document copy is an independent snapshot")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = waist, .height = 4_cm});

    const Document snapshot = doc;  // what undo stores
    doc.set(waist, 80_cm);
    doc.rename(panel, "renamed");

    EXPECT_NEAR((evaluate(snapshot)[panel].length(rect.top())), (70.0), "snapshot keeps the old value");
    EXPECT_NEAR((evaluate(doc)[panel].length(rect.top())), (80.0), "document has the new value");
    EXPECT((snapshot.get(panel).name == "panel"), "snapshot keeps the old name");
}

TEST_CASE("remove, rename and move")
{
    Document doc;
    Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    PanelId a = doc.add(Panel{.name = "a"});
    PanelId b = doc.add(Panel{.name = "b"});
    OpRef<Rect> r1 = doc.add(a, Rect{.width = 1_cm, .height = 1_cm});
    OpRef<Rect> r2 = doc.add(a, Rect{.width = 2_cm, .height = 2_cm});
    OpRef<Rect> r3 = doc.add(a, Rect{.width = 3_cm, .height = 3_cm});

    doc.rename(waist, "waistline");
    EXPECT((doc.get(waist).name == "waistline"), "parameter renamed");

    doc.move(r3, 0);
    EXPECT((doc.operations(a) == std::vector<OpId>{r3, r1, r2}), "moved to the front");
    doc.move(r3, 2);
    EXPECT((doc.operations(a) == std::vector<OpId>{r1, r2, r3}), "moved to the back");

    doc.remove(r2);
    EXPECT((doc.operations(a) == std::vector<OpId>{r1, r3}), "operation removed");
    EXPECT((!doc.contains(OpId(r2))), "removed operation is gone");

    doc.remove(b);
    EXPECT((doc.panels() == std::vector<PanelId>{a}), "panel removed");
}

TEST_CASE("unknown handles throw")
{
    Document doc;
    Document other;
    Param foreign = other.add(Parameter{.name = "x", .value = 1});
    PanelId panel = doc.add(Panel{.name = "panel"});
    OpRef<Rect> rect = doc.add(panel, Rect{.width = 1_cm, .height = 1_cm});
    doc.remove(panel);

    EXPECT_THROWS([&] { doc.set(foreign, 2); }, std::out_of_range, "set unknown parameter");
    EXPECT_THROWS([&] { (void)doc.get(panel); }, std::out_of_range, "get removed panel");
    EXPECT_THROWS([&] { doc.add(panel, Rect{}); }, std::out_of_range, "add to removed panel");
    EXPECT_THROWS([&] { doc.set(rect, &Rect::width, 2_cm); }, std::out_of_range, "set on removed operation");
    EXPECT_THROWS([&] { doc.remove(OpId(rect)); }, std::out_of_range, "remove removed operation");
}

namespace {

// A front and a back leg, sewn down both sides (one seam twisted, so that
// `reversed` has something to keep).
struct Legs {
    Document doc;
    PanelId front, back;
    OpRef<Rect> frontRect, backRect;
    Legs() {
        front = doc.add(Panel{.name = "front"});
        frontRect = doc.add(front, Rect{.width = 20_cm, .height = 90_cm});
        back = doc.add(Panel{.name = "back"});
        backRect = doc.add(back, Rect{.width = 24_cm, .height = 90_cm});
        doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
        doc.add(Seam{.a = {back, backRect.right()}, .b = {front, frontRect.left()}, .reversed = true});
    }
    PanelId instance(PanelId source, bool mirrored = true) {
        const PanelId panel = doc.add(Panel{.name = "instance"});
        doc.add(panel, Instance{.source = source, .mirrored = mirrored});
        return panel;
    }
    // Whether a seam joins these two sides, in this order.
    bool sewn(const SeamSide& a, const SeamSide& b, bool reversed = false) const {
        for (SeamId id : doc.seams())
            if (doc.get(id).a == a && doc.get(id).b == b && doc.get(id).reversed == reversed) return true;
        return false;
    }
};

} // namespace

TEST_CASE("an instance is sewn as its source once the other side has an instance too, whichever comes first")
{
    for (const bool frontFirst : {true, false}) {
        Legs legs;
        const PanelId first = legs.instance(frontFirst ? legs.front : legs.back);
        EXPECT((legs.doc.sewAsSource(first).empty() && legs.doc.seams().size() == 2), "alone, there is nothing to sew to");
        const PanelId second = legs.instance(frontFirst ? legs.back : legs.front);
        const std::vector<SeamId> added = legs.doc.sewAsSource(second);
        const PanelId front = frontFirst ? first : second, back = frontFirst ? second : first;
        EXPECT((added.size() == 2 && legs.doc.seams().size() == 4), "with both there, both seams come");
        EXPECT((legs.sewn({front, legs.frontRect.right()}, {back, legs.backRect.left()})), "the same two edges, on the same sides");
        EXPECT((legs.sewn({back, legs.backRect.right()}, {front, legs.frontRect.left()}, true)), "and reversed stays reversed");
        EXPECT((legs.doc.sewAsSource(first).empty() && legs.doc.sewAsSource(second).empty() && legs.doc.seams().size() == 4), "asked again, nothing is added twice");
    }
}

TEST_CASE("a seam within the source is made within the instance")
{
    Legs legs;
    const PanelId tube = legs.doc.add(Panel{.name = "tube"});
    const OpRef<Rect> rect = legs.doc.add(tube, Rect{.width = 30_cm, .height = 60_cm});
    legs.doc.add(Seam{.a = {tube, rect.left()}, .b = {tube, rect.right()}});
    const PanelId other = legs.instance(tube);
    EXPECT((legs.doc.sewAsSource(other).size() == 1 && legs.sewn({other, rect.left()}, {other, rect.right()})), "the instance is closed as its source is");
    EXPECT((legs.doc.sewAsSource(other).empty() && legs.doc.seams().size() == 4), "once");
}

TEST_CASE("a seam from the source to its own instance is not copied")
{
    Legs legs;
    const PanelId front = legs.instance(legs.front);
    // The centre seam, by hand: the front to its mirror image.
    legs.doc.add(Seam{.a = {legs.front, legs.frontRect.top()}, .b = {front, legs.frontRect.top()}});
    EXPECT((legs.doc.sewAsSource(front).empty()), "the other side has no instance of its own");
    const PanelId back = legs.instance(legs.back);
    EXPECT((legs.doc.sewAsSource(back).size() == 2 && legs.doc.seams().size() == 5), "the leg seams come, the centre seam stays alone");
    EXPECT((legs.doc.sewAsSource(front).empty()), "and nothing more for the first");
}

TEST_CASE("a second pair of instances is sewn to itself, not to the first")
{
    Legs legs;
    const PanelId front1 = legs.instance(legs.front), back1 = legs.instance(legs.back);
    CHECK(legs.doc.sewAsSource(back1).size() == 2);
    const PanelId front2 = legs.instance(legs.front);
    EXPECT((legs.doc.sewAsSource(front2).empty()), "the one instance of the back is taken");
    const PanelId back2 = legs.instance(legs.back);
    EXPECT((legs.doc.sewAsSource(back2).size() == 2), "the second pair is sewn");
    EXPECT((legs.sewn({front2, legs.frontRect.right()}, {back2, legs.backRect.left()}) && legs.sewn({back2, legs.backRect.right()}, {front2, legs.frontRect.left()}, true)),
           "to each other");
    EXPECT((legs.doc.sewAsSource(front1).empty() && legs.doc.sewAsSource(back1).empty() && legs.doc.seams().size() == 6), "and the first pair keeps to itself");
}

TEST_CASE("the newest free instance is the other side of a copied seam")
{
    Legs legs;
    const PanelId older = legs.instance(legs.back), newer = legs.instance(legs.back);
    const PanelId front = legs.instance(legs.front);
    CHECK(legs.doc.sewAsSource(front).size() == 2);
    EXPECT((legs.sewn({front, legs.frontRect.right()}, {newer, legs.backRect.left()})), "the newer one");
    EXPECT((legs.doc.sewAsSource(older).empty()), "the older finds the front taken");
}

TEST_CASE("a plain instance and a mirrored one are not sewn together by their sources' seams")
{
    Legs legs;
    const PanelId front = legs.instance(legs.front, false), mirrored = legs.instance(legs.back, true);
    EXPECT((legs.doc.sewAsSource(front).empty() && legs.doc.sewAsSource(mirrored).empty() && legs.doc.seams().size() == 2), "one of each kind: nothing");
    const PanelId plain = legs.instance(legs.back, false);
    EXPECT((legs.doc.sewAsSource(plain).size() == 2 && legs.sewn({front, legs.frontRect.right()}, {plain, legs.backRect.left()})), "two plain ones are sewn");
}

TEST_CASE("an instance of an instance is sewn as the instance it follows")
{
    Legs legs;
    const PanelId front = legs.instance(legs.front), back = legs.instance(legs.back);
    CHECK(legs.doc.sewAsSource(back).size() == 2);
    const PanelId frontOfFront = legs.instance(front, false);
    EXPECT((legs.doc.sewAsSource(frontOfFront).empty()), "the other side is not there yet");
    const PanelId backOfBack = legs.instance(back, false);
    EXPECT((legs.doc.sewAsSource(backOfBack).size() == 2), "the seams of the instances are copied on");
    EXPECT((legs.sewn({frontOfFront, legs.frontRect.right()}, {backOfBack, legs.backRect.left()})), "between the two that follow them");
}

TEST_CASE("a copied seam never goes to the panel the instance follows")
{
    Legs legs;
    const PanelId mirrored = legs.instance(legs.front);
    legs.doc.add(Seam{.a = {mirrored, legs.frontRect.top()}, .b = {legs.front, legs.frontRect.top()}});
    // The mirrored front is itself a mirrored instance of the front, its neighbour here.
    const PanelId further = legs.instance(mirrored);
    EXPECT((legs.doc.sewAsSource(further).empty() && legs.doc.seams().size() == 3), "there is no other instance of the front to sew to");
}

TEST_CASE("only an instance is sewn as its source")
{
    Legs legs;
    EXPECT((!legs.doc.instanceOf(legs.front) && legs.doc.sewAsSource(legs.front).empty()), "a panel with its own shape adds nothing");
    const PanelId front = legs.instance(legs.front), back = legs.instance(legs.back, false);
    REQUIRE(legs.doc.instanceOf(front).has_value());
    EXPECT((legs.doc.instanceOf(front)->source == legs.front && legs.doc.instanceOf(front)->mirrored && !legs.doc.instanceOf(back)->mirrored), "it says what it follows, and how");
    legs.doc.setEnabled(legs.doc.operations(back).front(), false);
    EXPECT((!legs.doc.instanceOf(back) && legs.doc.sewAsSource(back).empty()), "switched off, it follows nothing");
    legs.doc.remove(back);
    EXPECT_THROWS([&] { legs.doc.sewAsSource(back); }, std::out_of_range, "an unknown panel");
    EXPECT_THROWS([&] { (void)legs.doc.instanceOf(back); }, std::out_of_range, "an unknown panel to read");
}

TEST_CASE("the avatar is part of the document: a copy shares it, and putting on another leaves the copy's")
{
    Document doc;
    EXPECT((!doc.avatar()), "a new document has no body");
    const auto first = std::make_shared<const Avatar>(Avatar{.name = "first.obj", .points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, .triangles = {{0, 1, 2}}});
    doc.setAvatar(first);
    const Document before = doc;
    EXPECT((before.avatar() == first), "the copy has the same one, not a copy of its points");
    doc.setAvatar(std::make_shared<const Avatar>(Avatar{.name = "second.obj", .points = {}, .triangles = {}}));
    EXPECT((before.avatar() == first && doc.avatar()->name == "second.obj"), "as undo needs it");
    doc.setAvatar(nullptr);
    EXPECT((!doc.avatar()), "and it can be taken off");
}
