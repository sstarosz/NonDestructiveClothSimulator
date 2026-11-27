// Example 5: two sides share a base and stay in sync, while one side gets
// its own changes on top.
//
//   1. base        = construction panel: rectangle + curved armhole
//   2. right front = instance of the base
//   3. left front  = mirrored instance of the base, then, only here:
//                    a slanted hem cut and a lowered neck corner
//   4. change the base  ->  both sides follow; the left keeps its extras

#include <cmath>
#include <cstdio>

#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "Expect.h"

using namespace twintailor;

int main() {
    Document doc;

    Param halfChest = doc.add(Parameter{.name = "half_chest", .value = 25_cm});
    Param length    = doc.add(Parameter{.name = "bodice_length", .value = 45_cm});
    Param armhole   = doc.add(Parameter{.name = "armhole_depth", .value = 2_cm});

    // 1. The shared base. It is never cut from fabric itself.
    PanelId      base  = doc.add(Panel{.name = "bodice base", .construction = true});
    OpRef<Rect>  body  = doc.add(base, Rect{.width = halfChest, .height = length});
    OpRef<Curve> scoop = doc.add(base, Curve{.edge = body.right(), .bulge = -armhole});

    // 2. The right side is the base as it is.
    PanelId rightFront = doc.add(Panel{.name = "right front"});
    doc.add(rightFront, Instance{.source = base});

    // 3. The left side is the base mirrored, plus two things of its own.
    //    The base's names (body.left(), ...) work here too.
    PanelId            leftFront = doc.add(Panel{.name = "left front"});
    doc.add(leftFront, Instance{.source = base, .mirrored = true});
    OpRef<Line>        hemLine   = doc.add(leftFront, Line{.from = body.bottomRight(), .angle = 15_deg});
    OpRef<Cut>         hem       = doc.add(leftFront, Cut{.along = hemLine.line(), .keep = Cut::Side::Left});
    OpRef<AdjustPoint> neck      = doc.add(leftFront, AdjustPoint{.corner = body.topLeft(), .dx = 0_cm, .dy = -3_cm});

    Result shapes = evaluate(doc);
    const double rise = 25_cm * std::tan(15_deg * pi / 180);  // how far the hem cut climbs

    std::printf("both sides from one base\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("armholes match", shapes[leftFront].length(body.right()), shapes[rightFront].length(body.right()));
    expect("armhole is curved", shapes[rightFront].length(body.right()) > 45_cm, true);
    expect("right: hem", shapes[rightFront].length(body.bottom()), 25_cm);
    expect("right: centre front", shapes[rightFront].length(body.left()), 45_cm);
    expect("right: has slanted hem", shapes[rightFront].contains(hem.edge()), false);

    std::printf("left side only\n");
    expect("left: has slanted hem", shapes[leftFront].contains(hem.edge()), true);
    expect("left: straight hem exists", shapes[leftFront].contains(body.bottom()), false);
    expect("left: centre front", shapes[leftFront].length(body.left()), 45_cm - rise - 3_cm);
    expect("left: neck corner y", shapes[leftFront].position(body.topLeft()).y, 42_cm);
    expect("right: neck corner y", shapes[rightFront].position(body.topLeft()).y, 45_cm);

    // 4. Edit the base. Both sides update; the left side's cut and neck are
    //    applied again on top.
    doc.set(armhole, 4_cm);
    doc.set(halfChest, 30_cm);

    shapes = evaluate(doc);
    std::printf("base changed\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("armholes match", shapes[leftFront].length(body.right()), shapes[rightFront].length(body.right()));
    expect("right: hem", shapes[rightFront].length(body.bottom()), 30_cm);
    expect("left: shoulder", shapes[leftFront].length(body.top()) > 30_cm, true);  // slanted by the lowered neck
    expect("left: centre front", shapes[leftFront].length(body.left()),
           45_cm - 30_cm * std::tan(15_deg * pi / 180) - 3_cm);
    expect("left: has slanted hem", shapes[leftFront].contains(hem.edge()), true);
    expect("neck adjustment has no errors", shapes.failed(neck), false);
    expect("scoop has no errors", shapes.failed(scoop), false);

    return failures == 0 ? 0 : 1;
}
