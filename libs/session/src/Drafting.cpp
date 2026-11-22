#include "clothcad/session/Drafting.h"

#include <algorithm>
#include <cmath>

#include "clothcad/core/Evaluate.h"
#include "clothcad/session/Locks.h"
#include "clothcad/sim/Layout.h"

namespace clothcad::session {

namespace {

// One change through the editor; on refusal the outcome says why.
bool apply(Editor& editor, const std::function<void(Document&)>& verb, ChangeRules rules, Outcome& outcome) {
    const std::expected<void, std::string> done = editor.change(verb, rules);
    if (done) return true;
    outcome.notice = "Not changed: " + done.error();
    outcome.refused = true;
    return false;
}

// The rules for each kind of change made on the table.
ChangeRules byHand(bool newStep) { return {.step = newStep ? Step::New : Step::Continue, .grows = Grows::WhereDragged}; }
ChangeRules placing(bool newStep) { return {.step = newStep ? Step::New : Step::Continue, .touches = Touches::PlacementOnly}; }
// Adding and removing things may leave a problem behind: that is shown where it is, not refused.
constexpr ChangeRules adding{.ifItBreaks = IfItBreaks::Allow};

// On a symmetric panel a drag has to respect the mirror: a drag on the
// mirrored side is the mirror image of the drag on the drawn side, and a
// point lying on the mirror's axis may only slide along it.
struct Movement {
    double dx, dy;
};
Movement acrossTheMirror(const Document& doc, const Result& shapes, PanelId panel, Movement moved, bool wasImage, const Vec2* position) {
    const std::optional<OpRef<Mirror>> mirror = symmetryOf(doc, panel);
    if (!mirror) return moved;
    for (const SymmetryAxis& axis : shapes[panel].axes) {
        if (!(axis.mirror == OpId(*mirror))) continue;
        const double length = std::hypot(axis.end.x - axis.start.x, axis.end.y - axis.start.y);
        if (length < 1e-9) continue;
        const double ux = (axis.end.x - axis.start.x) / length, uy = (axis.end.y - axis.start.y) / length;
        const double along = moved.dx * ux + moved.dy * uy;
        if (wasImage) moved = {2.0 * along * ux - moved.dx, 2.0 * along * uy - moved.dy};
        if (position) {
            const double off = (position->x - axis.start.x) * uy - (position->y - axis.start.y) * ux;
            if (std::abs(off) < 1e-6) moved = {along * ux, along * uy};  // on the axis: slide along it
        }
    }
    return moved;
}

bool panelNamed(const Document& doc, const std::string& name) {
    for (PanelId p : doc.panels())
        if (doc.get(p).name == name) return true;
    return false;
}

} // namespace

std::string Drafting::freeName(const Document& doc, const std::string& base) {
    for (int number = 1;; ++number) {
        const std::string name = base + std::to_string(number);
        if (!panelNamed(doc, name)) return name;
    }
}

Outcome Drafting::handle(Editor& editor, const Gesture& gesture) {
    const Outcome outcome = std::visit([&](const auto& g) { return on(editor, g); }, gesture);
    if (outcome.toolDone) tool = Tool::Select;
    return outcome;
}

void Drafting::forget(const Document& doc) {
    dragging_.reset();
    scaling_.reset();
    cornerDrags_.clear();
    turning_.reset();
    arrangedFrom_.reset();
    selection.corners.clear();
    if (selection.panel && !doc.contains(*selection.panel)) selection.panel.reset();
}

Outcome Drafting::on(Editor&, const SelectPanel& g) {
    if (g.panel != selection.panel) selection.corners.clear();
    selection.panel = g.panel;
    return {};
}

Outcome Drafting::on(Editor& editor, const SelectCorners& g) {
    if (!editor.document().contains(g.panel)) return {};
    if (selection.panel != g.panel) selection.corners.clear();
    selection.panel = g.panel;
    std::vector<CornerRef>& chosen = selection.corners;
    const auto has = [&](const CornerRef& c) { return std::find(chosen.begin(), chosen.end(), c) != chosen.end(); };
    if (!g.add) {
        chosen = g.corners;
    } else if (g.corners.size() == 1 && has(g.corners.front())) {
        chosen.erase(std::find(chosen.begin(), chosen.end(), g.corners.front()));
    } else {
        for (const CornerRef& c : g.corners)
            if (!has(c)) chosen.push_back(c);
    }
    return {};
}

Outcome Drafting::on(Editor& editor, const SewEdges& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    if (!doc.contains(g.a.panel) || !doc.contains(g.b.panel)) return outcome;
    for (SeamId id : doc.seams()) {
        const Seam& seam = doc.get(id);
        if ((seam.a == g.a && seam.b == g.b) || (seam.a == g.b && seam.b == g.a)) {
            outcome.notice = "These two edges are already sewn together.";
            return outcome;
        }
    }
    apply(editor, [&](Document& d) { d.add(Seam{.a = g.a, .b = g.b}); }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const RotatePanel& g) {
    Outcome outcome;
    if (!editor.document().contains(g.panel)) return outcome;
    const Panel& panel = editor.document().get(g.panel);
    if (g.phase == DragPhase::Begin) {
        const std::optional<Bounds> own = bounds(editor.shapes()[g.panel]);
        const Vec2 centre = own ? Vec2{(own->min.x + own->max.x) / 2.0, (own->min.y + own->max.y) / 2.0} : Vec2{};
        const Vec2 now = turned(centre, panel.angle);
        turning_ = Turning{centre, {panel.x + now.x, panel.y + now.y}};
    }
    if (!turning_) return outcome;
    const Vec2 shifted = turned(turning_->centre, g.angle);
    const Vec2 pivot = turning_->pivot;
    apply(editor, [&](Document& doc) {
        doc.place(g.panel, pivot.x - shifted.x, pivot.y - shifted.y);
        doc.turn(g.panel, g.angle);
    }, placing(g.phase == DragPhase::Begin), outcome);
    if (g.phase == DragPhase::End) turning_.reset();
    return outcome;
}

Outcome Drafting::on(Editor& editor, const ScalePanel& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    if (!doc.contains(g.panel)) return outcome;
    if (g.phase == DragPhase::Begin) {
        scaling_.reset();
        // The last operation that runs and is on: a mirror applied last waits until the end, so look past it.
        std::optional<OpId> last;
        for (OpId op : doc.operations(g.panel)) {
            const Mirror* mirror = std::get_if<Mirror>(&doc.get(op));
            if (doc.enabled(op) && !(mirror && mirror->appliedLast)) last = op;
        }
        if (!last && doc.operations(g.panel).empty()) return outcome;
        const Scale* scale = last ? std::get_if<Scale>(&doc.get(*last)) : nullptr;
        if (scale && asNumber(scale->x) && asNumber(scale->y)) {
            scaling_ = Scaling{*last, *asNumber(scale->x), *asNumber(scale->y), false};
        } else if (scale) {
            outcome.notice = "This panel's scale is set by a formula. Change it in the Attribute Editor.";
            outcome.refused = true;
            return outcome;
        } else {
            scaling_ = Scaling{std::nullopt, 1.0, 1.0, false};  // its own layer comes with the first accepted change
        }
    }
    if (!scaling_ || (scaling_->layer && (!doc.contains(*scaling_->layer) || !std::holds_alternative<Scale>(doc.get(*scaling_->layer))))) return outcome;
    // A thousandth, so that the numbers in the layer stay readable.
    const auto rounded = [](double v) { return std::max(std::round(v * 1000.0) / 1000.0, 0.001); };
    const double x = rounded(scaling_->baseX * g.x), y = rounded(scaling_->baseY * g.y);
    std::optional<OpId> layer = scaling_->layer;
    if (!layer && x == 1.0 && y == 1.0) {  // nothing to scale yet: no layer for a drag that has not moved
        if (g.phase == DragPhase::End) scaling_.reset();
        return outcome;
    }
    const bool done = apply(editor, [&](Document& d) {
        if (!layer) layer = d.add(g.panel, Scale{}).id;
        const OpRef<Scale> scale(*layer);
        d.set(scale, &Scale::x, Expr(x));
        d.set(scale, &Scale::y, Expr(y));
    }, byHand(!scaling_->stepOpen), outcome);
    if (done) {
        scaling_->layer = layer;
        scaling_->stepOpen = true;
    }
    if (g.phase == DragPhase::End) scaling_.reset();
    return outcome;
}

Outcome Drafting::on(Editor& editor, const MoveInSpace& g) {
    return arrangeByDrag(editor, g.panel, g.phase, [&](Arrangement now) {
        now.x += g.by.x;
        now.y += g.by.y;
        now.z += g.by.z;
        return now;
    });
}

Outcome Drafting::on(Editor& editor, const TurnInSpace& g) {
    return arrangeByDrag(editor, g.panel, g.phase, [&](const Arrangement& was) { return sim::turnedAbout(was, g.axis, g.degrees); });
}

Outcome Drafting::arrangeByDrag(Editor& editor, PanelId panel, DragPhase phase, const std::function<Arrangement(const Arrangement&)>& now) {
    // Carrying or turning a panel in space only places it: its shape stays,
    // and a draped panel takes its drape along. One drag is one undo step.
    Outcome outcome;
    const Document& doc = editor.document();
    if (!doc.contains(panel)) return outcome;
    if (phase == DragPhase::Begin) {
        // The editor places a panel once it has an outline; one that has none yet has no place to start from.
        const Panel& placed = doc.get(panel);
        arrangedFrom_.reset();
        if (placed.arrangement.placed) arrangedFrom_ = placed.arrangement;
        else if (const Arrangement first = sim::firstPlace(placed, editor.shapes()[panel]); first.placed) arrangedFrom_ = first;
    }
    if (!arrangedFrom_) return outcome;
    Arrangement arranged = now(*arrangedFrom_);
    arranged.placed = true;
    apply(editor, [&](Document& d) { d.arrange(panel, arranged); }, placing(phase == DragPhase::Begin), outcome);
    if (phase == DragPhase::End) arrangedFrom_.reset();
    return outcome;
}

Outcome Drafting::on(Editor& editor, const MovePanel& g) {
    Outcome outcome;
    if (!editor.document().contains(g.panel)) return outcome;
    apply(editor, [&](Document& doc) { doc.place(g.panel, g.x, g.y); }, placing(g.phase == DragPhase::Begin), outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const MoveCorner& image) {
    Outcome outcome;
    const Document& doc = editor.document();
    bool newStep = false;  // whether this change opens the drag's undo step
    if (!doc.contains(image.panel)) return outcome;
    const Vec2 own = turned(Vec2{image.dx, image.dy}, -doc.get(image.panel).angle);  // the drag in the panel's own coordinates
    const Pattern& shape = editor.shapes()[image.panel];
    std::vector<CornerRef>& chosen = selection.corners;
    const auto isSelected = [&](const CornerRef& c) { return std::find(chosen.begin(), chosen.end(), c) != chosen.end(); };

    // Dragging a selected corner moves the whole selection; dragging any
    // other corner selects just that one.
    if (image.phase == DragPhase::Begin && !(chosen.size() > 1 && isSelected(image.corner))) chosen = {image.corner};
    const std::vector<CornerRef> grabbed = chosen.size() > 1 && isSelected(image.corner) ? chosen : std::vector<CornerRef>{image.corner};

    // On a symmetric panel, a mirrored corner moves its original, the mirrored
    // way; a corner and its own image count once.
    struct Moving {
        CornerRef corner;
        Movement by;
    };
    std::vector<Moving> moving;
    for (const CornerRef& c : grabbed) {
        if (!shape.contains(c)) continue;
        const DrawnCorner drawn = drawnSide(doc, image.panel, c);
        if (std::any_of(moving.begin(), moving.end(), [&](const Moving& m) { return m.corner == drawn.corner; })) continue;
        const Vec2 where = shape.position(c);
        moving.push_back({drawn.corner, acrossTheMirror(doc, editor.shapes(), image.panel, {own.x, own.y}, drawn.wasImage, &where)});
    }

    if (image.phase == DragPhase::Begin) {
        dragging_.reset();
        cornerDrags_.clear();
        // Corners that come from a formula stay where they are.
        std::string locked;
        moving.erase(std::remove_if(moving.begin(), moving.end(), [&](const Moving& m) {
                         const std::string why = cornerLock(doc, image.panel, m.corner);
                         if (!why.empty()) locked = why;
                         return !why.empty();
                     }),
                     moving.end());
        if (moving.empty()) {
            outcome.notice = locked;
            return outcome;
        }
        if (moving.size() == 1) {
            // Keep dragging the same corner in one operation: if the panel's last
            // operation already moves this corner by plain numbers, continue it.
            const CornerRef corner = moving.front().corner;
            const std::vector<OpId> operations = doc.operations(image.panel);
            const AdjustPoint* last = operations.empty() ? nullptr : std::get_if<AdjustPoint>(&doc.get(operations.back()));
            if (last && last->corner == corner && asNumber(last->dx) && asNumber(last->dy)) {
                newStep = true;  // the drag's first change opens its undo step
                dragging_ = Dragging{operations.back(), false, *asNumber(last->dx), *asNumber(last->dy)};
            } else {
                OpId added;
                if (!apply(editor, [&](Document& d) { added = d.add(image.panel, AdjustPoint{.corner = corner, .dx = 0_cm, .dy = 0_cm}); }, byHand(true), outcome))
                    return outcome;  // for instance the only point of a whole circle
                dragging_ = Dragging{added, false, 0.0, 0.0};
            }
        } else {
            // A group: one adjustment each, all in one undo step.
            std::vector<CornerDrag> started;
            if (!apply(editor, [&](Document& d) {
                    for (const Moving& m : moving) started.push_back({m.corner, d.add(image.panel, AdjustPoint{.corner = m.corner, .dx = 0_cm, .dy = 0_cm}).id});
                }, byHand(true), outcome))
                return outcome;
            cornerDrags_ = std::move(started);
        }
    }

    if (!cornerDrags_.empty()) {
        apply(editor, [&](Document& d) {
            for (const CornerDrag& drag : cornerDrags_) {
                const auto it = std::find_if(moving.begin(), moving.end(), [&](const Moving& m) { return m.corner == drag.corner; });
                if (it == moving.end() || !d.contains(drag.operation)) continue;
                const OpRef<AdjustPoint> adjust(drag.operation);
                d.set(adjust, &AdjustPoint::dx, Expr(it->by.dx));
                d.set(adjust, &AdjustPoint::dy, Expr(it->by.dy));
            }
        }, byHand(false), outcome);
        if (image.phase == DragPhase::End) cornerDrags_.clear();
        return outcome;
    }
    if (!dragging_ || !doc.contains(dragging_->operation) || moving.empty()) return outcome;
    const OpRef<AdjustPoint> adjust(dragging_->operation);
    const double x = dragging_->baseX + moving.front().by.dx, y = dragging_->baseY + moving.front().by.dy;
    apply(editor, [&](Document& d) {
        d.set(adjust, &AdjustPoint::dx, Expr(x));
        d.set(adjust, &AdjustPoint::dy, Expr(y));
    }, byHand(newStep), outcome);
    if (image.phase == DragPhase::End) dragging_.reset();
    return outcome;
}

Outcome Drafting::on(Editor& editor, const MoveHandle& image) {
    Outcome outcome;
    const Document& doc = editor.document();
    if (!doc.contains(image.panel)) return outcome;
    // On a symmetric panel, a handle on the mirrored side is the opposite
    // handle of the original edge, moved the mirrored way.
    const DrawnEdge drawn = drawnSide(doc, image.panel, image.edge);
    const Vec2 own = turned(Vec2{image.dx, image.dy}, -doc.get(image.panel).angle);
    const Movement moved = acrossTheMirror(doc, editor.shapes(), image.panel, {own.x, own.y}, drawn.wasImage, nullptr);
    const bool endHandle = drawn.wasImage ? !image.endHandle : image.endHandle;

    if (image.phase == DragPhase::Begin) {
        dragging_.reset();
        if (const std::string why = handleLock(doc, image.panel, drawn.edge, endHandle); !why.empty()) {
            outcome.notice = why;
            return outcome;
        }
        const OpRef<Bezier> curve = *curveOf(doc, image.panel, drawn.edge);
        const Offset& handle = endHandle ? doc.get(curve).endHandle : doc.get(curve).startHandle;
        dragging_ = Dragging{curve, endHandle, *asNumber(handle.x), *asNumber(handle.y)};
    }
    if (!dragging_ || !doc.contains(dragging_->operation)) return outcome;
    const OpRef<Bezier> curve(dragging_->operation);
    // A Scale after the curve stretches the handle's offset, so the offset moves by the pointer's drag divided by it.
    double stretchX = 1.0, stretchY = 1.0;
    bool later = false;
    for (OpId op : doc.operations(image.panel)) {
        if (later && doc.enabled(op))
            if (const Scale* scale = std::get_if<Scale>(&doc.get(op)); scale && asNumber(scale->x) && asNumber(scale->y)) {
                stretchX *= *asNumber(scale->x);
                stretchY *= *asNumber(scale->y);
            }
        if (op == dragging_->operation) later = true;
    }
    if (stretchX < 1e-9) stretchX = 1.0;
    if (stretchY < 1e-9) stretchY = 1.0;
    const Offset handle{dragging_->baseX + moved.dx / stretchX, dragging_->baseY + moved.dy / stretchY};
    const bool whichEnd = dragging_->endHandle;
    apply(editor, [&](Document& d) { d.set(curve, whichEnd ? &Bezier::endHandle : &Bezier::startHandle, handle); }, byHand(image.phase == DragPhase::Begin), outcome);
    if (image.phase == DragPhase::End) dragging_.reset();
    return outcome;
}

Outcome Drafting::on(Editor& editor, const DrawRect& g) {
    Outcome outcome{.toolDone = true};
    apply(editor, [&](Document& doc) {
        const PanelId panel = doc.add(Panel{.name = freeName(doc, "panel"), .x = g.x, .y = g.y});
        doc.add(panel, Rect{.width = Expr(g.width), .height = Expr(g.height)});
        selection = {panel, {}};
    }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const DrawCircle& g) {
    Outcome outcome{.toolDone = true};
    apply(editor, [&](Document& doc) {
        const PanelId panel = doc.add(Panel{.name = freeName(doc, "circle"), .x = g.x, .y = g.y});
        doc.add(panel, Circle{.radius = Expr(g.radius)});
        selection = {panel, {}};
    }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const PlaceRing& g) {
    Outcome outcome{.toolDone = true};
    apply(editor, [&](Document& doc) {
        const PanelId panel = doc.add(Panel{.name = freeName(doc, "skirt"), .x = g.x, .y = g.y});
        doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
        selection = {panel, {}};
    }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const BendEdge& image) {
    Outcome outcome{.toolDone = true};  // so the handles can be dragged straight away
    const Document& doc = editor.document();
    // On a symmetric panel, bending a mirrored edge bends its original: both follow.
    const BendEdge g{image.panel, drawnSide(doc, image.panel, image.edge).edge};
    selection.panel = g.panel;
    if (curveOf(doc, g.panel, g.edge)) return outcome;  // already a free curve: its handles are showing now
    const Pattern& shape = editor.shapes()[g.panel];
    if (!shape.contains(g.edge)) return outcome;
    // Handles a third of the way along the edge: it starts out straight.
    const Edge& edge = shape.edge(g.edge);
    const Vec2 start = shape.vertices[edge.start].position, end = shape.vertices[edge.end].position;
    double x = std::round((end.x - start.x) / 3.0 * 10.0) / 10.0, y = std::round((end.y - start.y) / 3.0 * 10.0) / 10.0;
    double endX = -x, endY = -y;
    if (edge.arc) {
        // An arc becomes a free curve that follows it, so bending it starts from what is drawn.
        const std::optional<CubicCurve> follow = cubicFromArc(shape, edge);
        if (!follow) {
            outcome.notice = "A whole circle has no ends to bend between. Make it a slice, or cut it first.";
            return outcome;
        }
        const auto tenth = [](double v) { return std::round(v * 10.0) / 10.0; };
        x = tenth(follow->control1.x - start.x), y = tenth(follow->control1.y - start.y);
        endX = tenth(follow->control2.x - end.x), endY = tenth(follow->control2.y - end.y);
    }
    apply(editor, [&](Document& d) { d.add(g.panel, Bezier{.edge = g.edge, .startHandle = {Expr(x), Expr(y)}, .endHandle = {Expr(endX), Expr(endY)}}); }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const AddPointOn& g) {
    Outcome outcome{.toolDone = true};  // to drag the new corner straight away
    const Document& doc = editor.document();
    selection.panel = g.panel;
    // On a symmetric panel the point goes on the half that was drawn, and
    // the mirror makes its image: a click on the mirrored side is moved across.
    const DrawnEdge drawn = drawnSide(doc, g.panel, g.at.edge);
    const double along = drawn.wasImage ? 1.0 - g.at.along : g.at.along;
    const std::vector<SeamId> seams = doc.seams();
    const bool sewn = std::any_of(seams.begin(), seams.end(), [&](SeamId id) {
        const Seam& seam = doc.get(id);
        return (seam.a.panel == g.panel && seam.a.edge == drawn.edge) || (seam.b.panel == g.panel && seam.b.edge == drawn.edge);
    });
    if (apply(editor, [&](Document& d) { d.add(g.panel, AddPoint{.at = {drawn.edge, Expr(along)}}); }, adding, outcome) && sewn)
        outcome.notice = "This edge was sewn: the seam now sews only the part before the new point. Sew the part after it too, if it should be sewn.";
    return outcome;
}

Outcome Drafting::on(Editor& editor, const DrawLine& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    selection.panel = g.panel;
    // On a symmetric panel the line is drawn on the half that was drawn. An
    // end placed on the mirrored side is moved to the same place on the original edge.
    const DrawnEdge from = drawnSide(doc, g.panel, g.from.edge), to = drawnSide(doc, g.panel, g.to.edge);
    if (from.wasImage != to.wasImage) {
        outcome.notice = "A line can't cross the mirror of a symmetric panel. Draw it on one half, or switch off \"Applied last\" on the mirror.";
        outcome.refused = true;
        return outcome;
    }
    const double fromAlong = from.wasImage ? 1.0 - g.from.along : g.from.along;
    const double toAlong = to.wasImage ? 1.0 - g.to.along : g.to.along;
    apply(editor, [&](Document& d) { d.add(g.panel, LineBetween{.from = {from.edge, Expr(fromAlong)}, .to = {to.edge, Expr(toAlong)}}); }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const EditLength& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    selection.panel = g.panel;
    const Pattern& shape = editor.shapes()[g.panel];
    const EdgeRef edge = g.edge;
    if (doc.contains(edge.op) && shape.contains(edge)) {
        const OpData& data = doc.get(edge.op);
        for (const Dimension& dimension : dimensions(data, edge.op)) {
            if (dimension.edge.role != edge.role) continue;  // a mirrored copy shares the role
            const Field field = fields(data)[dimension.field.index];
            const std::optional<double> value = valueOf(field.value, editor.shapes());
            if (!value || std::abs(*value - shape.length(edge)) > 1e-6) continue;  // a later step changed the edge
            outcome.typeLength = Outcome::LengthField{edge.op, dimension.field, std::string(field.label) + " of " + nameOf(Reference(edge), doc)};
            return outcome;
        }
    }
    outcome.notice = "This edge's length is not a setting of its own; it follows from where its corners are. Drag its corners instead.";
    return outcome;
}

Outcome Drafting::on(Editor& editor, const CutAway& g) {
    Outcome outcome;
    selection.panel = g.panel;
    apply(editor, [&](Document& d) { d.add(g.panel, Cut{.along = g.line, .keep = g.keep}); }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const SplitAt& g) {
    Outcome outcome{.toolDone = true};
    selection.panel = g.panel;
    apply(editor, [&](Document& d) {
        // One panel keeps one side; a new panel is what the cut removes.
        const OpRef<Cut> cut = d.add(g.panel, Cut{.along = g.line, .keep = Cut::Side::Left});
        const Panel& source = d.get(g.panel);
        const PanelId other = d.add(Panel{.name = freeName(d, source.name + " part"), .x = source.x, .y = source.y});
        d.add(other, Offcut{.of = cut});
    }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const MirrorAcross& g) {
    Outcome outcome{.toolDone = true};
    selection.panel = g.panel;
    apply(editor, [&](Document& d) { d.add(g.panel, Mirror{.across = g.edge}); }, adding, outcome);
    return outcome;
}

} // namespace clothcad::session
