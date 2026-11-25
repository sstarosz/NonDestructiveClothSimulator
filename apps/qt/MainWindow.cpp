#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include "AttributeEditor.h"
#include "Outliner.h"
#include "ParametersPanel.h"
#include "SpaceView.h"
#include "TableView.h"
#include "Theme.h"
#include "ValueField.h"
#include "clothcad/core/Drawing.h"
#include "clothcad/core/File.h"
#include "clothcad/core/Text.h"
#include "clothcad/session/Samples.h"
#include "clothcad/sim/Body.h"
#include "clothcad/sim/Layout.h"

namespace clothcad::ui {

namespace {

QDockWidget* makeDock(const QString& title, QWidget* content, QMainWindow* window) {
    auto* dock = new QDockWidget(title, window);
    dock->setObjectName(title);
    dock->setWidget(content);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    return dock;
}

// The avatars that come with the program: the build and an installed
// package put the project's assets/ beside it (apps/qt/CMakeLists.txt).
QString assetsFolder() { return QCoreApplication::applicationDirPath() + "/assets"; }

} // namespace

MainWindow::MainWindow(session::Threading threading, QWidget* parent) : QMainWindow(parent), session_(Document{}, threading) {
    setWindowTitle("Cloth CAD");
    resize(1600, 940);
    setDockNestingEnabled(true);

    shelf_ = new Shelf(this);
    // As in Maya: the action row under the menu, the shelf under that, and the
    // toolbox at the left edge, outside the docks.
    shelf_->actionRow()->setWindowTitle("Actions");
    addToolBar(Qt::TopToolBarArea, shelf_->actionRow());
    addToolBarBreak(Qt::TopToolBarArea);
    QToolBar* top = addToolBar("Shelf");
    top->setObjectName("shelf");
    top->setMovable(false);
    top->addWidget(shelf_);
    shelf_->toolbox()->setWindowTitle("Tools");
    addToolBar(Qt::LeftToolBarArea, shelf_->toolbox());

    space_ = new SpaceView(this);
    table_ = new TableView(this);
    views_ = new QSplitter(Qt::Horizontal, this);
    views_->addWidget(space_);
    views_->addWidget(table_);
    views_->setChildrenCollapsible(false);
    views_->setStretchFactor(0, 1);
    views_->setStretchFactor(1, 1);
    views_->setSizes({1, 1});  // each view's own size hint would otherwise decide: half and half
    setCentralWidget(views_);

    outliner_ = new Outliner(this);
    parameters_ = new ParametersPanel(this);
    attributes_ = new AttributeEditor(this);
    QDockWidget* outlinerDock = makeDock("Outliner", outliner_, this);
    QDockWidget* parametersDock = makeDock("Parameters", parameters_, this);
    QDockWidget* attributesDock = makeDock("Attribute Editor", attributes_, this);
    addDockWidget(Qt::LeftDockWidgetArea, outlinerDock);
    splitDockWidget(outlinerDock, parametersDock, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, attributesDock);
    // Docks stacked as tabs show their tabs on top, as Maya's do.
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    resizeDocks({outlinerDock, attributesDock}, {240, 300}, Qt::Horizontal);
    docks_ = {outlinerDock, parametersDock, attributesDock};

    help_ = new QLabel(this);
    frameTime_ = new QLabel(this);
    frameTime_->setStyleSheet(QString("color: %1").arg(theme::quiet.name()));
    statusBar()->addWidget(help_, 1);
    statusBar()->addPermanentWidget(frameTime_);
    statusBar()->setSizeGripEnabled(false);
    say(hintFor(Tool::Select));

    // ---- What the user did, and what it changes ----
    connect(shelf_, &Shelf::toolChosen, this, [this](Tool tool) {
        stopPicking();
        useTool(tool);
        say(QString(nameOf(tool)) + ": " + hintFor(tool));
    });
    connect(shelf_->undo(), &QAction::triggered, this, [this] {
        if (!session_.editor().undo()) return;
        stopPicking();
        drafting_.forget(session_.document());
        table_->cancel();
        refresh();
    });
    connect(shelf_->redo(), &QAction::triggered, this, [this] {
        if (!session_.editor().redo()) return;
        stopPicking();
        drafting_.forget(session_.document());
        table_->cancel();
        refresh();
    });
    connect(shelf_->simulate(), &QAction::toggled, this, [this](bool on) { session_.simulate(on); });
    connect(shelf_->reset(), &QAction::triggered, this, [this] {
        session_.resetCloth();
        shelf_->simulate()->setChecked(false);
    });
    connect(shelf_, &Shelf::layoutChosen, this, &MainWindow::setViewLayout);
    connect(outliner_, &Outliner::panelChosen, this, [this](PanelId panel) { select(panel); });
    connect(outliner_, &Outliner::seamChosen, this, [this](SeamId seam) { selectSeam(seam); });
    connect(table_, &TableView::gestured, this, &MainWindow::on);
    connect(space_, &SpaceView::panelClicked, this, &MainWindow::select);
    connect(space_, &SpaceView::gestured, this, &MainWindow::on);
    connect(space_, &SpaceView::keyed, this, [this](QKeyCombination pressed) {
        if (editInSpace(pressed)) return;
        // Keys typed while the 3D view's own window has the focus do not reach
        // the window's shortcuts by themselves.
        const QKeySequence typed(pressed);
        for (QAction* action : findChildren<QAction*>())
            if (action->isEnabled() && action->shortcuts().contains(typed)) {
                action->trigger();
                return;
            }
    });
    connect(space_, &SpaceView::hinted, this, [this](const QString& next) { say(next); });
    connect(space_, &SpaceView::clothPulled, this, [this](PanelId panel, std::uint32_t point, sim::Vec3 to, session::DragPhase phase) {
        // Pulling the cloth by hand changes nothing in the document: only the drape.
        if (!session_.document().contains(panel)) return;
        if (phase == session::DragPhase::Begin) session_.grab(panel, point, to);
        else if (phase == session::DragPhase::Continue) session_.pullTo(to);
        else session_.letGo();
    });
    connect(table_, &TableView::told, this, [this](const QString& why) { say(why, true); });
    connect(table_, &TableView::hinted, this, [this](const QString& next) { say(next); });
    connect(attributes_, &AttributeEditor::referenceClicked, this, [this](OpId layer, ReferenceId reference) { startPicking(layer, reference); });
    connect(attributes_, &AttributeEditor::operationOffered, this, [this](PanelId panel, session::Offer offer) {
        if (!session_.document().contains(panel)) return;
        // Added as it starts; one that still needs an edge, corner or line shows a problem until it is chosen.
        std::optional<session::Added> added;
        if (!change([&](Document& doc) { added = session::addOffered(doc, panel, offer); }, {.ifItBreaks = session::IfItBreaks::Allow})) return;
        attributes_->openLayer(added->operation);
        if (added->toChoose) startPicking(added->operation, *added->toChoose);
        if (session_.document().operations(panel).size() == 1) table_->fit();  // a new shape: show it
    });
    connect(table_, &TableView::referencePicked, this, [this](const Reference& reference) {
        const std::optional<Picking> was = picking_;
        stopPicking();
        if (was) change([&](Document& doc) { doc.set(was->operation, was->reference, reference); });
    });
    connect(table_, &TableView::choosingDropped, this, [this] {
        stopPicking();
        say({});
    });
    connect(table_, &TableView::pointerAt, this, [this](Vec2 at) {
        frameTime_->setText(QString::fromStdString("x " + formatValue(at.x, Unit::Length) + "   y " + formatValue(at.y, Unit::Length)));
    });

    connect(parameters_, &ParametersPanel::valueTyped, this, [this](Param parameter, QString text) {
        ValueField* field = parameters_->field(parameter);
        if (const std::optional<Expr> value = read(text, session_.document().get(parameter).unit, field))
            change([&](Document& doc) { doc.set(parameter, *value); }, {}, field);
    });
    connect(parameters_, &ParametersPanel::addRequested, this, [this] {
        int n = 1;
        const auto taken = [&](const std::string& name) {
            for (Param p : session_.document().parameters())
                if (session_.document().get(p).name == name) return true;
            return false;
        };
        while (taken("p" + std::to_string(n))) ++n;
        change([&](Document& doc) { doc.add(Parameter{.name = "p" + std::to_string(n), .value = 10_cm}); });
    });

    // Dragging a number: the same changes as typing it, one undo step per drag.
    connect(parameters_, &ParametersPanel::valueDragged, this, [this](Param parameter, double value, session::DragPhase phase) {
        if (session_.document().contains(parameter)) changeByDrag(phase, [&](Document& doc) { doc.set(parameter, Expr(value)); });
    });
    connect(attributes_, &AttributeEditor::fieldDragged, this, [this](OpId layer, FieldId id, double value, session::DragPhase phase) {
        if (session_.document().contains(layer)) changeByDrag(phase, [&](Document& doc) { doc.set(layer, id, Expr(value)); });
    });
    connect(attributes_, &AttributeEditor::spaceDragged, this, [this](PanelId panel, int which, double value, session::DragPhase phase) {
        if (!session_.document().contains(panel)) return;
        Arrangement next = session_.document().get(panel).arrangement;
        next.placed = true;
        double* slot[] = {&next.x, &next.y, &next.z, &next.turn, &next.tilt, &next.roll, &next.wrap};
        *slot[which] = value;
        changeByDrag(phase, [&](Document& doc) { doc.arrange(panel, next); }, {.touches = session::Touches::PlacementOnly});
    });
    connect(attributes_, &AttributeEditor::tableDragged, this, [this](PanelId panel, int which, double value, session::DragPhase phase) {
        if (!session_.document().contains(panel)) return;
        const Panel& p = session_.document().get(panel);
        const double x = which == 0 ? value : p.x, y = which == 1 ? value : p.y;
        changeByDrag(phase, [&](Document& doc) {
            if (which == 2) doc.turn(panel, value);
            else doc.place(panel, x, y);
        }, {.touches = session::Touches::PlacementOnly});
    });
    connect(parameters_, &ParametersPanel::renamed, this, [this](Param parameter, QString name) {
        const std::string wanted = name.toStdString();
        const Document& doc = session_.document();
        if (!doc.contains(parameter) || wanted == doc.get(parameter).name) return;
        QLineEdit* field = parameters_->nameField(parameter);
        const auto refuse = [&](const QString& why) {
            say(why, true);
            if (field) field->setText(QString::fromStdString(doc.get(parameter).name));
        };
        if (!canBeTypedInFormulas(wanted))
            return refuse("'" + name + "' can't be a parameter name. Use letters, digits and underscores, starting with a letter; "
                                       "not pi, min, max, abs, clamp, cm, mm or deg.");
        for (Param other : doc.parameters())
            if (!(other == parameter) && doc.get(other).name == wanted) return refuse("There is already a parameter named '" + name + "'. Choose another name.");
        change([&](Document& d) { d.rename(parameter, wanted); });
    });
    connect(parameters_, &ParametersPanel::removeRequested, this, [this](Param parameter) {
        if (session_.document().contains(parameter)) change([&](Document& doc) { doc.remove(parameter); }, {.ifItBreaks = session::IfItBreaks::Allow});
    });
    connect(outliner_, &Outliner::renamed, this, [this](PanelId panel, QString name) {
        if (!session_.document().contains(panel)) return;
        const std::string wanted = name.trimmed().toStdString();
        if (wanted.empty()) {
            say("A panel needs a name. Type one, or press Escape to keep the old one.", true);
            refresh();  // shows the old name again
        } else if (wanted != session_.document().get(panel).name) {
            change([&](Document& doc) { doc.rename(panel, wanted); });
        }
    });
    connect(attributes_, &AttributeEditor::layerSwitched, this, [this](OpId layer, bool on) {
        change([&](Document& doc) { doc.setEnabled(layer, on); }, {.ifItBreaks = session::IfItBreaks::Allow});
    });
    connect(attributes_, &AttributeEditor::layerOpened, this, [this] { refresh(); });
    // Escape comes back from looking at an earlier step; it is only taken while looking back.
    showAllSteps_ = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    showAllSteps_->setEnabled(false);
    connect(showAllSteps_, &QShortcut::activated, attributes_, &AttributeEditor::showAllSteps);
    connect(attributes_, &AttributeEditor::seamReversed, this, [this](SeamId seam, bool reversed) {
        if (!session_.document().contains(seam)) return;
        Seam changed = session_.document().get(seam);
        changed.reversed = reversed;
        change([&](Document& doc) { doc.set(seam, changed); });
    });
    connect(outliner_, &Outliner::seamDeleteRequested, this, [this](SeamId seam) {
        if (session_.document().contains(seam)) change([&](Document& doc) { doc.remove(seam); }, {.ifItBreaks = session::IfItBreaks::Allow});
    });
    connect(attributes_, &AttributeEditor::panelReferenceChosen, this, [this](OpId layer, ReferenceId reference, PanelId panel) {
        change([&](Document& doc) { doc.set(layer, reference, Reference(panel)); });
    });
    connect(attributes_, &AttributeEditor::cutSideChosen, this, [this](OpId layer, Cut::Side side) {
        change([&](Document& doc) { doc.set(OpRef<Cut>(layer), &Cut::keep, side); });
    });
    connect(attributes_, &AttributeEditor::lengthKeepingChosen, this, [this](OpId layer, SetLength::Keeping keeping) {
        change([&](Document& doc) { doc.set(OpRef<SetLength>(layer), &SetLength::keeping, keeping); });
    });
    connect(attributes_, &AttributeEditor::mirrorLastChanged, this, [this](OpId layer, bool last) {
        change([&](Document& doc) { doc.set(OpRef<Mirror>(layer), &Mirror::appliedLast, last); });
    });
    connect(attributes_, &AttributeEditor::instanceMirroredChanged, this, [this](OpId layer, bool mirrored) {
        change([&](Document& doc) { doc.set(OpRef<Instance>(layer), &Instance::mirrored, mirrored); });
    });
    connect(attributes_, &AttributeEditor::layerMoved, this, [this](OpId layer, std::size_t to) {
        if (!session_.document().contains(layer)) return;
        const std::vector<OpId> made = session_.document().operations(session_.document().panelOf(layer));
        const auto index = static_cast<std::size_t>(std::find(made.begin(), made.end(), layer) - made.begin());
        // The base shape stays first, and nothing goes before it.
        if (index == 0 || to == 0 || to >= made.size() || to == index) return;
        change([&](Document& doc) { doc.move(layer, to); }, {.ifItBreaks = session::IfItBreaks::Allow});
    });
    connect(attributes_, &AttributeEditor::layerRemoved, this, [this](OpId layer) {
        if (!session_.document().contains(layer)) return;
        if (viewUntil_ == layer) viewUntil_.reset();
        change([&](Document& doc) { doc.remove(layer); }, {.ifItBreaks = session::IfItBreaks::Allow});
    });
    connect(attributes_, &AttributeEditor::stepViewed, this, [this](std::optional<OpId> layer) {
        // The last step is the panel as it is: that is no looking back.
        if (layer && session_.document().contains(*layer)) {
            const std::vector<OpId> made = session_.document().operations(session_.document().panelOf(*layer));
            if (!made.empty() && made.back() == *layer) layer.reset();
        }
        viewUntil_ = layer;
        table_->cancel();
        say(layer ? "Showing the panel as it was at this step. Nothing is changed on the table until you show all steps again." : QString());
        refresh();
    });
    connect(attributes_, &AttributeEditor::fieldTyped, this, [this](OpId layer, FieldId id, QString text) { typed(layer, id, text, attributes_->field(layer, id)); });
    connect(table_, &TableView::keepingChosen, this, [this](OpId layer, SetLength::Keeping keeping) {
        if (!session_.document().contains(layer)) return;
        session::ChangeRules rules = lengthRules(session_.document().panelOf(layer));
        if (lengthChanged_) rules.step = session::Step::Continue;  // part of the edit that is open, if it has changed anything yet
        const std::expected<void, std::string> done = session_.editor().change([&](Document& doc) { doc.set(OpRef<SetLength>(layer), &SetLength::keeping, keeping); }, rules);
        if (!done) table_->showLengthProblem(QString::fromStdString(done.error()));
        else lengthChanged_ = true;
        // The anchor shown is the one the document has, whether or not the change was taken.
        const SetLength::Keeping now = std::get<SetLength>(session_.document().get(layer)).keeping;
        table_->showKeeping(now);
        const QString word = table_->anchorWord(now);
        say(now == SetLength::Keeping::Middle ? "Midpoint fixed · both ends move. Enter applies, Esc cancels."
                                              : word + " end fixed · the other end moves. Enter applies, Esc cancels.",
            !done);
        refresh();
    });
    connect(table_, &TableView::lengthEdited, this, [this](OpId layer, FieldId id, QString text) { previewLength(layer, id, text); });
    connect(table_, &TableView::lengthCommitted, this, [this](OpId layer, FieldId id, QString text) {
        previewLength(layer, id, text);  // what is in the field, if it was not shown yet
        lengthEditFrom_.reset();
        lengthChanged_ = false;
        say({});
    });
    connect(table_, &TableView::lengthCancelled, this, [this] { cancelLength(); });
    connect(attributes_, &AttributeEditor::spaceTyped, this, [this](PanelId panel, int which, QString text) {
        ValueField* field = attributes_->spaceField(which);
        const std::optional<Expr> value = read(text, which < 3 ? Unit::Length : Unit::Angle, field);
        if (!value) return;
        // Placement keeps plain numbers: a formula is worked out once.
        const std::optional<double> number = valueOf(*value, session_.editor().shapes());
        if (!number) {
            field->refuse("This works out to no number now.");
            return;
        }
        Arrangement next = session_.document().get(panel).arrangement;
        next.placed = true;
        double* slot[] = {&next.x, &next.y, &next.z, &next.turn, &next.tilt, &next.roll, &next.wrap};
        *slot[which] = *number;
        change([&](Document& doc) { doc.arrange(panel, next); }, {.touches = session::Touches::PlacementOnly}, field);
    });
    connect(attributes_, &AttributeEditor::tableTyped, this, [this](PanelId panel, int which, QString text) {
        ValueField* field = attributes_->tableField(which);
        const std::optional<Expr> value = read(text, which < 2 ? Unit::Length : Unit::Angle, field);
        const std::optional<double> number = value ? valueOf(*value, session_.editor().shapes()) : std::nullopt;
        if (!number) return;
        const Panel& p = session_.document().get(panel);
        const double x = which == 0 ? *number : p.x, y = which == 1 ? *number : p.y;
        change([&](Document& doc) {
            if (which == 2) doc.turn(panel, *number);
            else doc.place(panel, x, y);
        }, {.touches = session::Touches::PlacementOnly}, field);
    });
    connect(outliner_, &Outliner::copyRequested, this, [this](PanelId panel) { on(session::AddBeside{panel, session::Follow::Copy}); });
    // Instances and their seams are made for every selected panel; the rest of the shelf acts on the shown one.
    connect(shelf_, &Shelf::instanceRequested, this, [this] { on(session::DuplicateSelection{session::Follow::Instance}); });
    connect(shelf_, &Shelf::mirroredInstanceRequested, this, [this] { on(session::DuplicateSelection{session::Follow::MirroredInstance}); });
    connect(shelf_, &Shelf::sewAsSourceRequested, this, [this] { on(session::SewAsSource{}); });
    connect(shelf_, &Shelf::bakeRequested, this, [this] {
        const std::optional<PanelId> panel = drafting_.selection.panel;
        if (!panel) return;
        std::size_t baked = 0;
        if (change([&](Document& doc) { baked = bake(doc, *panel); }))
            say(baked == 0 ? "Nothing to bake: the panel has no formulas." : QString("Baked %1 formula%2 into numbers.").arg(baked).arg(baked == 1 ? "" : "s"));
    });
    connect(shelf_, &Shelf::placeFromTableRequested, this, [this] {
        const std::optional<PanelId> selected = drafting_.selection.panel;
        if (!selected) return;
        const PanelId panel = *selected;
        // No place of its own: the editor gives it one from the table again, keeping its anchor.
        change([&](Document& doc) { doc.arrange(panel, Arrangement{.anchor = doc.get(panel).arrangement.anchor}); }, {.touches = session::Touches::PlacementOnly});
    });
    connect(outliner_, &Outliner::baseOnlyChanged, this, [this](PanelId panel, bool baseOnly) {
        if (session_.document().contains(panel)) change([&](Document& doc) { doc.setConstruction(panel, baseOnly); });
    });
    connect(outliner_, &Outliner::deleteRequested, this, [this](PanelId panel) {
        on(session::DeletePanel{panel});
    });

    connect(attributes_, &AttributeEditor::particleDistanceTyped, this, [this](PanelId panel, QString text) {
        if (!session_.document().contains(panel)) return;
        const std::optional<Expr> value = read(text, Unit::Length, attributes_->particleDistance());
        const std::optional<double> number = value ? asNumber(*value) : std::nullopt;
        if (number && *number >= finestParticleDistance && *number <= coarsestParticleDistance)
            change([&](Document& doc) { doc.setParticleDistance(panel, *number); }, {}, attributes_->particleDistance());
        else if (value)
            attributes_->particleDistance()->refuse("Between " + QString::fromStdString(formatValue(finestParticleDistance, Unit::Length)) + " and " +
                                                    QString::fromStdString(formatValue(coarsestParticleDistance, Unit::Length)) + ".");
    });
    connect(attributes_, &AttributeEditor::particleDistanceDragged, this, [this](PanelId panel, double value, session::DragPhase phase) {
        if (!session_.document().contains(panel)) return;
        const double distance = std::clamp(value, finestParticleDistance, coarsestParticleDistance);
        changeByDrag(phase, [&](Document& doc) { doc.setParticleDistance(panel, distance); });
    });
    connect(outliner_, &Outliner::bodyShownChanged, this, &MainWindow::showBody);
    connect(outliner_, &Outliner::panelShownChanged, this, [this](PanelId panel, bool shown) {
        session_.setPanelShown(panel, shown);
        tick();
    });
    connect(outliner_, &Outliner::panelActiveChanged, this, [this](PanelId panel, bool active) {
        session_.setPanelActive(panel, active);
        tick();
    });
    connect(outliner_, &Outliner::seamShownChanged, this, [this](SeamId seam, bool shown) {
        session_.setSeamShown(seam, shown);
        tick();
    });

    buildMenus();
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::tick);
    timer_->start(16);
    refresh();
}

