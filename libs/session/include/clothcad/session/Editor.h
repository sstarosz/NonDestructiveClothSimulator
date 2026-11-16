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

struct ChangeRules {
    Step step = Step::New;
    IfItBreaks ifItBreaks = IfItBreaks::Refuse;
    Touches touches = Touches::Shapes;
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
    void evaluateAgain();
    // The first problem in `after` that the current outlines do not have, in words.
    std::string newProblemIn(const Result& after, const Document& changed) const;

    Document document_;
    std::shared_ptr<const Result> shapes_;
    std::vector<Document> undo_, redo_;
    std::uint64_t shapesVersion_ = 1;
    std::uint64_t version_ = 1;
};

} // namespace clothcad::session
