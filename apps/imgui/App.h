#pragma once
// The window's contents: parameters on the left; in the middle a toolbar and
// the canvas, where patterns are made; on the right the details of the
// selected panel.
//
// The App owns the Document. It changes it only through Document's verbs,
// keeps a copy before each change for undo, and recomputes all geometry with
// evaluate() after each change.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Canvas.h"
#include "clothcad/session/Session.h"
#include "View3D.h"
#include "clothcad/sim/Cloth.h"
#include "Samples.h"
#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"

namespace clothcad::app {

class App {
public:
    App();

    // Replaces the document: with one of the example garments, an empty
    // one, or any document under a title.
    void open(const Sample& sample);
    void openEmpty();
    void open(Document document, const char* title);

    // Draws the whole interface for one frame.
    void frame();

    const Document& document() const { return session_.document(); }
    const session::Session& session() const { return session_; }
    // Whether the cloth mesh is drawn over the panels.
    void setShowMesh(bool show) { showMesh_ = show; }
    // Puts the cloth on a body read from a Wavefront OBJ file (cm, y up,
    // standing on the floor, facing +z). Returns why it could not, or nothing.
    std::string loadAvatar(const std::string& path);
    // Goes back to the built-in mannequin.
    void useMannequin();
    // Whether the body is ready to be collided with (an avatar takes a moment).
    bool bodyReady() const { return session_.bodyReady(); }
    // Whether the 3D view is shown beside the table.
    void setShow3D(bool show) { show3D_ = show; }
    void setParticleDistance(float cm) { particleDistance_ = cm; }
    const View3D& view3D() const { return view3d_; }
    const sim::Placement& placement() const { return session_.shownPositions(); }
    // The mesh those positions belong to: while the cloth is being carried over to a changed pattern it can be the one before.
    const sim::ClothMesh& shownMesh() const { return session_.shownMesh(); }
    // Whether the cloth runs on a thread of its own (the default) or one step per frame, which repeats exactly and is what tests use. Resets the cloth.
    void runClothOnItsOwnThread(bool own) { session_.setThreading(own ? session::Threading::OwnThread : session::Threading::OneStepPerAdvance); simulating_ = false; }
    // Starts or pauses the simulation; the cloth stays as it is when paused.
    void setSimulating(bool on) { simulating_ = on; }
    bool simulating() const { return simulating_; }
    // How long the cloth has been simulated, in seconds; empty before the first step and after a reset.
    std::optional<double> simulatedTime() const { return session_.clothShown() ? std::optional<double>(session_.cloth().time) : std::nullopt; }
    const sim::ClothMesh& mesh() const { return *session_.mesh(); }
    // How many times the mesh has been made, for tests: only a change of shape should make it again.
    std::uint64_t meshesMade() const { return session_.meshesMade(); }
    const std::vector<CornerRef>& selectedCorners() const { return selectedCorners_; }
    Tool tool() const { return tool_; }
    std::optional<PanelId> selected() const { return selected_; }
    const Canvas& canvas() const { return canvas_; }

private:
    // What the user just did to a formula field.
    struct FieldEdit {
        Expr value;
        bool continuesDrag;  // a later step of one drag: part of the same undo step
    };

    void undo();
    void redo();
    // Forgets what was half done on the canvas and in the panes, after the document changed under it.
    void forgetHalfDone();

    // Applies a value edit through `verb(document, value)` and recomputes.
    // An edit that would break something that works now (a width going
    // negative, a formula dividing by zero) is refused: the document stays as
    // it was and the reason is shown. One drag, however long, is one undo step.
    template <class Verb>
    bool apply(const FieldEdit& edit, Verb verb) {
        const bool changed = change(!edit.continuesDrag, [&](Document& doc) { verb(doc, edit.value); });
        if (changed && drag_) drag_->changed = true;
        return changed;
    }

    // Changes values in the document through `verb(document)` and recomputes,
    // unless that would break something that works now; then nothing changes
    // and the reason is shown. `newUndoStep` is false for the later steps of
    // one drag.
    bool change(bool newUndoStep, const std::function<void(Document&)>& verb);
    // The same for moving panels, on the table or in space: no outline changes.
    void place(bool newUndoStep, const std::function<void(Document&)>& verb);

    // Queues a change that adds, removes or reorders things. It runs once the
    // frame's panes are drawn, so the lists being drawn don't change under them.
    void later(std::function<void(Document&)> change) { queued_.push_back(std::move(change)); }
    void runQueuedChanges();

    void menuBar();
    void filePopup();
    void saveTo(const std::string& path);
    bool openFrom(const std::string& path);
    void parametersPane();
    void toolbar();
    void detailsPane();
    static void arrangeInARow(Document& doc);