MainWindow::~MainWindow() = default;

namespace {

// A menu item that calls `does`; the same in every Qt 6.
template <class Does>
QAction* item(QMenu* menu, const QString& text, QKeySequence shortcut, QObject* owner, Does does) {
    QAction* action = menu->addAction(text);
    action->setShortcut(shortcut);
    QObject::connect(action, &QAction::triggered, owner, does);
    return action;
}

} // namespace

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu("File");
    item(file, "New", QKeySequence::New, this, [this] { open(Document{}, "untitled"); });
    item(file, "Open…", QKeySequence::Open, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Open", {}, "Patterns (*.clothcad)");
        if (!path.isEmpty()) openFile(path);
    });
    item(file, "Save", QKeySequence::Save, this, [this] {
        const QString path = path_.isEmpty() ? QFileDialog::getSaveFileName(this, "Save", title_ + ".clothcad", "Patterns (*.clothcad)") : path_;
        if (!path.isEmpty()) saveAs(path);
    });
    item(file, "Save as…", QKeySequence::SaveAs, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, "Save as", title_ + ".clothcad", "Patterns (*.clothcad)");
        if (!path.isEmpty()) saveAs(path);
    });
    QMenu* samples = file->addMenu("Samples");
    for (std::size_t i = 0; i < session::samples().size(); ++i)
        item(samples, session::samples()[i].name, {}, this, [this, i] { openSample(i); });
    file->addSeparator();
    item(file, "Quit", QKeySequence::Quit, this, [this] { close(); });

    QMenu* edit = menuBar()->addMenu("Edit");
    edit->addAction(shelf_->undo());
    edit->addAction(shelf_->redo());

    QMenu* body = menuBar()->addMenu("Body");
    item(body, "Avatar from OBJ…", {}, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Avatar", assetsFolder(), "Wavefront OBJ (*.obj)");
        if (!path.isEmpty()) loadAvatar(path);
    });

    QMenu* view = menuBar()->addMenu("View");
    item(view, "Frame everything", {}, this, [this] {
        table_->fit();
        space_->frame();
    });
    view->addSeparator();
    bodyAction_ = view->addAction("Body");
    bodyAction_->setCheckable(true);
    bodyAction_->setChecked(bodyShown_);
    bodyAction_->setToolTip("Hidden, the body is also taken out of the simulation.");
    connect(bodyAction_, &QAction::triggered, this, &MainWindow::showBody);
    QAction* grid = view->addAction("Grid on the table");
    grid->setCheckable(true);
    grid->setChecked(table_->gridShown());
    connect(grid, &QAction::triggered, this, [this](bool on) { table_->setGridShown(on); });
    view->addAction(shelf_->mesh());

    QMenu* windows = menuBar()->addMenu("Windows");
    for (QDockWidget* dock : docks_) windows->addAction(dock->toggleViewAction());
}

