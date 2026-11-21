// Performance of the core and the simulation: evaluating, meshing and
// stepping the cloth, judged against tests/benchmarks/baselines/CoreAndSim.txt.
// See tests/benchmarks/Perf.h for how times are judged.

#include <cmath>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/core/File.h"
#include "clothcad/sim/Body.h"
#include "clothcad/sim/Cloth.h"
#include "clothcad/sim/Layout.h"
#include "clothcad/sim/Mesh.h"
#include "Perf.h"

using namespace clothcad;
using namespace clothcad::sim;

// A garment the size of a skirt: front and back, sewn down both sides.
static Document skirt() {
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 120});
    const PanelId front = doc.add(Panel{.name = "front"});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = waist / 2, .height = 80_cm});
    const PanelId back = doc.add(Panel{.name = "back"});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = waist / 2, .height = 80_cm});
    doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    doc.add(Seam{.a = {front, frontRect.left()}, .b = {back, backRect.right()}});
    doc.arrange(front, Arrangement{.placed = true, .x = 0, .y = 90, .z = 26, .wrap = 100});
    doc.arrange(back, Arrangement{.placed = true, .x = 0, .y = 90, .z = -26, .turn = 180, .wrap = 100});
    return doc;
}

// Many panels with many kinds of operation, to time evaluation.
static Document manyPanels() {
    Document doc;
    const Param width = doc.add(Parameter{.name = "width", .value = 30});
    const Param ease = doc.add(Parameter{.name = "ease", .value = width / 10 + 1});
    for (int i = 0; i < 40; ++i) {
        const PanelId panel = doc.add(Panel{.name = "panel"});
        const OpRef<Rect> rect = doc.add(panel, Rect{.width = width + ease, .height = 50_cm});
        doc.add(panel, Bezier{.edge = rect.top(), .startHandle = {8, 6}, .endHandle = {-8, 6}});
        doc.add(panel, AdjustPoint{.corner = rect.bottomRight(), .dx = ease, .dy = 0_cm});
        const OpRef<LineBetween> line = doc.add(panel, LineBetween{.from = {rect.left(), 0.3}, .to = {rect.right(), 0.4}});
        const OpRef<Cut> cut = doc.add(panel, Cut{.along = line.line(), .keep = Cut::Side::Right});
        doc.add(panel, Mirror{.across = cut.farPart(rect.left())});
    }
    return doc;
}

// A closed, smooth surface with about as many triangles as an avatar: an
// egg 30 cm wide and 60 cm tall, standing at chest height.
static Surface egg() {
    Surface surface;
    const int around = 140, rings = 70;
    for (int r = 0; r <= rings; ++r)
        for (int a = 0; a < around; ++a) {
            const double up = pi * r / rings, turn = 2.0 * pi * a / around;
            surface.points.push_back({15.0 * std::sin(up) * std::cos(turn), 100.0 - 30.0 * std::cos(up), 12.0 * std::sin(up) * std::sin(turn)});
        }
    const auto at = [&](int r, int a) { return static_cast<uint32_t>(r * around + a % around); };
    for (int r = 0; r < rings; ++r)
        for (int a = 0; a < around; ++a) {
            surface.triangles.push_back({at(r, a), at(r + 1, a + 1), at(r + 1, a)});
            surface.triangles.push_back({at(r, a), at(r, a + 1), at(r + 1, a + 1)});
        }
    return surface;
}

int main(int argc, char** argv) {
    perf::Report report(argc, argv);
    if (!report.worthRunning()) return 0;

    // ---- Core ----
    const Document many = manyPanels();
    report.time("evaluate 40 panels of 6 operations", 12.0, [&] { evaluate(many); });
    report.time("copy that document (an undo step)", 0.5, [&] { Document copy = many; (void)copy; });
    report.time("write it as text", 3.0, [&] { toText(many); });
    const std::string text = toText(many);
    report.time("read it back", 4.0, [&] { fromText(text); });

    // ---- Meshing ----
    const Document doc = skirt();
    const Result shapes = evaluate(doc);
    report.time("mesh the skirt at 2 cm (2,800 particles)", 25.0, [&] { meshOf(doc, shapes, {.particleDistance = 2_cm}); });
    report.time("mesh the skirt at 1 cm (10,600 particles)", 220.0, [&] { meshOf(doc, shapes, {.particleDistance = 1_cm}); }, 3);
    const ClothMesh mesh = meshOf(doc, shapes, {.particleDistance = 2_cm});
    report.time("arrange its particles in space", 0.5, [&] { arranged(doc, mesh); });

    // ---- Bodies ----
    const Surface surface = egg();
    report.time("prepare a 19,600-triangle body to collide with", 900.0, [&] { DistanceField field(surface); }, 3);
    const Body scanned = bodyFrom(surface), nobody;

    // ---- The cloth ----
    // A step is timed on cloth that has been falling for a second, so that it is in contact and its seams are closing.
    const auto step = [&](const Body& body, Fabric fabric) {
        Cloth cloth(mesh, arranged(doc, mesh), fabric);
        for (int i = 0; i < 60; ++i) cloth.step(body);
        return perf::bestMilliseconds([&] { cloth.step(body); }, 40);
    };
    Fabric plain;
    Fabric through;
    through.collidesWithItself = false;
    report.record("cloth step, 2,800 particles, no body", 16.0, step(nobody, plain));
    report.record("cloth step, 2,800 particles, scanned body", 16.0, step(scanned, plain));
    report.record("cloth step, cloth passing through cloth", 15.0, step(scanned, through));
    {
        Cloth cloth(mesh, arranged(doc, mesh));
        for (int i = 0; i < 60; ++i) cloth.step(scanned);
        report.time("carry the cloth over to the same mesh", 6.0, [&] { Cloth carried = Cloth::continued(mesh, arranged(doc, mesh), cloth, mesh); (void)carried; });
    }
    return report.finish();
}
