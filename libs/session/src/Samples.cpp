#include "clothcad/session/Samples.h"

#include "clothcad/core/Drawing.h"
#include "clothcad/core/Evaluate.h"

namespace clothcad::session {

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
    Param fullness    = doc.add(Parameter{.name = "fullness", .value = 0.5, .unit = Unit::None});

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
    Param hemAngle    = doc.add(Parameter{.name = "hem_angle", .value = 25_deg, .unit = Unit::Angle});

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
    Param hemAngle  = doc.add(Parameter{.name = "left_hem_angle", .value = 15_deg, .unit = Unit::Angle});
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

// A T-shirt body, front and back, each a mirrored half shaped by adding
// points: a neck point on the top, an underarm point on the side. As
// examples/06_tshirt_body.cpp.
Document tshirtBody() {
    Document doc;
    Param chest         = doc.add(Parameter{.name = "chest", .value = 96_cm});
    Param ease          = doc.add(Parameter{.name = "ease", .value = 8_cm});
    Param length        = doc.add(Parameter{.name = "length", .value = 66_cm});
    Param neckWidth     = doc.add(Parameter{.name = "neck_width", .value = 8_cm});
    Param frontNeckDrop = doc.add(Parameter{.name = "front_neck_drop", .value = 8_cm});
    Param backNeckDrop  = doc.add(Parameter{.name = "back_neck_drop", .value = 2_cm});
    Param shoulderDrop  = doc.add(Parameter{.name = "shoulder_drop", .value = 4_cm});
    Param armholeDepth  = doc.add(Parameter{.name = "armhole_depth", .value = 22_cm});
    const Expr quarter = (chest + ease) / 4;

    struct Half {
        PanelId panel;
        OpRef<Rect> body;
        OpRef<Mirror> fold;
    };
    const auto half = [&](const char* name, Param neckDrop) {
        const PanelId panel = doc.add(Panel{.name = name});
        const OpRef<Rect> body = doc.add(panel, Rect{.width = quarter, .height = length});
        // The top runs from the side to the centre; the side from the hem up.
        const OpRef<AddPoint> neck = doc.add(panel, AddPoint{.at = {body.top(), (quarter - neckWidth) / quarter}});
        const OpRef<AddPoint> underarm = doc.add(panel, AddPoint{.at = {body.right(), (length - armholeDepth) / length}});
        doc.add(panel, AdjustPoint{.corner = body.topRight(), .dx = -3_cm, .dy = -shoulderDrop});  // the shoulder slopes
        doc.add(panel, Curve{.edge = underarm.after(body.right()), .bulge = -2_cm});              // the armhole
        doc.add(panel, AdjustPoint{.corner = body.topLeft(), .dx = 0_cm, .dy = -neckDrop});         // the neckline drops
        doc.add(panel, Bezier{.edge = neck.after(body.top()), .startHandle = {0_cm, -neckDrop * 0.6}, .endHandle = {neckWidth / 2, 0_cm}});
        return Half{panel, body, doc.add(panel, Mirror{.across = body.left()})};
    };
    const Half front = half("front", frontNeckDrop);
    const Half back = half("back", backNeckDrop);
    // The back is turned round in space: the front's drawn half meets the back's mirrored half.
    const auto sew = [&](EdgeRef a, EdgeRef b) { doc.add(Seam{.a = {front.panel, a}, .b = {back.panel, b}}); };
    sew(front.body.right(), back.fold.image(back.body.right()));
    sew(front.fold.image(front.body.right()), back.body.right());
    sew(front.body.top(), back.fold.image(back.body.top()));
    sew(front.fold.image(front.body.top()), back.body.top());
    doc.arrange(front.panel, Arrangement{.placed = true, .x = 0, .y = 108, .z = 16, .wrap = 150});
    doc.arrange(back.panel, Arrangement{.placed = true, .x = 0, .y = 108, .z = -16, .turn = 180, .wrap = 150});
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
        {"6. T-shirt body", tshirtBody},
    };
    return all;
}

void layOutOnTheTable(Document& doc) {
    const std::vector<PanelId> panels = doc.panels();
    if (panels.size() < 2) return;
    for (PanelId panel : panels)
        if (doc.get(panel).x != 0.0 || doc.get(panel).y != 0.0) return;
    const Result laidOut = evaluate(doc);
    double nextX = 0.0;
    for (PanelId panel : panels) {
        const std::optional<Bounds> box = bounds(laidOut[panel]);
        if (!box) continue;
        doc.place(panel, nextX - box->min.x, -box->min.y);
        nextX += box->max.x - box->min.x + 12_cm;
    }
}

} // namespace clothcad::session