void MainWindow::open(Document document, const QString& title) {
    session::layOutOnTheTable(document);
    // A bar for a length of the document that is going has nothing to type into, and the next document may
    // have a layer with the same number.
    table_->closeLength();
    lengthEditFrom_.reset();
    lengthChanged_ = false;
    session_.open(std::move(document));
    shelf_->simulate()->setChecked(false);
    title_ = title;
    path_.clear();
    drafting_ = {};
    stopPicking();
    table_->cancel();
    setWindowTitle(title + "  —  Cloth CAD");
    refresh();
    table_->fit();
    if (session_.document().panels().empty()) say("The table is empty: choose Rectangle on the shelf and drag on the table to draw a panel.");
}

bool MainWindow::openFile(const QString& path) {
    std::variant<Document, FileError> opened = clothcad::open(path.toStdString());
    if (FileError* problem = std::get_if<FileError>(&opened)) {
        say(QString::fromStdString(problem->line ? "Line " + std::to_string(problem->line) + ": " + problem->message : problem->message), true);
        return false;
    }
    open(std::move(std::get<Document>(opened)), QFileInfo(path).completeBaseName());
    path_ = path;
    return true;
}

bool MainWindow::saveAs(const QString& path) {
    const std::string problem = clothcad::save(session_.document(), path.toStdString());
    if (!problem.empty()) {
        say(QString::fromStdString(problem), true);
        return false;
    }
    path_ = path;
    say("Saved.");
    return true;
}

