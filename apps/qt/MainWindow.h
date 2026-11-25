#pragma once
// The Qt window of 0.1.1, laid out as Maya is: the action row (undo, play,
// reset) and the shelf across the top, the toolbox (Select, Move, Rotate,
// Scale, the view layouts) at the left edge, docks that fold, float and stack
// as tabs (Outliner and Parameters on the left; Attribute Editor on the
// right), the 3D view on the left of the middle and the table on the right,
// and a help line at the bottom.
//
// It owns the session. The docks and views only report what the user did;
// what that changes in the document is decided here, through the editor,
// so that every change can be undone and one that would break the pattern
// is refused, with why in the help line.
//
//   MainWindow window;
//   window.openSample(5);
//   window.show();

#include <QMainWindow>
#include <optional>

#include "Outliner.h"
#include "Shelf.h"
#include "clothcad/session/Drafting.h"
#include "clothcad/session/Session.h"

class QDockWidget;
class QLabel;
class QShortcut;
class QSplitter;
class QTimer;

namespace clothcad::ui {

class AttributeEditor;
class ParametersPanel;
class SpaceView;
class TableView;
class ValueField;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(session::Threading threading = session::Threading::OwnThread, QWidget* parent = nullptr);
    ~MainWindow() override;

    void open(Document document, const QString& title);
    // A sample, on the body it is draped on (its avatar from assets/).
    void openSample(std::size_t index);
    // What the File and Body menus do once a file is chosen. Each says why in
    // the help line when it cannot, and then changes nothing.
    bool openFile(const QString& path);
    bool saveAs(const QString& path);
    bool loadAvatar(const QString& path);
    void select(std::optional<PanelId> panel);
    std::optional<PanelId> selected() const { return drafting_.selection.panel; }
    // Every selected panel, the one the docks show first.
    std::vector<PanelId> selectedPanels() const { return drafting_.selection.panels(); }
    std::optional<OpId> viewedStep() const { return viewUntil_; }
    // The body shown in 3D or hidden; hidden, it is also out of the simulation.
    void showBody(bool shown);
    bool bodyShown() const { return bodyShown_; }
    const std::vector<CornerRef>& selectedCorners() const { return drafting_.selection.corners; }

    // One frame: the session catches up and everything shown follows.
    // A timer calls it sixty times a second; tests call it themselves.
    void tick();
    // Stops or starts that timer: a benchmark that counts frames runs them itself.
    void setTicking(bool running);
    // The cloth on its own thread (the default) or stepped by the frames, as tests and benchmarks need.
    void setThreading(session::Threading threading);

    const session::Session& session() const { return session_; }
    // The parts, for tests.
    Shelf* shelf() const { return shelf_; }
    Outliner* outliner() const { return outliner_; }
    ParametersPanel* parameters() const { return parameters_; }
    AttributeEditor* attributes() const { return attributes_; }
    TableView* table() const { return table_; }
    SpaceView* space() const { return space_; }
    QDockWidget* dock(const QString& name) const;
    QString helpLine() const;
    ViewLayout viewLayout() const { return layout_; }

private:
    // Makes a change as one undo step; on refusal says why, in `field` too if given.
    bool change(const std::function<void(Document&)>& verb, session::ChangeRules rules = {}, ValueField* field = nullptr);
    // A change from one step of dragging a field: the whole drag is one undo
    // step, begun by the first of its changes that was taken.
    bool changeByDrag(session::DragPhase phase, const std::function<void(Document&)>& verb, session::ChangeRules rules = {});
    // What was typed into a field that measures `unit`; empty, with the field marked, if it does not read.
    std::optional<Expr> read(const QString& text, Unit unit, ValueField* field);
    void refresh();
    // A length typed on the table, as it is typed: shown at once, in one undo step with whatever the
    // gesture that opened it did, so that Escape takes all of it back and Enter keeps it.
    void previewLength(OpId layer, FieldId id, const QString& text);
    void cancelLength();
    // What the bar says is going to happen to the ends of the edge.
    QString lengthStatus(const session::Outcome::LengthField& what) const;
    std::optional<std::uint64_t> lengthEditFrom_;  // the history state before the gesture that opened the bar
    bool lengthChanged_ = false;                   // the document has been changed since then
    bool gesturing_ = false;                       // a gesture is being handled: the bar may be reopened on something else
    session::ChangeRules lengthRules() const;      // how a length typed on the table is changed
    void closeLengthIfGone();                      // the bar's length was undone or removed: nothing to type into
    QString lengthText_;                           // the text that is applied now
    // What was typed for a field of a layer, in the Attribute Editor (`field` shows a refusal) or on the table.
    void typed(OpId layer, FieldId id, const QString& text, ValueField* field);
    void say(const QString& words, bool problem = false);
    void setViewLayout(ViewLayout layout);
    void buildMenus();
    Outliner::Body bodyNow() const;
    void showMeshCounts();
    // What a gesture on the table changes: decided by the rules both windows share.
    void on(const session::Gesture& gesture);
    // Delete, copy, paste and duplicate typed in the 3D view; false for any other key.
    bool editInSpace(QKeyCombination pressed);
    // The tool in use: kept by Drafting, shown by the shelf and both views.
    void useTool(Tool tool);

    session::Session session_;
    Shelf* shelf_;
    Outliner* outliner_;
    QShortcut* showAllSteps_;  // Escape, while an earlier step is shown
    ParametersPanel* parameters_;
    AttributeEditor* attributes_;
    TableView* table_;
    SpaceView* space_;
    QSplitter* views_;
    QLabel* help_;
    QLabel* frameTime_;
    QTimer* timer_;
    std::vector<QDockWidget*> docks_;

    session::Drafting drafting_;  // the tool, the selection, and the drags in progress
    bool fieldDragTaken_ = false;  // the field being dragged has changed the document
    bool bodyShown_ = true;        // hidden, the body is also out of the simulation
    QAction* bodyAction_ = nullptr;
    // Looking back: the selected panel shown on the table as of this step, and
    // what it was then. Nothing is reshaped on the table meanwhile.
    std::optional<OpId> viewUntil_;
    Result past_;  // where the panel carried in space was when the drag began
    // The layer reference being chosen by a click on the table, if one is.
    struct Picking {
        OpId operation;
        ReferenceId reference;
    };
    std::optional<Picking> picking_;
    void stopPicking();
    // A layer's edge, corner or line is chosen next by a click on the table.
    void startPicking(OpId layer, ReferenceId reference);
    std::uint64_t shownVersion_ = 0;
    std::uint64_t countedMesh_ = 0;  // the mesh the attribute editor's counts are of, and whose panel
    std::optional<PanelId> countedPanel_;
    ViewLayout layout_ = ViewLayout::SideBySide;
    QString path_;
    QString title_;
};

} // namespace clothcad::ui
