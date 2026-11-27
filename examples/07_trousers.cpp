// Example 7: trousers, a front and a back for each leg.
//
//   1. the right front: a rectangle a quarter of the hips wide, with a point
//      added on the inside edge for the crotch, moved out into the crotch,
//      and one on the side for the hips. The waist and the hem get lengths
//      of their own (Set length), which a rectangle's width cannot say; the
//      side slants in to the waist, rounded over the hips, and the leg narrows
//      to the hem.
//   2. the right back: the same the other way round, with a deeper crotch.
//   3. sewn at the side, above and below the hips, and along the inseam.
//   4. the left leg: a mirrored instance of each, sewn as the right leg is;
//      the centre front and back sewn by hand, each panel to its mirror image.
//   5. waist = 80  ->  the waist widens; the hips, the crotch and the hem
//                      stay, the seams still match
//
// Right and left are the wearer's. A rectangle's right edge runs from the
// hem up, its left edge from the waist down.

#include <cmath>
#include <cstdio>
#include <numbers>

#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "Expect.h"

using namespace twintailor;

namespace {

// What one panel is made of, to name its edges afterwards.
struct Leg {
    PanelId panel;
    OpRef<Rect> body;
    OpRef<AddPoint> crotch, hip;
    EdgeRef upperSide, lowerSide, inseam, rise;  // the side above and below the hip point
};

// How far the outline turns where one arc ends and the next begins, in degrees.
double turnAt(const Pattern& pattern, const EdgeRef& before, const EdgeRef& after) {
    const auto heading = [&](const Edge& edge, std::uint32_t at) {
        const Vec2 point = pattern.vertices[at].position;
        const double x = point.x - edge.arc->center.x, y = point.y - edge.arc->center.y;
        return edge.arc->sweep > 0 ? std::atan2(x, -y) : std::atan2(-x, y);
    };
    const Edge &first = pattern.edge(before), &second = pattern.edge(after);
    const double turn = heading(second, second.start) - heading(first, first.end);
    return std::abs(std::remainder(turn, 2.0 * std::numbers::pi)) * 180.0 / std::numbers::pi;
}

} // namespace

