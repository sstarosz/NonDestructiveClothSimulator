// Example 4: a sleeve with its own symmetry, mirrored again for the other arm.
//
//   1. left sleeve  = half a sleeve (rectangle, raised crown, free-curve
//                     cap), mirrored across its centre line
//   2. right sleeve = mirrored instance of the whole left sleeve
//   3. bicep = 36  ->  both halves of both sleeves follow; the hem still
//                      fits the wrist

#include <cmath>
#include <cstdio>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"
#include "Expect.h"

using namespace clothcad;

int main() {
    Document doc;

    Param bicep        = doc.add(Parameter{.name = "bicep", .value = 30_cm});
    Param sleeveLength = doc.add(Parameter{.name = "sleeve_length", .value = 60_cm});
    Param capHeight    = doc.add(Parameter{.name = "cap_height", .value = 12_cm});
    Param wrist        = doc.add(Parameter{.name = "wrist", .value = 22_cm});

    // 1. You draw one half; its left edge is the centre line. The crown is
    //    raised on the centre line and the cap runs to it as one S-shaped
    //    curve, level at both ends so the halves join smoothly.
    PanelId       leftSleeve = doc.add(Panel{.name = "left sleeve"});
    OpRef<Rect>   half       = doc.add(leftSleeve, Rect{.width = bicep / 2, .height = sleeveLength});
    doc.add(leftSleeve, AdjustPoint{.corner = half.topLeft(), .dx = 0_cm, .dy = capHeight});
    OpRef<Bezier> cap        = doc.add(leftSleeve, Bezier{.edge = half.top(),
                                                         .startHandle = {-bicep / 4, 0_cm},
                                                         .endHandle   = {bicep / 4, 0_cm}});
    //    The hem corner moves in so the sleeve narrows to the wrist.
    doc.add(leftSleeve, AdjustPoint{.corner = half.bottomRight(), .dx = (wrist - bicep) / 2, .dy = 0_cm});
    OpRef<Mirror> fold       = doc.add(leftSleeve, Mirror{.across = half.left()});

    // 2. The other arm.
    PanelId         rightSleeve = doc.add(Panel{.name = "right sleeve"});
    OpRef<Instance> otherArm    = doc.add(rightSleeve, Instance{.source = leftSleeve, .mirrored = true});

    Result shapes = evaluate(doc);
    const Pattern* left = &shapes[leftSleeve];
    const Pattern* right = &shapes[rightSleeve];

    std::printf("left sleeve\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("hem, drawn half", left->length(half.bottom()), 11_cm);
    expect("hem, mirrored half", left->length(fold.image(half.bottom())), 11_cm);
    expect("cap halves match", left->length(fold.image(half.top())), left->length(half.top()));
    expect("cap is longer than straight", left->length(half.top()) > std::hypot(15_cm, 12_cm), true);
    expect("crown height", left->position(half.topLeft()).y, 72_cm);
    expect("cap arrives level at crown", left->edge(half.top()).cubic->control2.y, 72_cm);
    expect("centre line still an edge", left->contains(half.left()), false);
    expect("underarm seam", left->length(fold.image(half.right())), std::hypot(4_cm, 60_cm));

    std::printf("right sleeve\n");
    expect("is flipped", right->flipped, true);
    expect("hem, drawn half", right->length(half.bottom()), 11_cm);
    expect("cap matches left sleeve", right->length(half.top()), left->length(half.top()));
    expect("underarm x, left sleeve", left->position(half.topRight()).x, 15_cm);
    expect("underarm x, right sleeve", right->position(half.topRight()).x, -15_cm);

    // 3. One edit reaches all four halves.
    doc.set(bicep, 36_cm);
    doc.set(capHeight, 14_cm);

    shapes = evaluate(doc);
    left = &shapes[leftSleeve];
    right = &shapes[rightSleeve];

    std::printf("bicep 36\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("left underarm x", left->position(half.topRight()).x, 18_cm);
    expect("left underarm x, mirrored", left->position(fold.image(half.topRight())).x, -18_cm);
    expect("right underarm x", right->position(half.topRight()).x, -18_cm);
    expect("hem still fits the wrist", left->length(half.bottom()) + left->length(fold.image(half.bottom())), 22_cm);
    expect("crown height", left->position(half.topLeft()).y, 74_cm);
    expect("handles follow bicep", left->edge(half.top()).cubic->control1.x, 18_cm - 9_cm);
    expect("free curve has no errors", shapes.failed(cap), false);
    expect("instance has no errors", shapes.failed(otherArm), false);

    return failures == 0 ? 0 : 1;
}
