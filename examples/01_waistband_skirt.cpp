// Example 1: waistband and rectangle skirt sharing the waist dimension.
//
//   1. waist = 70, skirt_length = 60
//   2. waistband = rectangle  waist x 4
//   3. skirt     = copy of the waistband, height changed to skirt_length
//   4. waist = 80  ->  both panels are 80 wide, the skirt is still 60 long

#include <cmath>
#include <cstdio>

#include "core/Document.h"
#include "core/Evaluate.h"
#include "examples/Expect.h"

using namespace clothcad;

int main() {
    Document doc;

    // 1. Named dimensions.
    Param waist       = doc.add(Parameter{.name = "waist", .value = 70_cm});
    Param skirtLength = doc.add(Parameter{.name = "skirt_length", .value = 60_cm});

    // 2. The waistband.
    PanelId     waistband = doc.add(Panel{.name = "waistband"});
    OpRef<Rect> bandRect  = doc.add(waistband, Rect{.width = waist, .height = 4_cm});

    // 3. The skirt: a copy whose width still refers to waist.
    PanelCopy   skirt     = doc.copy(waistband, Panel{.name = "skirt"});
    OpRef<Rect> skirtRect = skirt[bandRect];
    doc.set(skirtRect, &Rect::height, skirtLength);

    Result shapes = evaluate(doc);
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("waistband top", shapes[waistband].length(bandRect.top()), 70_cm);
    expect("waistband left", shapes[waistband].length(bandRect.left()), 4_cm);
    expect("skirt top", shapes[skirt].length(skirtRect.top()), 70_cm);
    expect("skirt left", shapes[skirt].length(skirtRect.left()), 60_cm);

    // 4. Change the one shared dimension.
    doc.set(waist, 80_cm);

    shapes = evaluate(doc);
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("waistband top", shapes[waistband].length(bandRect.top()), 80_cm);
    expect("skirt top", shapes[skirt].length(skirtRect.top()), 80_cm);
    expect("skirt left", shapes[skirt].length(skirtRect.left()), 60_cm);

    return failures == 0 ? 0 : 1;
}