bool MainWindow::loadAvatar(const QString& path) {
    sim::LoadedSurface loaded = sim::loadObj(path.toStdString());
    if (!loaded.problem.empty()) {
        say(QString::fromStdString(loaded.problem) + ". Choose a Wavefront OBJ file in cm, y up, facing +z.", true);
        return false;
    }
    session_.useAvatar(std::move(loaded.surface), path.toStdString());
    refresh();
    return true;
}

void MainWindow::openSample(std::size_t index) {
    const session::Sample& sample = session::samples().at(index);
    open(sample.build(), sample.name);
    // The same avatar again is kept: working out how to collide with it takes a moment.
    const QString avatar = assetsFolder() + "/" + sample.avatar;
    if (session_.avatarPath() != avatar.toStdString()) loadAvatar(avatar);
}

void MainWindow::showBody(bool shown) {
    bodyShown_ = shown;
    session_.setBodyPresent(shown);
    if (bodyAction_) bodyAction_->setChecked(shown);
    outliner_->showBody(bodyNow());
    tick();
}

void MainWindow::setThreading(session::Threading threading) {
    session_.setThreading(threading);
    shelf_->simulate()->setChecked(false);
}

Outliner::Body MainWindow::bodyNow() const {
    const std::string& path = session_.avatarPath();
    return {.name = path.empty() ? QString() : QFileInfo(QString::fromStdString(path)).fileName(), .shown = bodyShown_, .ready = session_.bodyReady()};
}

