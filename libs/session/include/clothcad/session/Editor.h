#pragma once
// The document as it is edited. Every change goes through here, so that it
// can be undone and redone, and so that a change which would break something
// that works now is refused instead of made.
//
//   Editor editor(std::move(doc));
//   auto done = editor.change([&](Document& d) { d.set(rect, &Rect::width, 40_cm); });
//   if (!done) show(done.error());   // why it was refused, in words
//   editor.undo();
//
// A drag is many changes and one undo step: the first starts the step
// (Step::New), the others continue it (Step::Continue).
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
#include <string>
#include <vector>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"

namespace clothcad::session {

enum class Step {
    New,       // starts an undo step
    Continue,  // part of the step before it, as the later moments of one drag
};

// What a change may do to the document's problems.
enum class IfItBreaks {
    Refuse,  // a change that adds a problem is not made
    Allow,   // made anyway: adding or removing things, where a problem is the honest result
};

// What a change touches. Moving a panel on the table or in space changes no
// outline, so the outlines are not evaluated again.
enum class Touches { Shapes, PlacementOnly };

// Which way a panel grows when a change reshapes it.
enum class Grows {
    // Its sewn edges stay where they are, on the table and in space: a field or
    // a formula made a skirt longer, and it grows at the hem.
    AwayFromSeams,
    // Exactly as dragged: the panel's own coordinates stay put, on the table
    // and in space, so the corner under the mouse follows the mouse.
    WhereDragged,
};

struct ChangeRules {
    Step step = Step::New;
    IfItBreaks ifItBreaks = IfItBreaks::Refuse;
    Touches touches = Touches::Shapes;
    Grows grows = Grows::AwayFromSeams;
};

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

    // Changes the document through `verb`. Returns why it was refused, if it
    // was; then the document is as it was. A verb that throws (a handle that
    // no longer exists) is refused with the exception's words.
    std::expected<void, std::string> change(const std::function<void(Document&)>& verb, ChangeRules rules = {});

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();

    // Starts again with another document, with no history: opening a file.
    void replace(Document document);

private:
    void placeNewPanels();
    void keepSeamsInPlace(const Result& before, const Result& after);
    void evaluateAgain();
    // The first problem in `after` that the current outlines do not have, in words.
    std::string newProblemIn(const Result& after, const Document& changed) const;

    Document document_;
    std::shared_ptr<const Result> shapes_;
    struct Kept {
        Document document;
        std::uint64_t step;
    };
    std::vector<Kept> undo_, redo_;
    std::uint64_t shapesVersion_ = 1;
    std::uint64_t version_ = 1;
    std::uint64_t step_ = 1, lastStep_ = 1;
};

} // namespace clothcad::session
