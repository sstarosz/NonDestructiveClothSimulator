#include "clothcad/session/Editor.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "clothcad/core/Fields.h"

namespace clothcad::session {

Editor::Editor(Document document) : document_(std::move(document)), shapes_(std::make_shared<const Result>(evaluate(document_))) {}

void Editor::evaluateAgain() {
    shapes_ = std::make_shared<const Result>(evaluate(document_));
    ++shapesVersion_;
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
        shapes_ = std::move(after);
        ++shapesVersion_;
    }
    // A continued step adds nothing to undo; with nothing to continue it starts one.
    if (rules.step == Step::New || undo_.empty()) undo_.push_back(std::move(before));
    redo_.clear();
    ++version_;
    return {};
}

bool Editor::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(std::exchange(document_, std::move(undo_.back())));
    undo_.pop_back();
    evaluateAgain();
    ++version_;
    return true;
}

bool Editor::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(std::exchange(document_, std::move(redo_.back())));
    redo_.pop_back();
    evaluateAgain();
    ++version_;
    return true;
}

void Editor::replace(Document document) {
    document_ = std::move(document);
    undo_.clear();
    redo_.clear();
    evaluateAgain();
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