void MainWindow::startPicking(OpId layer, ReferenceId reference) {
    picking_ = Picking{layer, reference};
    useTool(Tool::Select);
    refresh();
}

void MainWindow::select(std::optional<PanelId> panel) {
    if (panel && !session_.document().contains(*panel)) panel.reset();
    if (panel == drafting_.selection.panel && !drafting_.selection.seam) return;
    drafting_.handle(session_.editor(), session::SelectPanel{panel});
    refresh();
}

void MainWindow::selectSeam(std::optional<SeamId> seam) {
    if (seam && !session_.document().contains(*seam)) seam.reset();
    if (seam == drafting_.selection.seam) return;
    drafting_.handle(session_.editor(), session::SelectSeam{seam});
    refresh();
}

void MainWindow::setTicking(bool running) {
    if (running) timer_->start(16);
    else timer_->stop();
}

void MainWindow::tick() {
    QElapsedTimer took;
    took.start();
    const bool meshShown = shelf_->mesh()->isChecked();
    if (const std::optional<std::string> problem = session_.update(space_->isVisible() || meshShown)) say(QString::fromStdString(*problem), true);
    table_->setMesh(meshShown ? session_.mesh() : nullptr);
    if (session_.editor().version() != shownVersion_) refresh();
    if (space_->isVisible()) space_->show(session_, drafting_.selection.panel, drafting_.tool, meshShown, bodyShown_, drafting_.selection.seam);
    space_->showTime(session_.clothShown() ? std::optional<double>(session_.cloth().time) : std::nullopt, session_.clothShown() && session_.cloth().sewing);
    outliner_->showBody(bodyNow());
    outliner_->showPanelStates(session_.hiddenPanels(), session_.inactivePanels());
    outliner_->showSeamStates(session_.hiddenSeams());
    showMeshCounts();
    (void)took;
}

