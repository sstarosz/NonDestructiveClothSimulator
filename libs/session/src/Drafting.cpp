#include "clothcad/session/Drafting.h"

#include <algorithm>
#include <cmath>

#include "clothcad/core/Evaluate.h"
#include "clothcad/session/Lengths.h"
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

// "front copy", or "front copy 2" if that is taken.
std::string firstFreeName(const Document& doc, const std::string& wanted) {
    if (!panelNamed(doc, wanted)) return wanted;
    for (int n = 2;; ++n)
        if (!panelNamed(doc, wanted + " " + std::to_string(n))) return wanted + " " + std::to_string(n);
}

// Where a panel that follows `source` goes: just right of it, with room to
// spare. A mirrored panel's own left edge is the source's right one.
Vec2 besideSource(const Editor& editor, PanelId source, Follow how) {
    const Panel& from = editor.document().get(source);
    const Result& shapes = editor.shapes();
    double x = from.x + 30_cm;
    if (const std::optional<Bounds> box = bounds(shapes[source])) {
        const double newLeft = how == Follow::MirroredInstance ? -box->max.x : box->min.x;
        const std::optional<Bounds> placed = bounds(turned(shapes[source], from.angle));
        x = from.x + (placed ? placed->max.x : box->max.x) + 12_cm - newLeft;
    }
    return {x, from.y};
}

// "1 seam", "2 seams".
std::string seamCount(std::size_t count) { return std::to_string(count) + (count == 1 ? " seam" : " seams"); }

// Where a mirrored instance of `source` first stands in space: as its source,
// mirrored across the body's middle (a second leg beside the first). None, so
// that it stands where the table has it, as any new panel, if the source has no
// place of its own, or its anchor is less than a quarter of its width from the
// middle: its mirror image would start inside it.
std::optional<Arrangement> mirrorImageInSpace(const Editor& editor, PanelId source) {
    Arrangement from = editor.document().get(source).arrangement;
    const Pattern& outline = editor.shapes()[source];
    const std::optional<Bounds> box = bounds(outline);
    if (!from.placed || !box || std::abs(from.x) < (box->max.x - box->min.x) / 4.0) return std::nullopt;
    // Said, though it may be the middle of the box: the instance's anchor is the image of this spot.
    from.anchor = sim::anchorOf(from, outline);
    return sim::mirrored(from);
}

// A new panel like each `source`, at its place on the table, all in one change.
// Instances are sewn as their sources are, and a mirrored instance of a panel
// at one side of the body stands in space as its mirror image. Empty if the
// change is refused.
struct Placed {
    PanelId source;
    Vec2 at;
};
std::vector<PanelId> addLike(Editor& editor, const std::vector<Placed>& wanted, Follow how, Outcome& outcome) {
    const std::string suffix = how == Follow::Copy ? " copy" : how == Follow::Instance ? " instance" : " mirrored";
    std::vector<PanelId> added;
    std::size_t sewn = 0;
    apply(editor, [&](Document& doc) {
        added.clear();
        sewn = 0;
        for (const Placed& one : wanted) {
            const std::string name = firstFreeName(doc, doc.get(one.source).name + suffix);
            if (how == Follow::Copy) {
                const auto particleDistance = doc.get(one.source).particleDistance;
                added.push_back(doc.copy(one.source, Panel{.name = name, .x = one.at.x, .y = one.at.y, .particleDistance = particleDistance}));
            } else {
                const PanelId panel = doc.add(Panel{.name = name, .x = one.at.x, .y = one.at.y});
                doc.add(panel, Instance{.source = one.source, .mirrored = how == Follow::MirroredInstance});
                if (how == Follow::MirroredInstance)
                    if (const std::optional<Arrangement> image = mirrorImageInSpace(editor, one.source)) doc.arrange(panel, *image);
                added.push_back(panel);
            }
        }
        // Only when they are all there can they be sewn to each other. A copy is not an instance: it gets none.
        for (PanelId panel : added) sewn += doc.sewAsSource(panel).size();
    }, adding, outcome);
    if (outcome.refused) added.clear();
    else if (sewn > 0)
        outcome.notice = seamCount(sewn) + " sewn as on " +
                         (wanted.size() == 1 ? editor.document().get(wanted.front().source).name : std::string("the panels they follow")) + ".";
    return added;
}

