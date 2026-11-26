#include "clothcad/session/Editor.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "clothcad/core/Drawing.h"
#include "clothcad/core/Fields.h"
#include "clothcad/sim/Layout.h"

namespace clothcad::session {

Editor::Editor(Document document) : document_(std::move(document)), shapes_(std::make_shared<const Result>(evaluate(document_))) {
    placeNewPanels();
}

// Panels that have an outline and no place in space yet get one. Pieces only
// used as a base are never in space.
void Editor::placeNewPanels() {
    for (const PanelResult& shape : shapes_->panels) {
        if (!document_.contains(shape.panel)) continue;
        const Panel& panel = document_.get(shape.panel);
        if (panel.arrangement.placed || panel.construction) continue;
        const Arrangement place = sim::firstPlace(panel, shape.pattern);
        if (place.placed) document_.arrange(shape.panel, place);
    }
}

void Editor::evaluateAgain() {
    shapes_ = std::make_shared<const Result>(evaluate(document_));
    ++shapesVersion_;
}

void Editor::keepSeamsInPlace(const Result& before, const Result& after) {
    for (const PanelResult& now : after.panels) {
        if (!document_.contains(now.panel) || now.pattern.vertices.empty()) continue;
        const Panel& panel = document_.get(now.panel);
        const auto was = std::find_if(before.panels.begin(), before.panels.end(), [&](const PanelResult& r) { return r.panel == now.panel; });
        if (was == before.panels.end() || was->pattern.vertices.empty()) continue;
        const Vec2 from = sim::anchorOf(panel.arrangement, was->pattern);
        const Vec2 to = sim::anchorAfterChange(document_, now.panel, was->pattern, now.pattern, from);
        if (to == from) continue;  // its outline did not move under it
        // On the table: the spot that stays is drawn where it was.
        const Vec2 by = turned(Vec2{to.x - from.x, to.y - from.y}, panel.angle);
        const double x = panel.x - by.x, y = panel.y - by.y;
        if (panel.arrangement.placed) {
            Arrangement moved = panel.arrangement;
            moved.anchor = to;
            document_.arrange(now.panel, moved);
        }
        document_.place(now.panel, x, y);
    }
}

std::expected<void, std::string> Editor::change(const std::function<void(Document&)>& verb, ChangeRules rules) {
    Document before = document_;
    try {
        verb(document_);
    } catch (const std::exception& problem) {
        document_ = std::move(before);
        return std::unexpected(std::string(problem.what()));
    }
    if (rules.touches == Touches::Shapes) {
        auto after = std::make_shared<const Result>(evaluate(document_));
        if (rules.ifItBreaks == IfItBreaks::Refuse && after->errors.size() > shapes_->errors.size()) {
            std::string why = newProblemIn(*after, document_);
            document_ = std::move(before);
            return std::unexpected(std::move(why));
        }
        if (rules.grows == Grows::AwayFromSeams) keepSeamsInPlace(*shapes_, *after);
        shapes_ = std::move(after);
        ++shapesVersion_;
    }
    placeNewPanels();
    // A continued step adds nothing to undo; with nothing to continue it starts one.
    if (rules.step == Step::New || undo_.empty()) {
        undo_.push_back({std::move(before), step_, content_});
        step_ = ++lastStep_;
    }
    content_ = ++lastContent_;
    redo_.clear();
    ++version_;
    return {};
}

bool Editor::undo() {
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
    if (redo_.empty()) return false;
    const Kept& next = redo_.back();
    undo_.push_back({std::exchange(document_, std::move(redo_.back().document)), std::exchange(step_, next.step), std::exchange(content_, next.content)});
    redo_.pop_back();
    evaluateAgain();
    ++version_;
    return true;
}

void Editor::replace(Document document) {
    document_ = std::move(document);
    undo_.clear();
    redo_.clear();
    step_ = ++lastStep_;
    content_ = saved_ = ++lastContent_;
    evaluateAgain();
    placeNewPanels();
    ++version_;
}

std::string Editor::newProblemIn(const Result& after, const Document& changed) const {
    for (const Error& error : after.errors) {
        const bool known = std::any_of(shapes_->errors.begin(), shapes_->errors.end(),
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

} // namespace clothcad::session
