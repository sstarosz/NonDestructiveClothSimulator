// Example 2: circle skirt whose inner edge always fits the waistband.
//
//   1. waist = 70, skirt_length = 60, fullness = 1 (a full circle)
//   2. waistband = rectangle  waist x 4
//   3. skirt     = ring sector: inner edge = waist, sweep = 360 * fullness
//   4. try fullness 1, 3/4, 1/2, 1/4: the radius changes, the inner edge
//      stays equal to the waistband
//   5. waist = 80  ->  the skirt still fits the waistband

#include <cmath>
#include <cstdio>

#include "core/Document.h"
#include "core/Evaluate.h"
#include "examples/Expect.h"

using namespace clothcad;

int main() {
    Document doc;

    // 1. Named dimensions, and one style choice.
    Param waist       = doc.add(Parameter{.name = "waist", .value = 70_cm});
    Param skirtLength = doc.add(Parameter{.name = "skirt_length", .value = 60_cm});
    Param fullness    = doc.add(Parameter{.name = "fullness", .value = 1});

    // 2. The waistband.
    PanelId     waistband = doc.add(Panel{.name = "waistband"});
    OpRef<Rect> bandRect  = doc.add(waistband, Rect{.width = waist, .height = 4_cm});

    // 3. The skirt. You give the waist; the radius is worked out for you.
    PanelId           skirt = doc.add(Panel{.name = "skirt"});
    OpRef<RingSector> ring  = doc.add(skirt, RingSector{.innerLength = waist,
                                                        .sweep       = 360_deg * fullness,
                                                        .depth       = skirtLength});

    // 4. Try the styles.
    for (double style : {1.0, 0.75, 0.5, 0.25}) {
        doc.set(fullness, style);
        Result shapes = evaluate(doc);

        std::printf("fullness %g\n", style);
        expect("errors", static_cast<double>(shapes.errors.size()), 0);
        expect("skirt waist edge", shapes[skirt].length(ring.inner()), shapes[waistband].length(bandRect.top()));
        expect("skirt side", shapes[skirt].length(ring.startSide()), 60_cm);
        expect("inner radius", shapes[skirt].edge(ring.inner()).arc->radius, 70_cm / (2 * pi * style));
    }

    // 5. Change the shared dimension.
    doc.set(waist, 80_cm);
    Result shapes = evaluate(doc);

    std::printf("waist 80\n");
    expect("waistband top", shapes[waistband].length(bandRect.top()), 80_cm);
    expect("skirt waist edge", shapes[skirt].length(ring.inner()), 80_cm);

    return failures == 0 ? 0 : 1;
}
