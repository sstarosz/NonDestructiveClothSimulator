// Example 3: asymmetric hem made by a cut you can come back to and change.
//
//   1. skirt = rectangle  waist x skirt_length
//   2. internal line from the bottom-left corner at hem_angle = 45 degrees
//   3. cut along the line, keep the upper side
//      -> at 45 degrees the cut is too deep: it runs out through the waist
//   4. hem_angle = 25  ->  same operations, new result
//   5. waist = 80      ->  the cut follows

#include <cmath>
#include <cstdio>

#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "Expect.h"

using namespace twintailor;

int main() {
    Document doc;

    Param waist       = doc.add(Parameter{.name = "waist", .value = 70_cm});
    Param skirtLength = doc.add(Parameter{.name = "skirt_length", .value = 60_cm});
    Param hemAngle    = doc.add(Parameter{.name = "hem_angle", .value = 45_deg, .unit = Unit::Angle});

    // 1-3. Three operations; none of them destroys anything.
    PanelId     skirt   = doc.add(Panel{.name = "skirt"});
    OpRef<Rect> rect    = doc.add(skirt, Rect{.width = waist, .height = skirtLength});
    OpRef<Line> hemLine = doc.add(skirt, Line{.from = rect.bottomLeft(), .angle = hemAngle});
    OpRef<Cut>  hem     = doc.add(skirt, Cut{.along = hemLine.line(), .keep = Cut::Side::Left});

    Result shapes = evaluate(doc);
    std::printf("hem angle 45: too deep\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("right side exists", shapes[skirt].contains(rect.right()), false);
    expect("waist edge", shapes[skirt].length(rect.top()), 60_cm);  // no longer the full 70
    expect("long side", shapes[skirt].length(rect.left()), 60_cm);

    // 4. Go back and change the line. The rectangle is rebuilt and cut again.
    doc.set(hemAngle, 25_deg);
    const double rise = 70_cm * std::tan(25_deg * pi / 180);

    shapes = evaluate(doc);
    std::printf("hem angle 25\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("waist edge", shapes[skirt].length(rect.top()), 70_cm);
    expect("long side", shapes[skirt].length(rect.left()), 60_cm);
    expect("short side", shapes[skirt].length(rect.right()), 60_cm - rise);  // about 27.4
    expect("hem", shapes[skirt].length(hem.edge()), 70_cm / std::cos(25_deg * pi / 180));
    expect("old bottom edge exists", shapes[skirt].contains(rect.bottom()), false);

    // 5. The cut follows the shared dimension too.
    doc.set(waist, 80_cm);

    shapes = evaluate(doc);
    std::printf("waist 80\n");
    expect("waist edge", shapes[skirt].length(rect.top()), 80_cm);
    expect("short side", shapes[skirt].length(rect.right()), 60_cm - 80_cm * std::tan(25_deg * pi / 180));

    return failures == 0 ? 0 : 1;
}
