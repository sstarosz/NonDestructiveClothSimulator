#include "app/Samples.h"

namespace clothcad::app {

namespace {

Document waistbandAndSkirt() {
    Document doc;
    Param waist       = doc.add(Parameter{.name = "waist", .value = 70_cm});
    Param skirtLength = doc.add(Parameter{.name = "skirt_length", .value = 60_cm});

    PanelId     waistband = doc.add(Panel{.name = "waistband"});
    OpRef<Rect> bandRect  = doc.add(waistband, Rect{.width = waist, .height = 4_cm});

    PanelCopy skirt = doc.copy(waistband, Panel{.name = "skirt"});
    doc.set(skirt[bandRect], &Rect::height, skirtLength);
    return doc;
}

Document circleSkirt() {
    Document doc;
    Param waist       = doc.add(Parameter{.name = "waist", .value = 70_cm});
    Param skirtLength = doc.add(Parameter{.name = "skirt_length", .value = 60_cm});
    Param fullness    = doc.add(Parameter{.name = "fullness", .value = 0.5});

    PanelId waistband = doc.add(Panel{.name = "waistband"});
    doc.add(waistband, Rect{.width = waist, .height = 4_cm});

    PanelId skirt = doc.add(Panel{.name = "skirt"});
    doc.add(skirt, RingSector{.innerLength = waist, .sweep = 360_deg * fullness, .depth = skirtLength});
    return doc;
}

Document asymmetricHem() {
    Document doc;
    Param waist       = doc.add(Parameter{.name = "waist", .value = 70_cm});
    Param skirtLength = doc.add(Parameter{.name = "skirt_length", .value = 60_cm});
    Param hemAngle    = doc.add(Parameter{.name = "hem_angle", .value = 25_deg});

    PanelId     skirt   = doc.add(Panel{.name = "skirt"});
    OpRef<Rect> rect    = doc.add(skirt, Rect{.width = waist, .height = skirtLength});
    OpRef<Line> hemLine = doc.add(skirt, Line{.from = rect.bottomLeft(), .angle = hemAngle});
    doc.add(skirt, Cut{.along = hemLine.line(), .keep = Cut::Side::Left});
    return doc;
}

Document mirroredSleeve() {
    Document doc;
    Param bicep        = doc.add(Parameter{.name = "bicep", .value = 30_cm});
    Param sleeveLength = doc.add(Parameter{.name = "sleeve_length", .value = 60_cm});
    Param capHeight    = doc.add(Parameter{.name = "cap_height", .value = 12_cm});
    Param wrist        = doc.add(Parameter{.name = "wrist", .value = 22_cm});

    // Half a sleeve; its left edge is the centre line. The crown is raised on
    // the centre line, and the cap runs to it as one S-shaped curve that is
    // level at both ends, so the two halves join smoothly.
    PanelId     leftSleeve = doc.add(Panel{.name = "left sleeve"});
    OpRef<Rect> half       = doc.add(leftSleeve, Rect{.width = bicep / 2, .height = sleeveLength});
    doc.add(leftSleeve, AdjustPoint{.corner = half.topLeft(), .dx = 0_cm, .dy = capHeight});
    doc.add(leftSleeve, Bezier{.edge = half.top(), .startHandle = {-bicep / 4, 0_cm}, .endHandle = {bicep / 4, 0_cm}});
    doc.add(leftSleeve, AdjustPoint{.corner = half.bottomRight(), .dx = (wrist - bicep) / 2, .dy = 0_cm});  // narrow to the wrist
    doc.add(leftSleeve, Mirror{.across = half.left()});

    PanelId rightSleeve = doc.add(Panel{.name = "right sleeve"});
    doc.add(rightSleeve, Instance{.source = leftSleeve, .mirrored = true});
    return doc;
}

Document sharedBase() {
    Document doc;
    Param halfChest = doc.add(Parameter{.name = "half_chest", .value = 25_cm});
    Param length    = doc.add(Parameter{.name = "bodice_length", .value = 45_cm});
    Param armhole   = doc.add(Parameter{.name = "armhole_depth", .value = 2_cm});
    Param hemAngle  = doc.add(Parameter{.name = "left_hem_angle", .value = 15_deg});
    Param neckDrop  = doc.add(Parameter{.name = "left_neck_drop", .value = 3_cm});

    PanelId     base = doc.add(Panel{.name = "bodice base", .construction = true});
    OpRef<Rect> body = doc.add(base, Rect{.width = halfChest, .height = length});
    doc.add(base, Curve{.edge = body.right(), .bulge = -armhole});

    PanelId rightFront = doc.add(Panel{.name = "right front"});
    doc.add(rightFront, Instance{.source = base});

    PanelId     leftFront = doc.add(Panel{.name = "left front"});
    doc.add(leftFront, Instance{.source = base, .mirrored = true});
    OpRef<Line> hemLine   = doc.add(leftFront, Line{.from = body.bottomRight(), .angle = hemAngle});
    doc.add(leftFront, Cut{.along = hemLine.line(), .keep = Cut::Side::Left});
    doc.add(leftFront, AdjustPoint{.corner = body.topLeft(), .dx = 0_cm, .dy = -neckDrop});
    return doc;
}

} // namespace

const std::vector<Sample>& samples() {
    static const std::vector<Sample> all = {
        {"1. Waistband and skirt", waistbandAndSkirt},
        {"2. Circle skirt", circleSkirt},
        {"3. Asymmetric hem", asymmetricHem},
        {"4. Mirrored sleeve", mirroredSleeve},
        {"5. Shared base", sharedBase},
    };
    return all;
}

} // namespace clothcad::app
