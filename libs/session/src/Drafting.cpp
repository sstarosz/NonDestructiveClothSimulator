#include "twintailor/session/Drafting.h"

#include <algorithm>
#include <cmath>

#include "twintailor/core/Evaluate.h"
#include "twintailor/session/Lengths.h"
#include "twintailor/session/Locks.h"
#include "twintailor/sim/Layout.h"
#include "Corners.h"

namespace twintailor::session {

namespace {

// Whether a change was taken; on refusal the outcome says why.
bool taken(const std::expected<void, std::string>& done, Outcome& outcome) {
    if (done) return true;
    outcome.notice = "Not changed: " + done.error();
    outcome.refused = true;
    return false;
}

// One change through the editor, by itself or in an edit.
bool apply(Editor& editor, const std::function<void(Document&)>& verb, ChangeRules rules, Outcome& outcome) {
    return taken(editor.change(verb, rules), outcome);
}
bool apply(Editor& editor, Edit edit, const std::function<void(Document&)>& verb, ChangeRules rules, Outcome& outcome) {
    return taken(editor.change(edit, verb, rules), outcome);
}

// The rules for each kind of change made on the table.
constexpr ChangeRules byHand{.grows = Grows::WhereDragged};
constexpr ChangeRules placing{.touches = Touches::PlacementOnly};
// Adding and removing things may leave a problem behind: that is shown where it is, not refused.
constexpr ChangeRules adding{.ifItBreaks = IfItBreaks::Allow};

// "1 seam", "2 seams".
std::string seamCount(std::size_t count) { return std::to_string(count) + (count == 1 ? " seam" : " seams"); }

// The selected panels in the document's order; only those that are still there.
std::vector<PanelId> selectedInOrder(const Document& doc, const Drafting::Selection& selection) {
    std::vector<PanelId> panels;
    for (PanelId panel : doc.panels())
        if (selection.has(panel)) panels.push_back(panel);
    return panels;
}

} // namespace

Along axisOf(double dx, double dy) { return std::abs(dx) >= std::abs(dy) ? Along::X : Along::Y; }

bool sewsReversed(double aAlong, double bAlong) { return (aAlong < 0.5) == (bAlong < 0.5); }

std::string Drafting::freeName(const Document& doc, const std::string& base) {
    for (int number = 1;; ++number) {
        const std::string name = base + std::to_string(number);
        if (!panelNamed(doc, name)) return name;
    }
}

Outcome Drafting::handle(Editor& editor, const Gesture& gesture) {
    const Outcome outcome = std::visit(
        [&](const auto& g) {
            if constexpr (requires { g.phase; }) {
                // Each drag is an edit of its own, from its beginning to its end.
                if (g.phase == DragPhase::Begin) dragged_.reset();
                const Outcome done = on(editor, g);
                if (g.phase == DragPhase::End && dragged_) editor.commit(*dragged_);
                return done;
            } else {
                return on(editor, g);
            }
        },
        gesture);
    if (outcome.toolDone) tool = Tool::Select;
    if (selection.panel) selection.seam.reset();  // whatever selected a panel lets go of the seam
    return outcome;
}

Edit Drafting::dragIn(Editor& editor) {
    if (!dragged_ || !editor.editing(*dragged_)) dragged_ = editor.begin();
    return *dragged_;
}

Edit Drafting::typingIn(Editor& editor) {
    if (!typed_ || !editor.editing(*typed_)) typed_ = editor.begin();
    return *typed_;
}

void Drafting::commitTyping(Editor& editor) {
    if (typed_) editor.commit(*typed_);
    typed_.reset();
}

void Drafting::cancelTyping(Editor& editor) {
    if (typed_) editor.cancel(*typed_);
    typed_.reset();
}

std::optional<Vec2> middleOfPanels(const Document& doc, const Result& shapes, const std::vector<PanelId>& panels) {
    std::optional<Bounds> all;
    for (PanelId panel : panels) {
        if (!doc.contains(panel)) continue;
        const Panel& placed = doc.get(panel);
        const std::optional<Bounds> box = bounds(turned(shapes[panel], placed.angle));
        if (!box) continue;
        const Bounds here{{box->min.x + placed.x, box->min.y + placed.y}, {box->max.x + placed.x, box->max.y + placed.y}};
        if (!all) all = here;
        else all = Bounds{{std::min(all->min.x, here.min.x), std::min(all->min.y, here.min.y)}, {std::max(all->max.x, here.max.x), std::max(all->max.y, here.max.y)}};
    }
    if (!all) return std::nullopt;
    return Vec2{(all->min.x + all->max.x) / 2.0, (all->min.y + all->max.y) / 2.0};
}

bool Drafting::beginGroup(const Document& doc, PanelId panel) {
    groupStarts_.clear();
    if (!selection.has(panel)) return false;
    const std::vector<PanelId> group = selectedInOrder(doc, selection);
    if (group.size() < 2) return false;
    for (PanelId member : group) {
        const Panel& placed = doc.get(member);
        groupStarts_.push_back({member, placed.x, placed.y, placed.angle});
    }
    return true;
}

std::vector<PanelId> Drafting::Selection::panels() const {
    std::vector<PanelId> all;
    if (panel) all.push_back(*panel);
    for (PanelId other : others)
        if (std::find(all.begin(), all.end(), other) == all.end()) all.push_back(other);
    return all;
}

bool Drafting::Selection::has(PanelId candidate) const {
    return panel == candidate || std::find(others.begin(), others.end(), candidate) != others.end();
}

void Drafting::join(PanelId panel) {
    if (selection.panel == panel) return;
    std::erase(selection.others, panel);
    if (selection.panel) selection.others.push_back(*selection.panel);
    selection.corners.clear();
    selection.panel = panel;
}

void Drafting::drop(PanelId panel) {
    std::erase(selection.others, panel);
    if (selection.panel != panel) return;
    selection.corners.clear();
    selection.panel.reset();
    if (!selection.others.empty()) {
        selection.panel = selection.others.back();
        selection.others.pop_back();
    }
}

void Drafting::forget(const Document& doc) {
    dragging_.reset();
    scaling_.reset();
    groupStarts_.clear();
    draggingCorners_ = false;
    turning_.reset();
    arrangedFrom_.reset();
    selection.corners.clear();
    for (PanelId gone : selection.panels())
        if (!doc.contains(gone)) drop(gone);
    if (selection.seam && !doc.contains(*selection.seam)) selection.seam.reset();
}

Outcome Drafting::on(Editor& editor, const SelectPanel& g) {
    if (g.how == Pick::Replace) {
        if (g.panel != selection.panel) selection.corners.clear();
        selection.panel = g.panel;
        selection.others.clear();
        selection.seam.reset();
    } else if (g.panel && editor.document().contains(*g.panel)) {
        if (g.how == Pick::Toggle && selection.has(*g.panel)) drop(*g.panel);
        else join(*g.panel);
    }
    return {};
}

Outcome Drafting::on(Editor& editor, const SelectPanels& g) {
    const Document& doc = editor.document();
    if (g.how == Pick::Replace) {
        selection.panel.reset();
        selection.others.clear();
        selection.corners.clear();
        selection.seam.reset();
    }
    for (PanelId panel : g.panels) {
        if (!doc.contains(panel)) continue;
        if (g.how == Pick::Toggle && selection.has(panel)) drop(panel);
        else join(panel);
    }
    return {};
}

Outcome Drafting::on(Editor& editor, const SelectSeam& g) {
    if (!g.seam) selection.seam.reset();
    else if (editor.document().contains(*g.seam)) selection = {.panel = {}, .corners = {}, .others = {}, .seam = g.seam};
    return {};
}

Outcome Drafting::on(Editor& editor, const SelectCorners& g) {
    if (!editor.document().contains(g.panel)) return {};
    if (selection.panel != g.panel) selection.corners.clear();
    selection.panel = g.panel;
    std::vector<CornerRef>& chosen = selection.corners;
    const auto has = [&](const CornerRef& c) { return std::find(chosen.begin(), chosen.end(), c) != chosen.end(); };
    if (g.how == Pick::Replace) {
        chosen = g.corners;
        return {};
    }
    for (const CornerRef& c : g.corners) {
        if (!has(c)) chosen.push_back(c);
        else if (g.how == Pick::Toggle) chosen.erase(std::find(chosen.begin(), chosen.end(), c));
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
    apply(editor, [&](Document& d) { d.add(Seam{.a = g.a, .b = g.b, .reversed = sewsReversed(g.aAlong, g.bAlong)}); }, adding, outcome);
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
    if (g.phase == DragPhase::Begin && beginGroup(editor.document(), g.panel))
        groupPivot_ = middleOfPanels(editor.document(), editor.shapes(), selectedInOrder(editor.document(), selection)).value_or(Vec2{});
    if (!groupStarts_.empty()) {
        // Several panels turn together about the middle of the group.
        const auto grabbed = std::find_if(groupStarts_.begin(), groupStarts_.end(), [&](const GroupStart& s) { return s.panel == g.panel; });
        if (grabbed == groupStarts_.end()) {
            groupStarts_.clear();
            return outcome;
        }
        const double by = g.angle - grabbed->angle;
        const Vec2 pivot = groupPivot_;
        apply(editor, dragIn(editor), [&](Document& doc) {
            for (const GroupStart& s : groupStarts_) {
                if (!doc.contains(s.panel)) continue;
                const Vec2 offset = turned(Vec2{s.x - pivot.x, s.y - pivot.y}, by);
                doc.place(s.panel, pivot.x + offset.x, pivot.y + offset.y);
                doc.turn(s.panel, s.angle + by);
            }
        }, placing, outcome);
        if (g.phase == DragPhase::End) groupStarts_.clear();
        return outcome;
    }
    if (!turning_) return outcome;
    const Vec2 shifted = turned(turning_->centre, g.angle);
    const Vec2 pivot = turning_->pivot;
    apply(editor, dragIn(editor), [&](Document& doc) {
        doc.place(g.panel, pivot.x - shifted.x, pivot.y - shifted.y);
        doc.turn(g.panel, g.angle);
    }, placing, outcome);
    if (g.phase == DragPhase::End) turning_.reset();
    return outcome;
}

std::expected<Drafting::ScalePart, std::string> Drafting::scalePartOf(const Editor& editor, PanelId panel) const {
    const Document& doc = editor.document();
    const Panel& placed = doc.get(panel);
    ScalePart part{.panel = panel, .anchor = placed.arrangement.anchor};
    if (const std::optional<Bounds> box = bounds(editor.shapes()[panel])) {
        part.ownMiddle = {(box->min.x + box->max.x) / 2.0, (box->min.y + box->max.y) / 2.0};
        const Vec2 own = turned(part.ownMiddle, placed.angle);
        part.middle = {placed.x + own.x, placed.y + own.y};
    }
    const std::string name = "the panel " + placed.name;
    const std::vector<OpId> layers = doc.operations(panel);
    // The last operation that runs and is on: a mirror applied last waits until the end, so look past it.
    std::optional<OpId> last;
    for (OpId op : layers) {
        const Mirror* mirror = std::get_if<Mirror>(&doc.get(op));
        if (doc.enabled(op) && !(mirror && mirror->appliedLast)) last = op;
    }
    if (last) {
        if (const Scale* scale = std::get_if<Scale>(&doc.get(*last))) {
            if (!asNumber(scale->x) || !asNumber(scale->y))
                return std::unexpected("The scale of " + name + " is set by a formula. Change it in the Attribute Editor.");
            part.layer = *last;
            part.baseA = *asNumber(scale->x);
            part.baseB = *asNumber(scale->y);
            return part;
        }
    }
    // No Scale layer to change: scale the starting shape's own lengths, so nothing is added.
    // Not when an edge has a length of its own (SetLength): that would stay as it is.
    const bool setsLengths = std::any_of(layers.begin(), layers.end(), [&](OpId op) { return doc.enabled(op) && std::holds_alternative<SetLength>(doc.get(op)); });
    const OpId first = layers.front();
    if (doc.enabled(first) && !setsLengths) {
        const OpData& data = doc.get(first);
        // A length that a formula sets is not overwritten: the formula keeps driving the shape, and a Scale layer goes on top.
        const auto lengths = [&](const Expr& a, const Expr& b) {
            if (!asNumber(a) || !asNumber(b)) return;
            part.shape = first;
            part.baseA = *asNumber(a);
            part.baseB = *asNumber(b);
        };
        if (const Rect* rect = std::get_if<Rect>(&data)) lengths(rect->width, rect->height);
        else if (const Circle* circle = std::get_if<Circle>(&data)) lengths(circle->radius, circle->radius);
        else if (const RingSector* ring = std::get_if<RingSector>(&data)) lengths(ring->innerLength, ring->depth);
    }
    return part;  // else its own Scale layer comes with the first accepted change
}

Outcome Drafting::on(Editor& editor, const ScalePanel& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    if (!doc.contains(g.panel)) return outcome;
    if (g.phase == DragPhase::Begin) {
        scaling_.reset();
        std::vector<PanelId> group = selection.has(g.panel) ? selectedInOrder(doc, selection) : std::vector<PanelId>{g.panel};
        if (group.empty()) group = {g.panel};
        Scaling next{.several = group.size() > 1};
        for (PanelId member : group) {
            if (doc.operations(member).empty()) continue;  // nothing to scale
            std::expected<ScalePart, std::string> part = scalePartOf(editor, member);
            if (!part) {
                outcome.notice = part.error();
                outcome.refused = true;
                return outcome;
            }
            next.parts.push_back(*part);
        }
        if (next.parts.empty()) return outcome;
        next.pivot = middleOfPanels(doc, editor.shapes(), group).value_or(Vec2{});
        scaling_ = std::move(next);
    }
    if (!scaling_) return outcome;
    for (const ScalePart& part : scaling_->parts) {
        const bool gone = !doc.contains(part.panel) ||
                          (part.layer && (!doc.contains(*part.layer) || !std::holds_alternative<Scale>(doc.get(*part.layer)))) ||
                          (part.shape && !doc.contains(*part.shape));
        if (gone) {
            scaling_.reset();
            return outcome;
        }
    }
    const bool evenly = std::abs(g.x - g.y) < 1e-9;
    if (scaling_->several && !evenly) {
        outcome.notice = "Several panels are scaled evenly only. Scale them the same in x and y, or click one panel to stretch it one way.";
        outcome.refused = true;
        return outcome;
    }
    // A circle's radius and a ring's radius are one length: there is no stretching them one way.
    for (const ScalePart& part : scaling_->parts) {
        if (!part.shape || evenly || std::holds_alternative<Rect>(doc.get(*part.shape))) continue;
        outcome.notice = "A circle or a ring is scaled evenly only. Scale it the same in x and y, or change its lengths in the Attribute Editor.";
        outcome.refused = true;
        return outcome;
    }
    // A thousandth, so that the numbers stay readable. A drag that is back at 1 gives the exact old number.
    const auto times = [](double base, double factor) { return factor == 1.0 ? base : std::max(std::round(base * factor * 1000.0) / 1000.0, 0.001); };
    const bool unchanged = g.x == 1.0 && g.y == 1.0;
    if (!scaling_->changed && unchanged) {  // a drag that has not moved changes nothing yet
        if (g.phase == DragPhase::End) scaling_.reset();
        return outcome;
    }
    Scaling next = *scaling_;
    const Edit drag = dragIn(editor);
    const bool done = apply(editor, drag, [&](Document& d) {
        for (ScalePart& part : next.parts) {
            if (part.shape) {
                const OpData& data = d.get(*part.shape);
                if (std::holds_alternative<Rect>(data)) {
                    const OpRef<Rect> rect(*part.shape);
                    d.set(rect, &Rect::width, Expr(times(part.baseA, g.x)));
                    d.set(rect, &Rect::height, Expr(times(part.baseB, g.y)));
                } else if (std::holds_alternative<Circle>(data)) {
                    d.set(OpRef<Circle>(*part.shape), &Circle::radius, Expr(times(part.baseA, g.x)));
                } else {
                    const OpRef<RingSector> ring(*part.shape);
                    d.set(ring, &RingSector::innerLength, Expr(times(part.baseA, g.x)));
                    d.set(ring, &RingSector::depth, Expr(times(part.baseB, g.x)));
                }
            } else {
                if (!part.layer) part.layer = d.add(part.panel, Scale{}).id;
                const OpRef<Scale> scale(*part.layer);
                d.set(scale, &Scale::x, Expr(times(part.baseA, g.x)));
                d.set(scale, &Scale::y, Expr(times(part.baseB, g.y)));
            }
        }
    }, byHand, outcome);
    if (done) {
        scaling_ = std::move(next);
        scaling_->changed = true;
        // A shape grows about its origin and a layer about its box's middle; what the panel's box now is tells where
        // the place has to be so that its middle goes where the scaling says: the group's middle stays, and each
        // panel's middle moves away from it by as much (a single panel's own middle stays). In space each panel's
        // middle stays: its anchor moves in the panel's own coordinates as far as that middle did.
        std::vector<std::pair<PanelId, Vec2>> places;
        std::vector<std::pair<PanelId, Arrangement>> anchors;
        for (const ScalePart& part : scaling_->parts) {
            const std::optional<Bounds> box = bounds(editor.shapes()[part.panel]);
            if (!box) continue;
            const Panel& placed = editor.document().get(part.panel);
            const Vec2 middle{(box->min.x + box->max.x) / 2.0, (box->min.y + box->max.y) / 2.0};
            const Vec2 own = turned(middle, placed.angle);
            const Vec2 want{scaling_->pivot.x + g.x * (part.middle.x - scaling_->pivot.x), scaling_->pivot.y + g.x * (part.middle.y - scaling_->pivot.y)};
            const Vec2 at{want.x - own.x, want.y - own.y};
            if (std::abs(at.x - placed.x) > 1e-9 || std::abs(at.y - placed.y) > 1e-9) places.push_back({part.panel, at});
            // With no anchor said, the middle of the box is the anchor and follows by itself.
            if (!part.anchor) continue;
            Arrangement arranged = placed.arrangement;
            arranged.anchor = Vec2{part.anchor->x + (middle.x - part.ownMiddle.x), part.anchor->y + (middle.y - part.ownMiddle.y)};
            if (!(arranged == placed.arrangement)) anchors.push_back({part.panel, arranged});
        }
        if (!places.empty() || !anchors.empty())
            apply(editor, drag, [&](Document& d) {
                for (const auto& [panel, at] : places) d.place(panel, at.x, at.y);
                for (const auto& [panel, arranged] : anchors) d.arrange(panel, arranged);
            }, placing, outcome);
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
    return arrangeByDrag(editor, g.panel, g.phase, [&](const Arrangement& was) {
        return g.about ? sim::turnedAbout(was, g.axis, g.degrees, *g.about) : sim::turnedAbout(was, g.axis, g.degrees);
    });
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
    apply(editor, dragIn(editor), [&](Document& d) { d.arrange(panel, arranged); }, placing, outcome);
    if (phase == DragPhase::End) arrangedFrom_.reset();
    return outcome;
}

Outcome Drafting::on(Editor& editor, const MovePanel& g) {
    Outcome outcome;
    if (!editor.document().contains(g.panel)) return outcome;
    if (g.phase == DragPhase::Begin) beginGroup(editor.document(), g.panel);
    if (!groupStarts_.empty()) {
        // Several panels move together, each by as much as the one taken hold of.
        const auto grabbed = std::find_if(groupStarts_.begin(), groupStarts_.end(), [&](const GroupStart& s) { return s.panel == g.panel; });
        if (grabbed == groupStarts_.end()) {
            groupStarts_.clear();
            return outcome;
        }
        const double dx = g.x - grabbed->x, dy = g.y - grabbed->y;
        apply(editor, dragIn(editor), [&](Document& doc) {
            for (const GroupStart& s : groupStarts_)
                if (doc.contains(s.panel)) doc.place(s.panel, s.x + dx, s.y + dy);
        }, placing, outcome);
        if (g.phase == DragPhase::End) groupStarts_.clear();
        return outcome;
    }
    apply(editor, dragIn(editor), [&](Document& doc) { doc.place(g.panel, g.x, g.y); }, placing, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const MoveCorner& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    if (!doc.contains(g.panel)) return outcome;
    std::vector<CornerRef>& chosen = selection.corners;
    const auto isSelected = [&](const CornerRef& c) { return std::find(chosen.begin(), chosen.end(), c) != chosen.end(); };

    // Dragging a selected corner moves the whole selection; dragging any
    // other corner selects just that one.
    if (g.phase == DragPhase::Begin && !(chosen.size() > 1 && isSelected(g.corner))) chosen = {g.corner};
    const std::vector<CornerRef> grabbed = chosen.size() > 1 && isSelected(g.corner) ? chosen : std::vector<CornerRef>{g.corner};

    if (g.phase == DragPhase::Begin) {
        const auto movable = movableCorners(editor.document(), editor.shapes(), g.panel, grabbed, {0.0, 0.0});
        draggingCorners_ = movable.has_value();
        if (!movable) outcome.notice = movable.error();  // a formula places them
    }
    if (!draggingCorners_) return outcome;
    // Held to one axis, the other coordinate is not moved at all, whatever the pointer does.
    const Vec2 by{g.along == Along::Y ? 0.0 : g.dx, g.along == Along::X ? 0.0 : g.dy};
    if (const auto moved = editor.preview(dragIn(editor), MoveCorners{.panel = g.panel, .corners = grabbed, .by = by}); !moved) {
        outcome.notice = "Not changed: " + moved.error();  // for instance the only point of a whole circle
        outcome.refused = true;
    }
    if (g.phase == DragPhase::End) draggingCorners_ = false;
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
    apply(editor, dragIn(editor), [&](Document& d) { d.set(curve, whichEnd ? &Bezier::endHandle : &Bezier::startHandle, handle); }, byHand, outcome);
    if (image.phase == DragPhase::End) dragging_.reset();
    return outcome;
}

Outcome Drafting::on(Editor& editor, const DrawRect& g) {
    Outcome outcome{.toolDone = true};
    apply(editor, [&](Document& doc) {
        const PanelId panel = doc.add(Panel{.name = freeName(doc, "panel"), .x = g.x, .y = g.y});
        doc.add(panel, Rect{.width = Expr(g.width), .height = Expr(g.height)});
        selection = {panel, {}, {}};
    }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const DrawCircle& g) {
    Outcome outcome{.toolDone = true};
    apply(editor, [&](Document& doc) {
        const PanelId panel = doc.add(Panel{.name = freeName(doc, "circle"), .x = g.x, .y = g.y});
        doc.add(panel, Circle{.radius = Expr(g.radius)});
        selection = {panel, {}, {}};
    }, adding, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const PlaceRing& g) {
    Outcome outcome{.toolDone = true};
    apply(editor, [&](Document& doc) {
        const PanelId panel = doc.add(Panel{.name = freeName(doc, "skirt"), .x = g.x, .y = g.y});
        doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
        selection = {panel, {}, {}};
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

namespace {

// What a window needs to let a length be typed: the field, and whether it is the
// edge's own or its shape's, which end an own one keeps, and whether the other is on offer.
Outcome::LengthField lengthFieldFor(const Document& doc, const EdgeRef& shown, const LengthSetting& setting) {
    const OpData& data = doc.get(setting.operation);
    Outcome::LengthField field{.operation = setting.operation, .field = setting.field, .edge = shown};
    if (const SetLength* set = std::get_if<SetLength>(&data)) {
        field.edge = set->edge;  // the drawn side, where the length was given
        field.ownLength = true;
        field.keeping = set->keeping;
    }
    if (doc.contains(shown.op))
        for (const Dimension& dimension : dimensions(doc.get(shown.op), shown.op))
            if (dimension.edge.role == shown.role) {
                field.canSwitch = true;
                field.shapeSetting = std::string(fields(doc.get(shown.op))[dimension.field.index].label);
            }
    field.label = std::string(fields(data)[setting.field.index].label) + " of " + nameOf(Reference(shown), doc);
    field.caption = std::string(fields(data)[setting.field.index].label) + " of " + edgeName(shown, doc);
    return field;
}

} // namespace

Outcome Drafting::on(Editor& editor, const EditLength& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    if (!doc.contains(g.panel)) return outcome;
    selection.panel = g.panel;
    const Pattern& shape = editor.shapes()[g.panel];
    if (!shape.contains(g.edge)) return outcome;
    outcome.toolDone = g.ownLength;  // the Length tool has done its work, unless the edge is refused below
    const std::optional<LengthSetting> setting = lengthSetBy(doc, editor.shapes(), g.panel, g.edge);
    const bool ownAlready = setting && std::holds_alternative<SetLength>(doc.get(setting->operation));
    if (setting && (ownAlready || !g.ownLength)) {
        outcome.typeLength = lengthFieldFor(doc, g.edge, *setting);
        return outcome;
    }
    // Nothing sets this edge's length yet (a T-shirt's shoulder, or an edge whose
    // corners were dragged), or the tool wants one of its own: give it one, as
    // long as it is now, and type that. On a symmetric panel it is made for the drawn side.
    const auto given = editor.run(typingIn(editor), GiveEdgeLength{g.panel, g.edge});
    if (!given) {
        outcome.notice = "Not changed: " + given.error();
        outcome.refused = true;
        outcome.toolDone = false;  // the tool stays, to try another edge
        return outcome;
    }
    outcome.typeLength = lengthFieldFor(editor.document(), g.edge, LengthSetting{given->layer, FieldId{0}});
    return outcome;
}

Outcome Drafting::on(Editor& editor, const UseShapeLength& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    if (!doc.contains(g.panel)) return outcome;
    selection.panel = g.panel;
    if (!ownLengthsOf(doc, g.panel, drawnSide(doc, g.panel, g.edge).edge).empty()) {
        if (const auto removed = editor.run(typingIn(editor), RemoveEdgeLength{g.panel, g.edge}); !removed) {
            // The edge's own length stays what is typed.
            outcome.notice = "Not changed: " + removed.error();
            outcome.refused = true;
            if (const std::optional<LengthSetting> now = lengthSetBy(doc, editor.shapes(), g.panel, g.edge)) outcome.typeLength = lengthFieldFor(doc, g.edge, *now);
            return outcome;
        }
    }
    // The shape's setting gives the edge its length again, as long as it was when it was given up.
    if (const std::optional<LengthSetting> setting = lengthSetBy(editor.document(), editor.shapes(), g.panel, g.edge))
        outcome.typeLength = lengthFieldFor(editor.document(), g.edge, *setting);
    else
        outcome.notice = "Its corners were moved, so this edge's length is not the shape's setting now. Give it a length of its own, or change the shape's.";
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

std::vector<PanelId> Drafting::add(Editor& editor, const DuplicatePanels& command, Outcome& outcome) {
    const std::expected<DuplicatePanels::Done, std::string> made = editor.run(command);
    if (!made) {
        outcome.notice = "Not changed: " + made.error();
        outcome.refused = true;
        return {};
    }
    selection = {made->panels.front(), {}, {made->panels.begin() + 1, made->panels.end()}};
    if (made->seams > 0)
        outcome.notice = seamCount(made->seams) + " sewn as on " +
                         (command.panels.size() == 1 ? editor.document().get(command.panels.front()).name : std::string("the panels they follow")) + ".";
    return made->panels;
}

Outcome Drafting::on(Editor& editor, const AddBeside& g) {
    Outcome outcome;
    if (!editor.document().contains(g.source)) return outcome;
    add(editor, DuplicatePanels{.panels = {g.source}, .how = g.how}, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const DeletePanel& g) {
    Outcome outcome;
    if (!editor.document().contains(g.panel)) return outcome;
    if (const auto done = editor.run(DeletePanels{{g.panel}}); !done) {
        outcome.notice = "Not changed: " + done.error();
        outcome.refused = true;
        return outcome;
    }
    drop(g.panel);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const DeleteSelection& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    const std::vector<PanelId> panels = selectedInOrder(doc, selection);
    if (panels.empty()) {
        outcome.notice = "Nothing is selected. Click a panel, or some of its corners, and press Delete again.";
        outcome.refused = true;
        return outcome;
    }
    if (g.wholePanels || selection.corners.empty() || !selection.panel || !doc.contains(*selection.panel)) {
        if (const auto done = editor.run(DeletePanels{panels}); !done) {
            outcome.notice = "Not changed: " + done.error();
            outcome.refused = true;
            return outcome;
        }
        for (PanelId panel : panels) drop(panel);
        return outcome;
    }
    if (const auto done = editor.run(DeleteCorners{.panel = *selection.panel, .corners = selection.corners}); !done) {
        outcome.notice = done.error();  // a sentence that says what to do instead
        outcome.refused = true;
        return outcome;
    }
    selection.corners.clear();
    return outcome;
}

Outcome Drafting::on(Editor& editor, const CopySelection&) {
    Outcome outcome;
    const Document& doc = editor.document();
    const std::vector<PanelId> panels = selectedInOrder(doc, selection);
    if (panels.empty()) {
        outcome.notice = "Nothing to copy. Click a panel first, then press Ctrl+C.";
        outcome.refused = true;
        return outcome;
    }
    copied_ = panels;
    outcome.notice = panels.size() == 1 ? "Copied " + doc.get(panels.front()).name + ". Press Ctrl+V to paste it where the pointer is."
                                        : "Copied " + std::to_string(panels.size()) + " panels. Press Ctrl+V to paste them where the pointer is.";
    return outcome;
}

Outcome Drafting::on(Editor& editor, const DuplicateSelection& g) {
    Outcome outcome;
    const std::vector<PanelId> panels = selectedInOrder(editor.document(), selection);
    if (panels.empty()) {
        outcome.notice = "Nothing to duplicate. Click a panel first, then press Ctrl+D.";
        outcome.refused = true;
        return outcome;
    }
    add(editor, DuplicatePanels{.panels = panels, .how = g.how}, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const PastePanel& g) {
    Outcome outcome;
    if (copied_.empty()) {
        outcome.notice = "Nothing has been copied. Select a panel and press Ctrl+C first.";
        outcome.refused = true;
        return outcome;
    }
    // Panels deleted since they were copied are left out.
    const std::size_t copiedBefore = copied_.size();
    std::erase_if(copied_, [&](PanelId panel) { return !editor.document().contains(panel); });
    if (copied_.empty()) {
        outcome.notice = copiedBefore == 1 ? "The panel that was copied is gone. Select a panel and press Ctrl+C again."
                                           : "The panels that were copied are gone. Select a panel and press Ctrl+C again.";
        outcome.refused = true;
        return outcome;
    }
    add(editor, DuplicatePanels{.panels = copied_, .how = Follow::Copy, .at = g.at}, outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const SewAsSource&) {
    Outcome outcome;
    const Document& doc = editor.document();
    std::vector<PanelId> instances;
    for (PanelId panel : selectedInOrder(doc, selection))
        if (doc.instanceOf(panel)) instances.push_back(panel);
    if (instances.empty()) {
        outcome.notice = "Select an instance first: only a panel that follows another can be sewn as that one is.";
        outcome.refused = true;
        return outcome;
    }

    const auto sew = [&](Document& d) {
        std::size_t added = 0;
        for (PanelId panel : instances) added += d.sewAsSource(panel).size();
        return added;
    };
    // Tried on a copy first: with nothing to add there must be no undo step.
    Document trial = doc;
    const std::size_t sewn = sew(trial);
    if (sewn > 0) {
        if (apply(editor, [&](Document& d) { sew(d); }, adding, outcome))
            outcome.notice = seamCount(sewn) + " sewn as on " +
                             (instances.size() == 1 ? editor.document().get(editor.document().instanceOf(instances.front())->source).name
                                                    : std::string("the panels they follow")) +
                             ".";
        return outcome;
    }

    outcome.refused = true;
    const std::vector<SeamId> seams = doc.seams();
    const bool sourcesSewn = std::any_of(instances.begin(), instances.end(), [&](PanelId panel) {
        const PanelId source = doc.instanceOf(panel)->source;
        return std::any_of(seams.begin(), seams.end(), [&](SeamId id) { return doc.get(id).a.panel == source || doc.get(id).b.panel == source; });
    });
    if (!sourcesSewn)
        outcome.notice = "Nothing to sew: the selected instances follow panels without seams. Sew those panels first, then try again.";
    else
        outcome.notice = "Nothing to add: each seam is there already, or the panel on its other side has no free instance of the same kind "
                         "(plain or mirrored). Make that instance, or use the Sew tool.";
    return outcome;
}

} // namespace twintailor::session