    // What each canvas gesture does to the document.
    void handle(const Gesture& gesture);
    void on(const SelectPanel& gesture);
    void on(const MovePanel& gesture);
    void on(const MoveCorner& gesture);
    void on(const MoveHandle& gesture);
    void on(const DrawRect& gesture);
    void on(const DrawCircle& gesture);
    void on(const EditLength& gesture);
    void on(const RotatePanel& gesture);
    void on(const SelectCorners& gesture);
    void on(const SewEdges& gesture);
    void seamsOf(PanelId panel);
    void arrangementOf(PanelId panel);
    void lengthPopup();
    void on(const PlaceRing& gesture);
    void on(const BendEdge& gesture);
    void on(const DrawLine& gesture);
    void on(const CutAway& gesture);
    void on(const SplitAt& gesture);
    void on(const MirrorAcross& gesture);
    // Adds a panel that follows `source` (a copy, an instance or a mirrored
    // instance), placed beside it.
    enum class Follow { Copy, Instance, MirroredInstance };
    void addBeside(PanelId source, Follow how);
    void operationsOf(PanelId panel);
    void addOperationButton(PanelId panel);
    void referenceRows(OpId operation);
    void choiceRows(OpId operation);
    void formulaRows(OpId operation);
    std::optional<PickRequest> pickRequest();

    // A formula field. A typed number can be dragged up and down like a
    // slider; a click opens the field for typing a number or a formula.
    // `limits`, when given, are shown as the field's rule.
    // `step` is the change per pixel of a drag; 0 picks one from the value.
    std::optional<FieldEdit> formulaField(const Expr& current, const Limits* limits = nullptr, double step = 0.0);
    std::optional<FieldEdit> typingField(float width);

    // A text field for a name. Returns the new name once the user leaves the
    // field having changed it.
    std::optional<std::string> nameField(const std::string& current);
    void renameParameter(Param parameter, const std::string& name);

    const std::string* problemWith(Param parameter) const;
    const std::string* problemWith(OpId operation) const;

    std::optional<PanelId> selected_;
    Canvas canvas_;
    Tool tool_ = Tool::Select;
    std::string title_;

    // The document, its history and everything worked out from it.
    session::Session session_;
    const Result& shapes() const { return session_.editor().shapes(); }

    // The cloth mesh shown over the panels, when the user asks to see it.
    bool showMesh_ = false;
    bool show3D_ = true;
    View3D view3d_;
    bool showBody_ = true;
    bool simulating_ = false;
    std::optional<Arrangement> carriedFrom_;  // the arrangement a panel had when a drag in 3D took hold of it
    float particleDistance_ = 2.0f;  // cm

    // The file the document was opened from or last saved to; empty if none.
    std::string path_;
    enum class FileAsk { None, Open, SaveAs, Avatar };
    FileAsk fileAsk_ = FileAsk::None;  // which question the file popup is asking
    bool openFilePopup_ = false;
    char pathText_[512] = "pattern.clothcad";
    std::string fileProblem_;
    std::vector<std::function<void(Document&)>> queued_;
    std::string notice_;  // why the last thing the user tried was refused

    // The reference the user is choosing by clicking on the canvas.
    struct Picking {
        OpId operation;
        ReferenceId reference;
    };
    std::optional<Picking> picking_;

    // The operation a corner or handle drag on the canvas is writing to.
    struct CanvasDrag {
        OpId operation;
        bool endHandle;      // for a handle drag: which handle
        double baseX, baseY; // its values when the drag began
    };
    std::optional<CanvasDrag> canvasDrag_;

    // Corners selected on the selected panel, to be moved together.
    std::vector<CornerRef> selectedCorners_;
    struct CornerDrag {
        CornerRef corner;  // the drawn original that moves
        OpId operation;    // the adjustment it is written to
    };
    std::vector<CornerDrag> cornerDrags_;  // while a group of corners is dragged

    // A panel being turned by its knob: it turns about the table position of
    // its box's centre, which stays where it is.
    struct Turning {
        Vec2 centre;  // the centre of the panel's box, in its own coordinates
        Vec2 pivot;   // where that is on the table
    };
    std::optional<Turning> turning_;

    // The step of the selected panel that the canvas is showing, when the
    // user is looking back through how it was made. Empty: the finished panel.
    std::optional<OpId> viewUntil_;
    Result past_;  // the document as of that step

    // The length the user is typing after double-clicking it on the canvas.
    struct LengthEdit {
        OpId operation;
        FieldId field;
        std::string label;  // "Width of 1. Rectangle"
    };
    std::optional<LengthEdit> lengthEdit_;
    char lengthText_[160] = {};
    std::string lengthProblem_;
    bool openLengthPopup_ = false;

    // ---- State of the text fields (fields are named by their ImGui id) ----
    struct Drag {
        unsigned int field;
        double start;          // the value when the drag began
        double step;           // change per pixel
        bool changed = false;  // whether it has changed the document yet
    };
    std::optional<Drag> drag_;

    unsigned int typing_ = 0;       // the formula field open for typing; 0 if none
    char typed_[160] = {};          // its text
    bool focusTyping_ = false;      // put the keyboard in it on the next frame
    int ignoreBlurUntilFrame_ = 0;  // Enter briefly takes the keyboard away; don't treat that as leaving
    std::string rejected_;          // why the typed formula was refused, if it was

    unsigned int naming_ = 0;  // the name field being edited; 0 if none
    std::string named_;        // its text so far
};

} // namespace clothcad::app