// Where copies (or instances) of a group go: it keeps its shape, with the middle of its box at
// `at`, or just right of where it is when there is no place. Mirrored instances are laid out as
// the mirror image of the group, so that each stands to the others as its source does, flipped.
std::vector<Placed> groupPlaces(const Editor& editor, const std::vector<PanelId>& sources, const std::optional<Vec2>& at, Follow how) {
    const Document& doc = editor.document();
    Vec2 low{1e300, 1e300}, high{-1e300, -1e300};
    for (PanelId source : sources) {
        const Panel& from = doc.get(source);
        const std::optional<Bounds> box = bounds(editor.shapes()[source]);
        const Bounds own = box ? *box : Bounds{};
        low = {std::min(low.x, from.x + own.min.x), std::min(low.y, from.y + own.min.y)};
        high = {std::max(high.x, from.x + own.max.x), std::max(high.y, from.y + own.max.y)};
    }
    const Vec2 by = at ? Vec2{at->x - (low.x + high.x) / 2.0, at->y - (low.y + high.y) / 2.0} : Vec2{high.x - low.x + 12_cm, 0.0};
    std::vector<Placed> places;
    for (PanelId source : sources) {
        const Panel& from = doc.get(source);
        // A mirrored instance's origin mirrors its source's, about the middle of the group.
        const double x = how == Follow::MirroredInstance ? low.x + high.x - from.x : from.x;
        places.push_back({source, {x + by.x, from.y + by.y}});
    }
    return places;
}

// The selected panels in the document's order; only those that are still there.
std::vector<PanelId> selectedInOrder(const Document& doc, const Drafting::Selection& selection) {
    std::vector<PanelId> panels;
    for (PanelId panel : doc.panels())
        if (selection.has(panel)) panels.push_back(panel);
    return panels;
}

// "2. Curve": a layer by its place in its panel and its label, as the window names it.
std::string layerName(const Document& doc, OpId layer) {
    const std::vector<OpId> siblings = doc.operations(doc.panelOf(layer));
    const auto index = static_cast<std::size_t>(std::find(siblings.begin(), siblings.end(), layer) - siblings.begin());
    return std::to_string(index + 1) + ". " + std::string(label(doc.get(layer)));
}

} // namespace

Along axisOf(double dx, double dy) { return std::abs(dx) >= std::abs(dy) ? Along::X : Along::Y; }

std::string Drafting::freeName(const Document& doc, const std::string& base) {
    for (int number = 1;; ++number) {
        const std::string name = base + std::to_string(number);
        if (!panelNamed(doc, name)) return name;
    }
}

Outcome Drafting::handle(Editor& editor, const Gesture& gesture) {
    const Outcome outcome = std::visit([&](const auto& g) { return on(editor, g); }, gesture);
    if (outcome.toolDone) tool = Tool::Select;
    if (selection.panel) selection.seam.reset();  // whatever selected a panel lets go of the seam
    return outcome;
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
    cornerDrags_.clear();
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
        apply(editor, [&](Document& doc) {
            for (const GroupStart& s : groupStarts_) {
                if (!doc.contains(s.panel)) continue;
                const Vec2 offset = turned(Vec2{s.x - pivot.x, s.y - pivot.y}, by);
                doc.place(s.panel, pivot.x + offset.x, pivot.y + offset.y);
                doc.turn(s.panel, s.angle + by);
            }
        }, placing(g.phase == DragPhase::Begin), outcome);
        if (g.phase == DragPhase::End) groupStarts_.clear();
        return outcome;
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
    if (!scaling_->stepOpen && unchanged) {  // a drag that has not moved changes nothing yet
        if (g.phase == DragPhase::End) scaling_.reset();
        return outcome;
    }
    Scaling next = *scaling_;
    const bool done = apply(editor, [&](Document& d) {
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
    }, byHand(!scaling_->stepOpen), outcome);
    if (done) {
        scaling_ = std::move(next);
        scaling_->stepOpen = true;
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
            apply(editor, [&](Document& d) {
                for (const auto& [panel, at] : places) d.place(panel, at.x, at.y);
                for (const auto& [panel, arranged] : anchors) d.arrange(panel, arranged);
            }, placing(false), outcome);
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
    apply(editor, [&](Document& d) { d.arrange(panel, arranged); }, placing(phase == DragPhase::Begin), outcome);
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
        apply(editor, [&](Document& doc) {
            for (const GroupStart& s : groupStarts_)
                if (doc.contains(s.panel)) doc.place(s.panel, s.x + dx, s.y + dy);
        }, placing(g.phase == DragPhase::Begin), outcome);
        if (g.phase == DragPhase::End) groupStarts_.clear();
        return outcome;
    }
    apply(editor, [&](Document& doc) { doc.place(g.panel, g.x, g.y); }, placing(g.phase == DragPhase::Begin), outcome);
    return outcome;
}