// What the selected panel's cloth came to. Worked out again only when a new
// mesh was made or another panel was chosen, not every frame.
void MainWindow::showMeshCounts() {
    const std::optional<PanelId> panel = drafting_.selection.panel;
    if (session_.meshesMade() == countedMesh_ && panel == countedPanel_) return;
    countedMesh_ = session_.meshesMade();
    countedPanel_ = panel;
    std::optional<AttributeEditor::MeshCounts> counts;
    const sim::ClothMesh& mesh = *session_.mesh();
    for (std::size_t i = 0; panel && i < mesh.panels.size(); ++i) {
        if (!(mesh.panels[i].panel == *panel)) continue;
        counts = AttributeEditor::MeshCounts{.particles = mesh.panels[i].points.size(), .triangles = mesh.panels[i].triangles.size()};
        for (const sim::Stitch& stitch : mesh.stitches) counts->stitches += stitch.panelA == i || stitch.panelB == i;
    }
    attributes_->showMesh(counts);
}

bool MainWindow::changeByDrag(session::DragPhase phase, const std::function<void(Document&)>& verb, session::ChangeRules rules) {
    if (phase == session::DragPhase::Begin) fieldDragTaken_ = false;
    rules.step = fieldDragTaken_ ? session::Step::Continue : session::Step::New;
    const bool done = change(verb, rules);
    if (done) fieldDragTaken_ = true;
    return done;
}

bool MainWindow::change(const std::function<void(Document&)>& verb, session::ChangeRules rules, ValueField* field) {
    const std::expected<void, std::string> done = session_.editor().change(verb, rules);
    if (!done) {
        const QString why = QString::fromStdString(done.error());
        say("Not changed: " + why, true);
        if (field) field->refuse(why);
        refresh();
        return false;
    }
    say({});
    refresh();
    return true;
}

void MainWindow::typed(OpId layer, FieldId id, const QString& text, ValueField* field) {
    if (!session_.document().contains(layer)) return;
    const Unit unit = fields(session_.document().get(layer))[id.index].unit;
    if (const std::optional<Expr> value = read(text, unit, field)) change([&](Document& doc) { doc.set(layer, id, *value); }, {}, field);
}

std::optional<Expr> MainWindow::read(const QString& text, Unit unit, ValueField* field) {
    const std::variant<Expr, TextError> value = parseValue(text.toStdString(), unit, session_.document());
    if (const TextError* problem = std::get_if<TextError>(&value)) {
        const QString why = QString::fromStdString(problem->message);
        say(why, true);
        if (field) field->refuse(why);
        return std::nullopt;
    }
    return std::get<Expr>(value);
}

