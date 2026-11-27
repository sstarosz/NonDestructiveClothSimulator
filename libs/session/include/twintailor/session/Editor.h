#pragma once
// The document as it is edited. Every change goes through here, so that it
// can be undone and redone, and so that a change which would break something
// that works now is refused instead of made.
//
//   Editor editor(std::move(doc));
//   auto done = editor.run(SetField{.field = waist, .value = 80_cm});
//   if (!done) show(done.error());   // why it was refused, in words
//   editor.undo();
//
// A change is made as a command (Commands.h). Dragged, it is run again and
// again from the document as it was (drag, then endDrag or cancelDrag). What
// has no command yet is changed through change(), where a drag is many changes
// and one undo step: the first starts the step (Step::New), the others
// continue it (Step::Continue).
//
// A panel gets its place in space once: the first time it has an outline,
// in the same step as that change, it stands where the 2D table has it then
// (sim::firstPlace). From then on the table and space are separate: moving or
// turning a panel on the table does not move it in space.
//
// When a panel's outline changes, its sewn edges stay where they are, in
// space and on the table: its anchor (Arrangement::anchor) and its place on
// the table move with them, so a longer skirt grows at the hem in both views.
// A change made by dragging the outline itself (Grows::WhereDragged) moves
// neither.

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/session/Commands.h"

namespace twintailor::session {

class Editor {
public:
    explicit Editor(Document document = {});

    const Document& document() const { return document_; }
    // The outlines and parameter values as of now, and a share of them that
    // stays valid however the document changes later.
    const Result& shapes() const { return *shapes_; }
    std::shared_ptr<const Result> sharedShapes() const { return shapes_; }
    // Goes up whenever the outlines are evaluated again.
    std::uint64_t shapesVersion() const { return shapesVersion_; }
    // Goes up with every change, undo and redo.
    std::uint64_t version() const { return version_; }
    // Which state of the history the document is in: a new number for every
    // new undo step, while undo and redo come back to the numbers they had.
    // What is kept per state (the session keeps the drape) is kept by it.
    std::uint64_t step() const { return step_; }

    // Whether the document differs from the one last saved. Undo and redo back
    // to the saved state count as saved. A new or newly opened document starts
    // saved. Not the same as step(): a continued step changes the document
    // under the same number, and a save during a drag must not hide that.
    bool unsaved() const { return content_ != saved_; }
    // The document as it is now has been written to its file.
    void markSaved() { saved_ = content_; }
    // No file holds the document as it is: a recovered copy.
    void markUnsaved() { saved_ = 0; }

    // Runs a command: one undo step, and what the command hands back; or why it
    // cannot be done, and then the document is as it was.
    template <DocumentChange C>
    std::expected<typename C::Done, std::string> run(const C& command) {
        endDrag();
        typename C::Done done{};
        const std::expected<void, std::string> made = commit(document_, shapes_, verbOf(command, done), command.rules(), true);
        if (!made) return std::unexpected(made.error());
        return done;
    }

    // Runs a command as the latest moment of a drag: on the document as it was
    // when the drag began, in place of what the moment before made. All the
    // moments are one undo step. A moment that cannot be done is said why and
    // leaves the one before.
    template <DocumentChange C>
    std::expected<typename C::Done, std::string> drag(const C& command) {
        if (!drag_) drag_ = Dragged{document_, shapes_, step_};
        typename C::Done done{};
        const std::expected<void, std::string> made = commit(drag_->from, drag_->shapes, verbOf(command, done), command.rules(), !drag_->stepOpen);
        if (!made) return std::unexpected(made.error());
        drag_->stepOpen = true;
        return done;
    }
    // The drag is over, and what it made stays. Anything else that changes the
    // history (run, change, undo, redo, replace) ends it too.
    void endDrag() { drag_.reset(); }
    // The drag is taken back and forgotten: nothing is left to redo. Says
    // whether there was anything to take back.
    bool cancelDrag();

    // An undo step that leaves the document as it is: what changes with it is
    // kept by its number (the session keeps the drape).
    void addStep();

    // Changes the document through `verb`: what has no command yet. Returns why
    // it was refused, if it was; then the document is as it was. A verb that
    // throws (a handle that no longer exists) is refused with the exception's words.
    std::expected<void, std::string> change(const std::function<void(Document&)>& verb, ChangeRules rules = {});

    // Takes back every step down to the state `step` was (see step()), as undo does, but the steps
    // taken back are forgotten, not kept to redo: a cancelled edit leaves nothing behind. Says whether
    // it took back anything; a `step` that is not in the history takes back nothing.
    bool rollBackTo(std::uint64_t step);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();

    // Starts again with another document, with no history: opening a file.
    void replace(Document document);

private:
    // A change made to a document whose outlines were `before`, or why not.
    using Verb = std::function<std::expected<void, std::string>(Document&, const Result& before)>;

    template <DocumentChange C>
    static Verb verbOf(const C& command, typename C::Done& done) {
        return [&command, &done](Document& doc, const Result& before) -> std::expected<void, std::string> {
            std::expected<typename C::Done, std::string> made = command.apply(doc, before);
            if (!made) return std::unexpected(std::move(made.error()));
            done = std::move(*made);
            return {};
        };
    }

    // Makes `verb`'s change to a copy of `from`, and takes it as the document if
    // all of it can be done; with `newStep` the document as it is now stays for undo.
    std::expected<void, std::string> commit(const Document& from, std::shared_ptr<const Result> before, const Verb& verb, ChangeRules rules, bool newStep);
    void evaluateAgain();

    Document document_;
    std::shared_ptr<const Result> shapes_;
    struct Kept {
        Document document;
        std::uint64_t step;
        std::uint64_t content;
    };
    std::vector<Kept> undo_, redo_;
    // A command being dragged: what each moment of it starts from.
    struct Dragged {
        Document from;
        std::shared_ptr<const Result> shapes;
        std::uint64_t step;     // the state before the drag
        bool stepOpen = false;  // whether a moment of it has been taken, in an undo step of its own
    };
    std::optional<Dragged> drag_;
    std::uint64_t shapesVersion_ = 1;
    std::uint64_t version_ = 1;
    std::uint64_t step_ = 1, lastStep_ = 1;
    // Which document this is: a new number for every change, continued or not,
    // while undo and redo come back to the numbers they had.
    std::uint64_t content_ = 1, lastContent_ = 1, saved_ = 1;
};

} // namespace twintailor::session
