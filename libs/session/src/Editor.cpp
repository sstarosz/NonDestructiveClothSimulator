#include "twintailor/session/Editor.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "twintailor/core/Drawing.h"
#include "twintailor/core/Fields.h"
#include "twintailor/sim/Layout.h"

namespace twintailor::session {

namespace {

// Panels that have an outline and no place in space yet get one. Pieces only
// used as a base are never in space.
void placeNewPanels(Document& doc, const Result& shapes) {
    for (const PanelResult& shape : shapes.panels) {
        if (!doc.contains(shape.panel)) continue;
        const Panel& panel = doc.get(shape.panel);
        if (panel.arrangement.placed || panel.construction) continue;
        const Arrangement place = sim::firstPlace(panel, shape.pattern);
        if (place.placed) doc.arrange(shape.panel, place);
    }
}

// Moves each reshaped panel's anchor, and its place on the table, with the
// sewn edges that kept their length (Grows::AwayFromSeams).
void keepSeamsInPlace(Document& doc, const Result& before, const Result& after) {
    for (const PanelResult& now : after.panels) {
        if (!doc.contains(now.panel) || now.pattern.vertices.empty()) continue;
        const Panel& panel = doc.get(now.panel);
        const auto was = std::find_if(before.panels.begin(), before.panels.end(), [&](const PanelResult& r) { return r.panel == now.panel; });
        if (was == before.panels.end() || was->pattern.vertices.empty()) continue;
        const Vec2 from = sim::anchorOf(panel.arrangement, was->pattern);
        const Vec2 to = sim::anchorAfterChange(doc, now.panel, was->pattern, now.pattern, from);
        if (to == from) continue;  // its outline did not move under it
        // On the table: the spot that stays is drawn where it was.
        const Vec2 by = turned(Vec2{to.x - from.x, to.y - from.y}, panel.angle);
        const double x = panel.x - by.x, y = panel.y - by.y;
        if (panel.arrangement.placed) {
            Arrangement moved = panel.arrangement;
            moved.anchor = to;
            doc.arrange(now.panel, moved);
        }
        doc.place(now.panel, x, y);
    }
}

// The first problem in `after` that the outlines before did not have, in words.
std::string newProblemIn(const Result& after, const Result& before, const Document& changed) {
    for (const Error& error : after.errors) {
        const bool known = std::any_of(before.errors.begin(), before.errors.end(),
                                       [&](const Error& old) { return old.where == error.where && old.message == error.message; });
        if (known) continue;
        if (const Param* parameter = std::get_if<Param>(&error.where))
            return changed.contains(*parameter) ? changed.get(*parameter).name + " " + error.message : error.message;
        const OpId operation = std::get<OpId>(error.where);
        if (!changed.contains(operation)) return error.message;
        const PanelId panel = changed.panelOf(operation);
        const std::vector<OpId> siblings = changed.operations(panel);
        const auto number = static_cast<std::size_t>(std::find(siblings.begin(), siblings.end(), operation) - siblings.begin()) + 1;
        return "in " + changed.get(panel).name + ", " + std::to_string(number) + ". " + std::string(label(changed.get(operation))) + ": " + error.message;
    }
    return "it would cause a problem";
}

} // namespace

Editor::Editor(Document document) : document_(std::move(document)), shapes_(std::make_shared<const Result>(evaluate(document_))) {
    placeNewPanels(document_, *shapes_);
}

void Editor::evaluateAgain() {
    shapes_ = std::make_shared<const Result>(evaluate(document_));
    ++shapesVersion_;
}

std::expected<void, std::string> Editor::commit(const Document& from, std::shared_ptr<const Result> before, const Verb& verb, ChangeRules rules,
                                                bool newStep) {
    Document next = from;
    try {
        if (std::expected<void, std::string> done = verb(next, *before); !done) return done;
    } catch (const std::exception& problem) {
        return std::unexpected(std::string(problem.what()));
    }
    std::shared_ptr<const Result> shapes = before;
    if (rules.touches == Touches::Shapes) {
        shapes = std::make_shared<const Result>(evaluate(next));
        if (rules.ifItBreaks == IfItBreaks::Refuse && shapes->errors.size() > before->errors.size())
            return std::unexpected(newProblemIn(*shapes, *before, next));
        if (rules.grows == Grows::AwayFromSeams) keepSeamsInPlace(next, *before, *shapes);
    }
    placeNewPanels(next, *shapes);
    // With nothing to continue, a continued step starts one.
    if (newStep || undo_.empty()) {
        // Room for the step first: it is the last thing that can fail, and until here nothing has changed.
        if (undo_.size() == undo_.capacity()) undo_.reserve(std::max<std::size_t>(16, 2 * undo_.capacity()));
        undo_.push_back({std::move(document_), step_, content_});
        step_ = ++lastStep_;
    }
    document_ = std::move(next);
    if (shapes != shapes_) {
        shapes_ = std::move(shapes);
        ++shapesVersion_;
    }
    content_ = ++lastContent_;
    redo_.clear();
    ++version_;
    return {};
}

std::expected<void, std::string> Editor::change(const std::function<void(Document&)>& verb, ChangeRules rules) {
    endDrag();
    const Verb whole = [&verb](Document& doc, const Result&) -> std::expected<void, std::string> {
        verb(doc);
        return {};
    };
    return commit(document_, shapes_, whole, rules, rules.step == Step::New);
}

bool Editor::cancelDrag() {
    if (!drag_) return false;
    const bool taken = drag_->stepOpen;
    const std::uint64_t before = drag_->step;
    endDrag();
    return taken && rollBackTo(before);
}

void Editor::addStep() {
    endDrag();
    undo_.push_back({document_, step_, content_});
    step_ = ++lastStep_;
    redo_.clear();
    ++version_;
}

bool Editor::undo() {
    endDrag();
    if (undo_.empty()) return false;
    const Kept& back = undo_.back();
    redo_.push_back({std::exchange(document_, std::move(undo_.back().document)), std::exchange(step_, back.step), std::exchange(content_, back.content)});
    undo_.pop_back();
    evaluateAgain();
    ++version_;
    return true;
}

bool Editor::rollBackTo(std::uint64_t step) {
    if (step_ == step) return false;
    const bool known = std::any_of(undo_.begin(), undo_.end(), [&](const Kept& kept) { return kept.step == step; });
    if (!known) return false;
    while (step_ != step && undo()) {}
    redo_.clear();
    return true;
}

bool Editor::redo() {
    endDrag();
    if (redo_.empty()) return false;
    const Kept& next = redo_.back();
    undo_.push_back({std::exchange(document_, std::move(redo_.back().document)), std::exchange(step_, next.step), std::exchange(content_, next.content)});
    redo_.pop_back();
    evaluateAgain();
    ++version_;
    return true;
}

void Editor::replace(Document document) {
    endDrag();
    document_ = std::move(document);
    undo_.clear();
    redo_.clear();
    step_ = ++lastStep_;
    content_ = saved_ = ++lastContent_;
    evaluateAgain();
    placeNewPanels(document_, *shapes_);
    ++version_;
}

} // namespace twintailor::session