void MainWindow::refresh() {
    const Document& doc = session_.document();
    if (!gesturing_) closeLengthIfGone();  // (a gesture that took the length away opens the bar again on another)
    const Result& shapes = session_.editor().shapes();
    std::optional<PanelId>& selected = drafting_.selection.panel;
    if (selected && !doc.contains(*selected)) drafting_.forget(doc);
    std::optional<SeamId>& seam = drafting_.selection.seam;
    if (seam && !doc.contains(*seam)) seam.reset();
    outliner_->show(doc, shapes, selected, bodyNow(), seam);
    for (QAction* onPanel : {shelf_->instance(), shelf_->mirroredInstance(), shelf_->bake(), shelf_->placeFromTable()}) onPanel->setEnabled(selected.has_value());
    const std::vector<PanelId> chosen = drafting_.selection.panels();
    shelf_->sewAsSource()->setEnabled(std::any_of(chosen.begin(), chosen.end(), [&](PanelId panel) { return doc.contains(panel) && doc.instanceOf(panel); }));
    parameters_->show(doc, shapes);
    // Looking back only at a step of the selected panel.
    if (viewUntil_ && (!selected || !doc.contains(*viewUntil_) || !(doc.panelOf(*viewUntil_) == *selected))) viewUntil_.reset();
    showAllSteps_->setEnabled(viewUntil_.has_value());
    if (viewUntil_) past_ = evaluateUpTo(doc, *viewUntil_);
    attributes_->show(doc, shapes, selected, viewUntil_);
    table_->show(doc, viewUntil_ ? past_ : shapes, selected, drafting_.selection.corners, drafting_.tool, drafting_.selection.others, seam);
    if (space_->isVisible()) space_->show(session_, selected, drafting_.tool, shelf_->mesh()->isChecked(), bodyShown_, seam);
    // A layer's reference being chosen: offer what that layer could use.
    if (picking_ && (!doc.contains(picking_->operation))) picking_.reset();
    std::optional<TableView::Choice> choice;
    if (picking_) {
        const std::vector<ReferenceField> uses = references(doc.get(picking_->operation));
        if (picking_->reference.index < uses.size() && !std::holds_alternative<PanelId>(uses[picking_->reference.index].value)) {
            const Reference& slot = uses[picking_->reference.index].value;
            const auto kind = std::holds_alternative<EdgeRef>(slot)     ? TableView::Choice::Kind::Edge
                              : std::holds_alternative<CornerRef>(slot) ? TableView::Choice::Kind::Corner
                                                                        : TableView::Choice::Kind::Line;
            choice = TableView::Choice{doc.panelOf(picking_->operation), kind, outlineBefore(doc, picking_->operation)};
            const std::size_t offered = kind == TableView::Choice::Kind::Edge     ? choice->candidates.edges.size()
                                        : kind == TableView::Choice::Kind::Corner ? choice->candidates.vertices.size()
                                                                                  : choice->candidates.lines.size();
            const char* what = kind == TableView::Choice::Kind::Edge ? "an edge" : kind == TableView::Choice::Kind::Corner ? "a corner" : "a line";
            if (offered == 0)
                say(kind == TableView::Choice::Kind::Line ? "This panel has no line to choose yet. Draw one with the Line tool first. Esc cancels."
                                                          : "There is nothing to choose on this panel at this point. Esc cancels.",
                    true);
            else
                say(QString("Click ") + what + " on the highlighted panel. Esc cancels.");
        } else {
            picking_.reset();
        }
    }
    table_->choose(choice);
    if (choice) table_->setFocus();  // so Esc reaches it
    shelf_->undo()->setEnabled(session_.editor().canUndo());
    shelf_->redo()->setEnabled(session_.editor().canRedo());
    shownVersion_ = session_.editor().version();
}

void MainWindow::previewLength(OpId layer, FieldId id, const QString& text) {
    if (!session_.document().contains(layer)) return;
    if (text == lengthText_) {  // back to what is applied: whatever was said against what was typed since is over
        table_->showLengthProblem({});
        return;
    }
    const Unit unit = fields(session_.document().get(layer))[id.index].unit;
    const std::variant<Expr, TextError> parsed = parseValue(text.toStdString(), unit, session_.document());
    if (const TextError* problem = std::get_if<TextError>(&parsed)) {
        table_->showLengthProblem(QString::fromStdString(problem->message));
        return;
    }
    session::ChangeRules rules = lengthRules(session_.document().panelOf(layer));
    if (lengthChanged_) rules.step = session::Step::Continue;
    const Expr value = std::get<Expr>(parsed);
    const std::expected<void, std::string> done = session_.editor().change([&](Document& doc) { doc.set(layer, id, value); }, rules);
    if (!done) {
        table_->showLengthProblem(QString::fromStdString(done.error()));  // the document is as it was
        return;
    }
    table_->showLengthProblem({});
    lengthText_ = text;
    lengthChanged_ = true;
    refresh();
}

// A length typed on the table is an edit by hand, like a dragged corner: the panel keeps its place on
// the table, so the end that is kept stays where it is on the screen and only the rest of the edge
// moves. (Left to itself the editor keeps the middle of the panel's box where it was, and the whole
// drawing slides as the edge grows.) A panel that is sewn is another matter: it grows away from its
// seams, on the table and in space, as it does for a formula or a field.
session::ChangeRules MainWindow::lengthRules(PanelId panel) const {
    const Document& doc = session_.document();
    for (SeamId id : doc.seams()) {
        const Seam& seam = doc.get(id);
        if (seam.a.panel == panel || seam.b.panel == panel) return {};
    }
    return {.grows = session::Grows::WhereDragged};
}

void MainWindow::closeLengthIfGone() {
    const auto& typing = table_->lengthBeingTyped();
    if (!typing || session_.document().contains(typing->operation)) return;
    table_->closeLength();
    lengthEditFrom_.reset();
    lengthChanged_ = false;
}

void MainWindow::cancelLength() {
    // All of the edit goes, and is forgotten: it must not be left to redo.
    if (lengthEditFrom_) session_.editor().rollBackTo(*lengthEditFrom_);
    lengthEditFrom_.reset();
    lengthChanged_ = false;
    // A length moves corners and makes none, so the corners that are chosen stay chosen: the click that
    // left the bar may be the one that adds to them.
    const std::vector<CornerRef> corners = drafting_.selection.corners;
    drafting_.forget(session_.document());
    drafting_.selection.corners = corners;
    table_->cancel();
    say({});
    refresh();
}