Outcome Drafting::on(Editor& editor, const MoveCorner& image) {
    Outcome outcome;
    const Document& doc = editor.document();
    bool newStep = false;  // whether this change opens the drag's undo step
    if (!doc.contains(image.panel)) return outcome;
    // Held to one axis, the other coordinate is not moved at all, whatever the pointer does.
    const double alongX = image.along == Along::Y ? 0.0 : image.dx, alongY = image.along == Along::X ? 0.0 : image.dy;
    const Vec2 own = turned(Vec2{alongX, alongY}, -doc.get(image.panel).angle);  // the drag in the panel's own coordinates
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
                         const std::string why = cornerLock(doc, editor.shapes(), image.panel, m.corner);
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

// Which end of an edge stays where it is when the edge is given a length: the
// end on the mirror of a symmetric panel (the fold), else neither, and both
// ends move.
SetLength::Keeping keepingFor(const Document& doc, PanelId panel, const EdgeRef& drawn, const Pattern& shape) {
    const std::optional<OpRef<Mirror>> mirror = symmetryOf(doc, panel);
    if (!mirror || !shape.contains(drawn)) return SetLength::Keeping::Middle;
    // The mirror's own edge is gone from the finished outline: look at the half it was made from.
    const Pattern half = outlineBefore(doc, *mirror);
    const EdgeRef& across = doc.get(*mirror).across;
    if (!half.contains(across)) return SetLength::Keeping::Middle;
    const Edge& fold = half.edge(across);
    const Edge& edge = shape.edge(drawn);
    const auto onFold = [&](uint32_t corner) {
        const CornerRef& name = shape.vertices[corner].name;
        return half.vertices[fold.start].name == name || half.vertices[fold.end].name == name;
    };
    if (onFold(edge.start)) return SetLength::Keeping::Start;
    if (onFold(edge.end)) return SetLength::Keeping::End;
    return SetLength::Keeping::Middle;
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
    const DrawnEdge drawn = drawnSide(doc, g.panel, g.edge);
    const SetLength::Keeping keeping = keepingFor(doc, g.panel, drawn.edge, shape);
    OpRef<SetLength> added;
    if (!apply(editor, [&](Document& d) { added = d.add(g.panel, SetLength{.edge = drawn.edge, .length = Expr(shape.length(g.edge)), .keeping = keeping}); }, {}, outcome)) {
        outcome.toolDone = false;  // refused: the tool stays, to try another edge
        return outcome;
    }
    outcome.typeLength = lengthFieldFor(editor.document(), g.edge, LengthSetting{added, FieldId{0}});
    return outcome;
}

Outcome Drafting::on(Editor& editor, const UseShapeLength& g) {
    Outcome outcome;
    const Document& doc = editor.document();
    if (!doc.contains(g.panel)) return outcome;
    selection.panel = g.panel;
    const EdgeRef drawn = drawnSide(doc, g.panel, g.edge).edge;
    std::vector<OpId> own;
    for (OpId op : doc.operations(g.panel))
        if (const SetLength* set = std::get_if<SetLength>(&doc.get(op)); set && set->edge == drawn && doc.enabled(op)) own.push_back(op);  // one switched off is kept
    if (!own.empty()) {
        // Would the shape's setting give the edge its length? Worked out on a copy, so that when it would
        // not (a corner was moved after the shape made the edge) nothing is taken away, and the edge's own
        // length stays what is typed.
        Document trial = doc;
        for (OpId op : own) trial.remove(op);
        if (!lengthSetBy(trial, evaluate(trial), g.panel, g.edge)) {
            outcome.notice = "Not changed: the edge's corners were moved after the shape made it, so the shape's setting no longer gives it a length. Keep the edge's own length, or change the corners.";
            outcome.refused = true;
            if (const std::optional<LengthSetting> now = lengthSetBy(doc, editor.shapes(), g.panel, g.edge)) outcome.typeLength = lengthFieldFor(doc, g.edge, *now);
            return outcome;
        }
        if (!apply(editor, [&](Document& d) { for (OpId op : own) d.remove(op); }, {}, outcome)) return outcome;
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

Outcome Drafting::on(Editor& editor, const AddBeside& g) {
    Outcome outcome;
    if (!editor.document().contains(g.source)) return outcome;
    const std::vector<PanelId> added = addLike(editor, {{g.source, besideSource(editor, g.source, g.how)}}, g.how, outcome);
    if (!added.empty()) selection = {added.front(), {}, {}};
    return outcome;
}

Outcome Drafting::on(Editor& editor, const DeletePanel& g) {
    Outcome outcome;
    if (!editor.document().contains(g.panel)) return outcome;
    if (!apply(editor, [&](Document& doc) { doc.remove(g.panel); }, adding, outcome)) return outcome;
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
        if (panels.size() == 1) return on(editor, DeletePanel{panels.front()});
        // Several panels go in one undo step. What used one of them stays and shows a problem.
        if (!apply(editor, [&](Document& d) {
                for (PanelId panel : panels) d.remove(panel);
            }, adding, outcome))
            return outcome;
        for (PanelId panel : panels) drop(panel);
        return outcome;
    }
    const PanelId panel = *selection.panel;

    // A corner can only go with the layer that made it, and only a Point-tool
    // layer is only that corner. Any other corner is part of a shape: say which.
    std::vector<OpId> points;
    for (const CornerRef& corner : selection.corners) {
        const OpId made = drawnSide(doc, panel, corner).corner.op;
        if (!doc.contains(made)) continue;
        if (!(doc.panelOf(made) == panel)) {
            outcome.notice = "This corner comes from the panel " + doc.get(doc.panelOf(made)).name + ". Change that panel instead.";
        } else if (!std::holds_alternative<AddPoint>(doc.get(made))) {
            outcome.notice = "This corner belongs to the " + layerName(doc, made) + " layer. Remove or change that layer instead.";
        } else {
            if (std::find(points.begin(), points.end(), made) == points.end()) points.push_back(made);
            continue;
        }
        outcome.refused = true;
        return outcome;
    }
    if (points.empty()) return outcome;
    if (!apply(editor, [&](Document& d) {
            for (OpId point : points) d.remove(point);
        }, adding, outcome))
        return outcome;
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
    if (panels.size() == 1) return on(editor, AddBeside{panels.front(), g.how});
    const std::vector<PanelId> added = addLike(editor, groupPlaces(editor, panels, std::nullopt, g.how), g.how, outcome);
    if (!added.empty()) selection = {added.front(), {}, {added.begin() + 1, added.end()}};
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
    std::vector<Placed> places;
    if (copied_.size() > 1) {
        places = groupPlaces(editor, copied_, g.at, Follow::Copy);
    } else {
        const PanelId source = copied_.front();
        Vec2 place = besideSource(editor, source, Follow::Copy);
        if (g.at) {
            // The copy's middle goes under the pointer.
            const std::optional<Bounds> box = bounds(editor.shapes()[source]);
            const Vec2 middle = box ? Vec2{(box->min.x + box->max.x) / 2.0, (box->min.y + box->max.y) / 2.0} : Vec2{};
            place = {g.at->x - middle.x, g.at->y - middle.y};
        }
        places = {{source, place}};
    }
    const std::vector<PanelId> added = addLike(editor, places, Follow::Copy, outcome);
    if (!added.empty()) {
        selection = {added.front(), {}, {added.begin() + 1, added.end()}};
    }
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

} // namespace clothcad::session
