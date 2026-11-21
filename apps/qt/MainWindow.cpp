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
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include "AttributeEditor.h"
#include "ClothPanel.h"
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

// "front copy", or "front copy 2" if that is taken.
std::string firstFreeName(const Document& doc, const std::string& wanted) {
    const auto taken = [&](const std::string& name) {
        for (PanelId panel : doc.panels())
            if (doc.get(panel).name == name) return true;
        return false;
    };
    if (!taken(wanted)) return wanted;
    for (int n = 2;; ++n)
        if (!taken(wanted + " " + std::to_string(n))) return wanted + " " + std::to_string(n);
}

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
    QToolBar* top = addToolBar("Shelf");
    top->setObjectName("shelf");
    top->setMovable(false);
    top->addWidget(shelf_);

    space_ = new SpaceView(this);
    table_ = new TableView(this);
    views_ = new QSplitter(Qt::Horizontal, this);
    views_->addWidget(space_);
    views_->addWidget(table_);
    views_->setChildrenCollapsible(false);
    setCentralWidget(views_);
    lastView_ = table_;

    outliner_ = new Outliner(this);
    parameters_ = new ParametersPanel(this);
    attributes_ = new AttributeEditor(this);
    cloth_ = new ClothPanel(this);
    QDockWidget* outlinerDock = makeDock("Outliner", outliner_, this);
    QDockWidget* parametersDock = makeDock("Parameters", parameters_, this);
    QDockWidget* attributesDock = makeDock("Attribute Editor", attributes_, this);
    QDockWidget* clothDock = makeDock("Cloth", cloth_, this);
    addDockWidget(Qt::LeftDockWidgetArea, outlinerDock);
    splitDockWidget(outlinerDock, parametersDock, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, attributesDock);
    tabifyDockWidget(attributesDock, clothDock);
    attributesDock->raise();
    resizeDocks({outlinerDock, attributesDock}, {240, 300}, Qt::Horizontal);
    docks_ = {outlinerDock, parametersDock, attributesDock, clothDock};

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
        table_->setTool(tool);
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
    connect(table_, &TableView::gestured, this, [this](const session::Gesture& gesture) {
        lastView_ = table_;
        on(gesture);
    });
    connect(space_, &SpaceView::panelClicked, this, [this](std::optional<PanelId> panel) {
        lastView_ = space_;
        select(panel);
    });
    connect(space_, &SpaceView::panelCarried, this, [this](PanelId panel, sim::Vec3 by, session::DragPhase phase) {
        // Carrying a panel through space only places it: its shape stays,
        // and a draped panel takes its drape along. One drag is one undo step.
        lastView_ = space_;
        if (!session_.document().contains(panel)) return;
        if (phase == session::DragPhase::Begin) {
            carriedFrom_.reset();
            if (session_.mesh())
                if (const sim::PanelMesh* mesh = session_.mesh()->find(panel)) carriedFrom_ = sim::arrangementOf(session_.document(), *mesh);
        }
        if (!carriedFrom_) return;
        Arrangement now = *carriedFrom_;
        now.placed = true;
        now.x += by.x;
        now.y += by.y;
        now.z += by.z;
        change([&](Document& doc) { doc.arrange(panel, now); },
               {.step = phase == session::DragPhase::Begin ? session::Step::New : session::Step::Continue, .touches = session::Touches::PlacementOnly});
        if (phase == session::DragPhase::End) carriedFrom_.reset();
    });
    connect(space_, &SpaceView::hinted, this, [this](const QString& next) { say(next); });
    connect(space_, &SpaceView::clothPulled, this, [this](PanelId panel, std::uint32_t point, sim::Vec3 to, session::DragPhase phase) {
        // Pulling the cloth by hand changes nothing in the document: only the drape.
        lastView_ = space_;
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
    connect(attributes_, &AttributeEditor::renamed, this, [this](PanelId panel, QString name) {
        if (!name.trimmed().isEmpty()) change([&](Document& doc) { doc.rename(panel, name.trimmed().toStdString()); });
    });
    connect(attributes_, &AttributeEditor::layerSwitched, this, [this](OpId layer, bool on) {
        change([&](Document& doc) { doc.setEnabled(layer, on); }, {.ifItBreaks = session::IfItBreaks::Allow});
    });
    connect(attributes_, &AttributeEditor::layerOpened, this, [this] { refresh(); });
    connect(attributes_, &AttributeEditor::seamReversed, this, [this](SeamId seam, bool reversed) {
        if (!session_.document().contains(seam)) return;
        Seam changed = session_.document().get(seam);
        changed.reversed = reversed;
        change([&](Document& doc) { doc.set(seam, changed); });
    });
    connect(attributes_, &AttributeEditor::seamRemoved, this, [this](SeamId seam) {
        if (session_.document().contains(seam)) change([&](Document& doc) { doc.remove(seam); }, {.ifItBreaks = session::IfItBreaks::Allow});
    });
    connect(attributes_, &AttributeEditor::panelReferenceChosen, this, [this](OpId layer, ReferenceId reference, PanelId panel) {
        change([&](Document& doc) { doc.set(layer, reference, Reference(panel)); });
    });
    connect(attributes_, &AttributeEditor::cutSideChosen, this, [this](OpId layer, Cut::Side side) {
        change([&](Document& doc) { doc.set(OpRef<Cut>(layer), &Cut::keep, side); });
    });
    connect(attributes_, &AttributeEditor::mirrorLastChanged, this, [this](OpId layer, bool last) {
        change([&](Document& doc) { doc.set(OpRef<Mirror>(layer), &Mirror::appliedLast, last); });
    });
    connect(attributes_, &AttributeEditor::instanceMirroredChanged, this, [this](OpId layer, bool mirrored) {
        change([&](Document& doc) { doc.set(OpRef<Instance>(layer), &Instance::mirrored, mirrored); });
    });
    connect(attributes_, &AttributeEditor::layerMoved, this, [this](OpId layer, bool later) {
        if (!session_.document().contains(layer)) return;
        const std::vector<OpId> made = session_.document().operations(session_.document().panelOf(layer));
        const auto index = static_cast<std::size_t>(std::find(made.begin(), made.end(), layer) - made.begin());
        // The base shape stays first.
        if (later ? index + 1 >= made.size() : index <= 1) return;
        const std::size_t to = later ? index + 1 : index - 1;
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
    connect(attributes_, &AttributeEditor::fieldTyped, this, [this](OpId layer, FieldId id, QString text) {
        ValueField* field = attributes_->field(id);
        const Unit unit = fields(session_.document().get(layer))[id.index].unit;
        if (const std::optional<Expr> value = read(text, unit, field)) change([&](Document& doc) { doc.set(layer, id, *value); }, {}, field);
    });
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
    connect(attributes_, &AttributeEditor::copyRequested, this, [this](PanelId panel) { addBeside(panel, Follow::Copy); });
    connect(attributes_, &AttributeEditor::instanceRequested, this, [this](PanelId panel) { addBeside(panel, Follow::Instance); });
    connect(attributes_, &AttributeEditor::mirroredInstanceRequested, this, [this](PanelId panel) { addBeside(panel, Follow::MirroredInstance); });
    connect(attributes_, &AttributeEditor::bakeRequested, this, [this](PanelId panel) {
        std::size_t baked = 0;
        if (change([&](Document& doc) { baked = bake(doc, panel); }))
            say(baked == 0 ? "Nothing to bake: the panel has no formulas." : QString("Baked %1 formula%2 into numbers.").arg(baked).arg(baked == 1 ? "" : "s"));
    });
    connect(attributes_, &AttributeEditor::placeFromTableRequested, this, [this](PanelId panel) {
        // No place of its own: the editor gives it one from the table again, keeping its anchor.
        change([&](Document& doc) { doc.arrange(panel, Arrangement{.anchor = doc.get(panel).arrangement.anchor}); }, {.touches = session::Touches::PlacementOnly});
    });
    connect(attributes_, &AttributeEditor::baseOnlyChanged, this, [this](PanelId panel, bool baseOnly) {
        change([&](Document& doc) { doc.setConstruction(panel, baseOnly); });
    });
    connect(attributes_, &AttributeEditor::deleteRequested, this, [this](PanelId panel) {
        if (change([&](Document& doc) { doc.remove(panel); }, {.ifItBreaks = session::IfItBreaks::Allow})) select(std::nullopt);
    });

    connect(cloth_, &ClothPanel::distanceTyped, this, [this](QString text) {
        const std::optional<Expr> value = read(text, Unit::Length, cloth_->distance());
        const std::optional<double> number = value ? asNumber(*value) : std::nullopt;
        if (number && *number >= 0.5 && *number <= 5.0) session_.setParticleDistance(*number);
        else if (value) cloth_->distance()->refuse("Between 0.5 cm and 5 cm.");
    });
    connect(cloth_, &ClothPanel::ownThreadChanged, this, [this](bool on) {
        session_.setThreading(on ? session::Threading::OwnThread : session::Threading::OneStepPerAdvance);
        shelf_->simulate()->setChecked(false);
    });
    connect(cloth_, &ClothPanel::bodyShownChanged, this, &MainWindow::showBody);

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
    tick();
}

void MainWindow::startPicking(OpId layer, ReferenceId reference) {
    picking_ = Picking{layer, reference};
    shelf_->setTool(Tool::Select);
    table_->setTool(Tool::Select);
    refresh();
}

void MainWindow::select(std::optional<PanelId> panel) {
    if (panel && !session_.document().contains(*panel)) panel.reset();
    if (panel == drafting_.selection.panel) return;
    drafting_.handle(session_.editor(), session::SelectPanel{panel});
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
    if (space_->isVisible()) space_->show(session_, drafting_.selection.panel, meshShown, bodyShown_);
    space_->showTime(session_.clothShown() ? std::optional<double>(session_.cloth().time) : std::nullopt, session_.clothShown() && session_.cloth().sewing);
    cloth_->show(session_, session_.threading() == session::Threading::OwnThread, bodyShown_);
    (void)took;
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
    const Result& shapes = session_.editor().shapes();
    std::optional<PanelId>& selected = drafting_.selection.panel;
    if (selected && !doc.contains(*selected)) drafting_.forget(doc);
    const QString body = session_.avatarPath().empty() ? "No body" : QFileInfo(QString::fromStdString(session_.avatarPath())).fileName();
    outliner_->show(doc, shapes, selected, body);
    parameters_->show(doc, shapes);
    // Looking back only at a step of the selected panel.
    if (viewUntil_ && (!selected || !doc.contains(*viewUntil_) || !(doc.panelOf(*viewUntil_) == *selected))) viewUntil_.reset();
    if (viewUntil_) past_ = evaluateUpTo(doc, *viewUntil_);
    attributes_->show(doc, shapes, selected, viewUntil_);
    table_->show(doc, viewUntil_ ? past_ : shapes, selected, drafting_.selection.corners);
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

void MainWindow::on(const session::Gesture& gesture) {
    if (viewUntil_ && !std::holds_alternative<session::SelectPanel>(gesture)) {
        table_->cancel();
        say("Showing an earlier step: click Show all steps in the Attribute Editor to change the pattern again.", true);
        return;
    }
    const session::Outcome done = drafting_.handle(session_.editor(), gesture);
    if (!done.notice.empty()) say(QString::fromStdString(done.notice), done.refused);
    else if (std::holds_alternative<session::MoveCorner>(gesture) || std::holds_alternative<session::MovePanel>(gesture)) say({});
    if (done.toolDone) {
        shelf_->setTool(Tool::Select);
        table_->setTool(Tool::Select);
    }
    refresh();
    if (done.typeLength) {
        // The length is a field of a layer: open that layer and type there.
        dock("Attribute Editor")->show();
        dock("Attribute Editor")->raise();
        attributes_->openLayer(done.typeLength->operation);
        if (ValueField* field = attributes_->field(done.typeLength->field)) {
            field->setFocus();
            field->selectAll();
        }
        say(QString::fromStdString("Type the " + done.typeLength->label + "."));
    }
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
    space_->setVisible(layout != ViewLayout::One || lastView_ == space_);
    table_->setVisible(layout != ViewLayout::One || lastView_ == table_);
}

void MainWindow::addBeside(PanelId source, Follow how) {
    const Panel& from = session_.document().get(source);
    double x = from.x + 30_cm;
    const Result& shapes = session_.editor().shapes();
    if (const std::optional<Bounds> box = bounds(shapes[source])) {
        // Just right of where the source is; a mirrored panel's own left edge is the source's right one.
        const double newLeft = how == Follow::MirroredInstance ? -box->max.x : box->min.x;
        const std::optional<Bounds> placed = bounds(turned(shapes[source], from.angle));
        x = from.x + (placed ? placed->max.x : box->max.x) + 12_cm - newLeft;
    }
    const char* suffix = how == Follow::Copy ? " copy" : how == Follow::Instance ? " instance" : " mirrored";
    std::optional<PanelId> added;
    change([&](Document& doc) {
        const std::string name = firstFreeName(doc, doc.get(source).name + suffix);
        if (how == Follow::Copy) {
            added = doc.copy(source, Panel{.name = name, .x = x, .y = from.y});
        } else {
            added = doc.add(Panel{.name = name, .x = x, .y = from.y});
            doc.add(*added, Instance{.source = source, .mirrored = how == Follow::MirroredInstance});
        }
    });
    if (added) select(added);
}

} // namespace clothcad::ui