QString MainWindow::lengthStatus(const session::Outcome::LengthField& what) const {
    if (!what.keeping) return QString::fromStdString("Type the " + what.label + ". Enter applies, Esc cancels.");
    const QString word = table_->anchorWord(*what.keeping);
    return (*what.keeping == SetLength::Keeping::Middle ? QString("Midpoint fixed · both ends move") : word + " end fixed · the other end moves") + ". Click another anchor to change it. Enter applies, Esc cancels.";
}

void MainWindow::on(const session::Gesture& gesture) {
    // Looking back at an earlier step, the pattern does not change; placing panels in space still does.
    const bool placesInSpace = std::holds_alternative<session::MoveInSpace>(gesture) || std::holds_alternative<session::TurnInSpace>(gesture);
    const bool changesNothing = std::holds_alternative<session::SelectPanel>(gesture) || std::holds_alternative<session::SelectPanels>(gesture) ||
                                std::holds_alternative<session::CopySelection>(gesture);
    if (viewUntil_ && !changesNothing && !placesInSpace) {
        table_->cancel();
        say("Showing an earlier step: press Escape to show all steps and change the pattern again.", true);
        return;
    }
    struct Running {
        bool& flag;
        explicit Running(bool& f) : flag(f) { flag = true; }
        ~Running() { flag = false; }
    } running(gesturing_);
    // A length is typed in one go: Escape goes back to how it was before the gesture that opened the bar.
    const bool lengthGesture = std::holds_alternative<session::EditLength>(gesture) || std::holds_alternative<session::UseShapeLength>(gesture);
    if (lengthGesture && !lengthEditFrom_) {
        lengthEditFrom_ = session_.editor().step();
        lengthChanged_ = false;
    }
    const session::Outcome done = drafting_.handle(session_.editor(), gesture);
    if (!done.notice.empty()) say(QString::fromStdString(done.notice), done.refused);
    else if (std::holds_alternative<session::MoveCorner>(gesture) || std::holds_alternative<session::MovePanel>(gesture)) say({});
    shelf_->setTool(drafting_.tool);  // a drawing tool that has done its work is back to Select
    refresh();
    if (done.typeLength) {
        // The length is a field of a layer, but it is typed on the table, where the edge is: the
        // Attribute Editor is the back end of this, not the way to work.
        const session::Outcome::LengthField& wanted = *done.typeLength;
        const Field field = fields(session_.document().get(wanted.operation))[wanted.field.index];
        const std::optional<double> number = asNumber(field.value);
        const std::string now = number ? formatValue(*number, field.unit) : "= " + formatExpr(field.value, session_.document());
        lengthChanged_ = lengthChanged_ || (lengthEditFrom_ && session_.editor().step() != *lengthEditFrom_);
        lengthText_ = QString::fromStdString(now);
        table_->typeLength(wanted, lengthText_);
        if (done.notice.empty()) say(lengthStatus(wanted));  // a refusal's words stay
    } else if (lengthGesture && !table_->lengthEditor()) {
        lengthEditFrom_.reset();  // nothing to type: the gesture was refused, or has nothing to say
        lengthChanged_ = false;
    }
    gesturing_ = false;
    closeLengthIfGone();  // a bar left for a length that this gesture took away
}

// Delete, Ctrl+C, Ctrl+V and Ctrl+D typed in the 3D view do what they do on the
// table, to the selected panels. The 3D view shows no corners, so Delete takes
// whole panels; Paste has no place on the table to use, so it goes beside the originals.
bool MainWindow::editInSpace(QKeyCombination pressed) {
    const QKeySequence typed(pressed);
    if (pressed == QKeyCombination(Qt::Key_Delete) || pressed == QKeyCombination(Qt::Key_Backspace)) {
        on(session::DeleteSelection{.wholePanels = true});  // says so when nothing is selected
    } else if (typed == QKeySequence(QKeySequence::Copy)) {
        on(session::CopySelection{});
    } else if (typed == QKeySequence(QKeySequence::Paste)) {
        on(session::PastePanel{std::nullopt});
    } else if (pressed == QKeyCombination(Qt::ControlModifier, Qt::Key_D)) {
        on(session::DuplicateSelection{});
    } else {
        return false;
    }
    return true;
}

void MainWindow::useTool(Tool tool) {
    drafting_.tool = tool;
    shelf_->setTool(tool);
    refresh();
}

void MainWindow::stopPicking() {
    if (!picking_) return;
    picking_.reset();
    table_->choose(std::nullopt);
}

void MainWindow::say(const QString& words, bool problem) {
    help_->setText(words);
    help_->setStyleSheet(QString("color: %1").arg((problem ? theme::error : theme::text).name()));
}

QString MainWindow::helpLine() const { return help_->text(); }

QDockWidget* MainWindow::dock(const QString& name) const {
    for (QDockWidget* d : docks_)
        if (d->windowTitle() == name) return d;
    return nullptr;
}

void MainWindow::setViewLayout(ViewLayout layout) {
    layout_ = layout;
    shelf_->layout(layout)->setChecked(true);
    views_->setOrientation(layout == ViewLayout::Stacked ? Qt::Vertical : Qt::Horizontal);
    space_->setVisible(layout != ViewLayout::TableOnly);
    table_->setVisible(layout != ViewLayout::SpaceOnly);
    if (layout == ViewLayout::SideBySide || layout == ViewLayout::Stacked) views_->setSizes({1, 1});  // shared in half again
}

} // namespace clothcad::ui