int main() {
    Document doc;
    Param waist       = doc.add(Parameter{.name = "waist", .value = 76_cm});
    Param hips        = doc.add(Parameter{.name = "hips", .value = 94_cm});
    Param ease        = doc.add(Parameter{.name = "ease", .value = 6_cm});
    Param length      = doc.add(Parameter{.name = "length", .value = 92_cm});
    Param rise        = doc.add(Parameter{.name = "rise", .value = 28_cm});
    Param ankle       = doc.add(Parameter{.name = "ankle", .value = 40_cm});
    Param frontCrotch = doc.add(Parameter{.name = "front_crotch", .value = 5_cm});
    Param backCrotch  = doc.add(Parameter{.name = "back_crotch", .value = 12_cm});
    const Expr quarter = (hips + ease) / 4;
    const Expr hipDepth = 18_cm;  // from the waist down to the hips

    // 1. The front: the side on its left, the inside of the leg on its right.
    Leg front;
    front.panel = doc.add(Panel{.name = "right front"});
    front.body = doc.add(front.panel, Rect{.width = quarter, .height = length});
    front.crotch = doc.add(front.panel, AddPoint{.at = {front.body.right(), (length - rise) / length}});
    front.hip = doc.add(front.panel, AddPoint{.at = {front.body.left(), hipDepth / length}});
    doc.add(front.panel, AdjustPoint{.corner = front.crotch.corner(), .dx = frontCrotch, .dy = 0_cm});
    doc.add(front.panel, Curve{.edge = front.crotch.after(front.body.right()), .bulge = -2_cm});
    doc.add(front.panel, SetLength{.edge = front.body.top(), .length = waist / 4, .keeping = SetLength::Keeping::Start});
    doc.add(front.panel, SetLength{.edge = front.body.bottom(), .length = ankle / 2});
    // Rounded over the hips: two arcs that meet at the hip point in one line.
    doc.add(front.panel, Curve{.edge = front.body.left(), .bulge = 1.6_cm});
    doc.add(front.panel, Curve{.edge = front.hip.after(front.body.left()), .bulge = 0.4_cm});
    front.upperSide = front.body.left();
    front.lowerSide = front.hip.after(front.body.left());
    front.inseam = front.body.right();
    front.rise = front.crotch.after(front.body.right());

    // 2. The back: the other way round. Its crotch point is a little lower,
    //    as patterns have it, so the two inseams are about as long.
    Leg back;
    back.panel = doc.add(Panel{.name = "right back"});
    back.body = doc.add(back.panel, Rect{.width = quarter, .height = length});
    back.crotch = doc.add(back.panel, AddPoint{.at = {back.body.left(), rise / length}});
    back.hip = doc.add(back.panel, AddPoint{.at = {back.body.right(), (length - hipDepth) / length}});
    doc.add(back.panel, AdjustPoint{.corner = back.crotch.corner(), .dx = -backCrotch, .dy = -1.2_cm});
    doc.add(back.panel, Curve{.edge = back.body.left(), .bulge = -2_cm});
    doc.add(back.panel, SetLength{.edge = back.body.top(), .length = waist / 4, .keeping = SetLength::Keeping::End});
    doc.add(back.panel, SetLength{.edge = back.body.bottom(), .length = ankle / 2});
    doc.add(back.panel, Curve{.edge = back.hip.after(back.body.right()), .bulge = 1.6_cm});
    doc.add(back.panel, Curve{.edge = back.body.right(), .bulge = 0.4_cm});
    back.upperSide = back.hip.after(back.body.right());
    back.lowerSide = back.body.right();
    back.inseam = back.crotch.after(back.body.left());
    back.rise = back.body.left();

    // 3. The right leg. Each pair runs opposite ways, as a seam expects.
    const auto sew = [&](PanelId a, EdgeRef edgeA, PanelId b, EdgeRef edgeB) { doc.add(Seam{.a = {a, edgeA}, .b = {b, edgeB}}); };
    sew(front.panel, front.upperSide, back.panel, back.upperSide);
    sew(front.panel, front.lowerSide, back.panel, back.lowerSide);
    sew(front.panel, front.inseam, back.panel, back.inseam);

    // 4. The left leg: an instance shows its source's edges under the same
    //    names; a mirrored one runs them the other way round.
    const PanelId leftFront = doc.add(Panel{.name = "left front"});
    doc.add(leftFront, Instance{.source = front.panel, .mirrored = true});
    const PanelId leftBack = doc.add(Panel{.name = "left back"});
    doc.add(leftBack, Instance{.source = back.panel, .mirrored = true});
    const std::size_t copied = doc.sewAsSource(leftFront).size() + doc.sewAsSource(leftBack).size();
    sew(front.panel, front.rise, leftFront, front.rise);
    sew(back.panel, back.rise, leftBack, back.rise);

    Result shapes = evaluate(doc);
    const Pattern* f = &shapes[front.panel];
    const Pattern* b = &shapes[back.panel];

    std::printf("right front and back\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("front waist, a quarter", f->length(front.body.top()), 19_cm);
    expect("back waist, a quarter", b->length(back.body.top()), 19_cm);
    expect("front hem, half the ankle", f->length(front.body.bottom()), 20_cm);
    expect("back hem, half the ankle", b->length(back.body.bottom()), 20_cm);
    expect("front crotch point x", f->position(front.crotch.corner()).x, 30_cm);
    expect("front crotch point height", f->position(front.crotch.corner()).y, 64_cm);
    expect("back crotch point x", b->position(back.crotch.corner()).x, -12_cm);
    expect("back crotch point height", b->position(back.crotch.corner()).y, 62.8_cm);
    expect("hip line on the side", f->position(front.hip.corner()).y, 74_cm);
    expect("side above the hips is rounded", f->length(front.upperSide) > std::hypot(6.0, 18.0) + 0.1, true);
    expect("no corner at the hips, in degrees", turnAt(*f, front.upperSide, front.lowerSide) < 1.5, true);
    expect("sides above the hips match", f->length(front.upperSide), b->length(back.upperSide));
    expect("sides below the hips match", f->length(front.lowerSide), b->length(back.lowerSide));
    expect("inseams within 0.5 mm", std::abs(f->length(front.inseam) - b->length(back.inseam)) < 0.05, true);
    expect("crotch curve is an arc", f->edge(front.rise).arc.has_value(), true);

    std::printf("left leg\n");
    expect("seams copied to it", static_cast<double>(copied), 3);
    expect("seams found", static_cast<double>(shapes.seams.size()), 8);
    expect("left front waist", shapes[leftFront].length(front.body.top()), 19_cm);
    expect("left front's crotch on its left", shapes[leftFront].position(front.crotch.corner()).x < shapes[leftFront].position(front.body.topLeft()).x, true);

    // 5. A wider waist.
    doc.set(waist, 80_cm);
    shapes = evaluate(doc);
    f = &shapes[front.panel];
    b = &shapes[back.panel];

    std::printf("waist 80\n");
    expect("errors", static_cast<double>(shapes.errors.size()), 0);
    expect("front waist widens", f->length(front.body.top()), 20_cm);
    expect("centre front stays", f->position(front.body.topRight()).x, 25_cm);
    expect("hem stays", f->length(front.body.bottom()), 20_cm);
    expect("crotch point stays", f->position(front.crotch.corner()).x, 30_cm);
    expect("sides above the hips still match", f->length(front.upperSide), b->length(back.upperSide));
    expect("left leg follows", shapes[leftFront].length(front.body.top()), 20_cm);

    return failures == 0 ? 0 : 1;
}
