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
// A change is made as a command, by itself or in an edit (Commands.h). A
// Session holds the editor of the document that is open and hands it out for
// reading: there, commands are run by the session.
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
// neither, nor does a length typed on the table for a panel without seams
// (Grows::InPlaceUnlessSewn).

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
    // saved. Not the same as step(): an edit changes the document again and
    // again under one number, and a save during a drag must not hide that.
    bool unsaved() const { return content_ != saved_; }
    // The document as it is now has been written to its file.
    void markSaved() { saved_ = content_; }
    // No file holds the document as it is: a recovered copy.
    void markUnsaved() { saved_ = 0; }

    // ---- A change by itself: one undo step ----
    // Runs a command, and hands back what it has to tell; or why it cannot be
    // done, and then the document is as it was.
    template <DocumentChange C>
    std::expected<typename C::Done, std::string> run(const C& command) {
        typename C::Done done{};
        return handedBack(alone(verbOf(command, done), command.rules()), done);
    }
    // Changes the document through `verb`: any change at all, for tests (the
    // program runs commands). Returns why it was refused, if it was; then the
    // document is as it was. A verb that throws (a handle that no longer exists)
    // is refused with the exception's words.
    std::expected<void, std::string> change(const std::function<void(Document&)>& verb, ChangeRules rules = {});

    // ---- Several changes in one undo step (Commands.h) ----
    // An edit that is open is committed first. Nothing is in the history until
    // something is changed in the edit.
    Edit begin();
    // Adds a command to the edit.
    template <DocumentChange C>
    std::expected<typename C::Done, std::string> run(Edit edit, const C& command) {
        typename C::Done done{};
        return handedBack(added(edit, verbOf(command, done), command.rules()), done);
    }
    // Runs a command in place of the preview before it: on the document as it
    // was before the first preview since the edit began or since the last run in
    // it. A preview that cannot be done leaves the one before.
    template <DocumentChange C>
    std::expected<typename C::Done, std::string> preview(Edit edit, const C& command) {
        typename C::Done done{};
        return handedBack(shown(edit, verbOf(command, done), command.rules()), done);
    }
    std::expected<void, std::string> change(Edit edit, const std::function<void(Document&)>& verb, ChangeRules rules = {});
    // An undo step for the edit though the document stays as it is: what changes
    // with it is kept by its number (the session keeps the drape).
    void addStep(Edit edit);
    // The edit is over, and what it made stays.
    void commit(Edit edit);
    // The edit is over, and what it made is taken back and forgotten: nothing is
    // left to redo. Says whether there was anything to take back.
    bool cancel(Edit edit);
    // Whether this edit is still open.
    bool editing(Edit edit) const { return open_ && open_->edit == edit; }
    // Whether an edit is open that has changed something: a change that may
    // still be taken back, as during a drag.
    bool unfinished() const { return open_ && open_->stepTaken; }

    // ---- The history ----
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

    template <class Done>
    static std::expected<Done, std::string> handedBack(std::expected<void, std::string> made, Done& done) {
        if (!made) return std::unexpected(std::move(made.error()));
        return std::move(done);
    }

    // A verb run by itself, added to an edit, or shown as an edit's preview.
    std::expected<void, std::string> alone(const Verb& verb, ChangeRules rules);
    std::expected<void, std::string> added(Edit edit, const Verb& verb, ChangeRules rules);
    std::expected<void, std::string> shown(Edit edit, const Verb& verb, ChangeRules rules);
    // Makes `verb`'s change to a copy of `from`, and takes it as the document if
    // all of it can be done; with `newStep` the document as it is now stays for undo.
    std::expected<void, std::string> make(const Document& from, std::shared_ptr<const Result> before, const Verb& verb, ChangeRules rules, bool newStep);
    void takeStep(Document&& left);
    void evaluateAgain();

    Document document_;
    std::shared_ptr<const Result> shapes_;
    struct Kept {
        Document document;
        std::uint64_t step;
        std::uint64_t content;
    };
    std::vector<Kept> undo_, redo_;
    // What each preview of an edit starts from.
    struct PreviewFrom {
        Document document;
        std::shared_ptr<const Result> shapes;
    };
    struct Open {
        Edit edit;
        bool stepTaken = false;  // whether it has its undo step yet
        std::optional<PreviewFrom> previewFrom{};
    };
    std::optional<Open> open_;
    std::uint64_t lastEdit_ = 0;
    std::uint64_t shapesVersion_ = 1;
    std::uint64_t version_ = 1;
    std::uint64_t step_ = 1, lastStep_ = 1;
    // Which document this is: a new number for every change, in an edit or not,
    // while undo and redo come back to the numbers they had.
    std::uint64_t content_ = 1, lastContent_ = 1, saved_ = 1;
};

} // namespace twintailor::session
