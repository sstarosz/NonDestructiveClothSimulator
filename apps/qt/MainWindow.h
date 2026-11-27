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

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "Outliner.h"
#include "Shelf.h"
#include "twintailor/session/Autosave.h"
#include "twintailor/session/Drafting.h"
#include "twintailor/session/FrameTimes.h"
#include "twintailor/session/Session.h"

class QDockWidget;
class QLabel;
class QLockFile;
class QProgressBar;
class QShortcut;
class QSplitter;
class QTimer;

namespace twintailor::ui {

class AttributeEditor;
class AvatarDialog;
class ParametersPanel;
class SpaceView;
class TableView;
class ValueField;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(session::Threading threading = session::Threading::OwnThread, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Another document in the window, at once, with its scene as `state` leaves
    // it: the File menu asks first about unsaved changes, these do not. It
    // starts saved.
    void open(Document document, const QString& title, SceneState state = {});
    // A sample, on the body it is draped on (its avatar from assets/).
    void openSample(std::size_t index);
    // What the File and Body menus do once a file is chosen. Each says why in
    // the help line when it cannot, and then changes nothing. An avatar goes
    // into the document, as one undo step.
    bool openFile(const QString& path);
    bool saveAs(const QString& path);
    // Writes the garment to an OBJ file (Session::obj) and says in the help
    // line how many panels went where.
    bool exportObj(const QString& path);
    // An avatar from an OBJ file, read as `readAs` says; by default the file
    // is taken to be as the document is: cm, y up, facing +z.
    bool loadAvatar(const QString& path, const AvatarImport& readAs = {});
    // What Body > Avatar from OBJ... does with the chosen file: opens the
    // AvatarDialog ("avatarImport") with a guess of its unit and axes, and
    // shows the body in 3D as it is chosen. Meanwhile only the dialog and the
    // 3D view's camera take the hand. OK puts the avatar on, standing on the
    // floor; Cancel changes nothing.
    void chooseAvatar(const QString& path);
    // From now on unsaved work is copied now and then into a folder of its own
    // under `folder`, and a copy that a run which stopped left there is
    // offered at once. Closing the window takes the copy away; the program
    // stopping otherwise leaves it for the next start.
    void keepRecoveryIn(const QString& folder, double everySeconds = 30.0);
    // What the program remembers between runs (the folders of the last pattern,
    // avatar and export, where the next file dialog of each starts, and View >
    // Instances in violet) is read from this file now and written to it from now
    // on. Without this it is remembered only while the window is there.
    void keepSettingsIn(const QString& file);
    // Until a copy that is being written is on disk, for tests.
    void finishRecoveryCopy();
    // An error that nothing else handled: the window cannot be trusted to go
    // on. Unsaved work is copied at once, as it is, and the window stops
    // working; then a message ("cannotGoOn") says what happened and where the
    // work is. Closing it emits mustEnd(), and whoever made the window ends
    // the program. Only the first error is told.
    void cannotGoOn(const QString& error);
    void select(std::optional<PanelId> panel);
    std::optional<PanelId> selected() const { return drafting_.selection.panel; }
    // A seam chosen instead of panels, and the one chosen now.
    void selectSeam(std::optional<SeamId> seam);
    std::optional<SeamId> selectedSeam() const { return drafting_.selection.seam; }
    // Every selected panel, the one the docks show first.
    std::vector<PanelId> selectedPanels() const { return drafting_.selection.panels(); }
    std::optional<OpId> viewedStep() const { return viewUntil_; }
    // The body shown in 3D or hidden; hidden, it is also out of the simulation.
    void showBody(bool shown);
    bool bodyShown() const { return session_.bodyPresent(); }
    void showBodySeeThrough(bool seeThrough);  // the body drawn so that cloth inside it shows
    void markInstances(bool on);               // instances drawn violet in 3D; remembered by the program
    bool bodySeeThrough() const { return session_.bodySeeThrough(); }
    const std::vector<CornerRef>& selectedCorners() const { return drafting_.selection.corners; }

    // One frame: the session catches up and everything shown follows.
    // A timer calls it once for each picture the screen shows (every 4 ms at
    // 240 Hz, 16 ms at 60 Hz); tests call it themselves.
    void tick();
    // Stops or starts that timer: a benchmark that counts frames runs them itself.
    void setTicking(bool running);
    // How often that timer calls it, for tests.
    int tickMilliseconds() const;
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

signals:
    // After cannotGoOn(), once its message has been read.
    void mustEnd();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // Saves to the document's file, asking for one if it has none yet. False if
    // that was cancelled or the file could not be written.
    bool save();
    // Whether the document may be left behind, `before` doing something: yes if
    // it is saved; otherwise the user is asked to save it, drop it or cancel.
    bool mayDiscard(const QString& before);
    // The document's name in the title, with "*" while it has unsaved changes.
    void showTitle(const QString& title);
    void showUnsaved();
    // A field being typed in takes what is typed, as Enter does; false if that was refused.
    bool takeWhatIsTyped();
    void offerRecovery(const QString& folder);
    void endRecovery();  // no copy, and no more of them: the window is closed
    // Writes a recovery copy of unsaved work now; how that went, in words.
    QString copyBeforeEnding();
    // The kinds of file the window asks for, each with a folder of its own.
    enum class Files { Patterns, Avatars, Exports };
    // Where a dialog for such files starts: the folder of the last one, while
    // it is there. Else the program's own avatars, the patterns' folder for
    // exports, and for patterns wherever the system starts (empty).
    QString folderFor(Files files) const;
    void rememberFolder(Files files, const QString& file);
    // The status line says it before the window stops to read or write a file.
    void sayAtOnce(const QString& words);
    // The status line's bar while the body is being made ready for the cloth
    // (Session::bodyProgress, 0 to 1); none: no bar.
    void showPreparing(std::optional<double> done);
    // An avatar into the document, as one undo step; nothing if it is the one worn.
    bool wear(std::shared_ptr<const Avatar> avatar);
    // While an avatar is chosen: the dialog, what was switched off so that
    // nothing else takes the hand, and what to put back.
    struct ChoosingAvatar {
        AvatarDialog* dialog = nullptr;
        std::vector<QPointer<QWidget>> widgets;
        std::vector<QPointer<QAction>> actions;
        bool playing = false;
        ViewLayout layout = ViewLayout::SideBySide;
    };
    ChoosingAvatar holdTheRest();
    void endChoosing();     // the preview goes, and the rest is as it was
    void stopChoosing();    // as Cancel, if an avatar is being chosen
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
    session::ChangeRules lengthRules(PanelId panel) const;  // how a length typed on the table is changed
    void closeLengthIfGone();                      // the bar's length was undone or removed: nothing to type into
    QString lengthText_;                           // the text that is applied now
    // What was typed for a field of a layer, in the Attribute Editor (`field` shows a refusal) or on the table.
    void typed(OpId layer, FieldId id, const QString& text, ValueField* field);
    void say(const QString& words, bool problem = false);
    void setViewLayout(ViewLayout layout);
    void buildMenus();
    void showSpace();  // the 3D view as the window has it now, if it is seen
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
    QLabel* pointerAt_;  // where the pointer is on the table
    QProgressBar* preparing_;
    QTimer* timer_;
    void paceTicks();  // the timer follows the screen's rate
    // View > Frame times: what the frames take in this window, on this screen.
    session::FrameTimes frameTimes_;
    QLabel* frameTimesLine_;
    QElapsedTimer clock_;  // since the window was made: when each picture was shown
    std::optional<double> frameTimesShownAt_;
    void showFrameTimes();  // the line in the status bar, four times a second while it is shown
    std::vector<QDockWidget*> docks_;

    std::array<QString, 3> folders_;  // of the last file of each kind, in Files' order
    QString settingsFile_;            // where they are kept between runs; empty: nowhere
    session::Drafting drafting_;  // the tool, the selection, and the drags in progress
    std::optional<ChoosingAvatar> choosing_;
    bool fieldDragTaken_ = false;  // the field being dragged has changed the document
    std::shared_ptr<const Avatar> shownAvatar_;  // the document's, as last shown
    QAction* bodyAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* seeThroughAction_ = nullptr;
    QAction* markInstancesAction_ = nullptr;  // checked: instances are violet in 3D
    std::uint64_t problemsSaid_ = 0;  // how many times the help line has said that something cannot be
    // Recovery copies: the folder this run keeps them in, held by a lock while it runs.
    QString recoveryFolder_;
    std::unique_ptr<QLockFile> recoveryLock_;
    std::unique_ptr<session::Autosave> autosave_;
    bool failed_ = false;  // cannotGoOn() has been called: nothing more is done
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

} // namespace twintailor::ui
