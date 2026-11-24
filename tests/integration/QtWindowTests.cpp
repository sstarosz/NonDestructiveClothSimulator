// The Qt window, used through its own widgets the way a person would, without
// a screen (QT_QPA_PLATFORM=offscreen): what the document became is checked.

#include <QAction>
#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QTemporaryDir>
#include <QScrollBar>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <random>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "AttributeEditor.h"
#include "Expect.h"
#include "MainWindow.h"
#include "Outliner.h"
#include "ParametersPanel.h"
#include "SpaceView.h"
#include "TableView.h"
#include "Theme.h"
#include "ValueField.h"
#include "clothcad/core/Fields.h"
#include "clothcad/core/File.h"
#include "clothcad/core/Text.h"
#include "clothcad/session/Samples.h"
#include "clothcad/sim/Layout.h"

using namespace clothcad;
using namespace clothcad::ui;

namespace {

// One application for the whole run, as Qt wants. It is never deleted: on
// the off-screen platform, closing it after mouse events were sent crashes
// as the program ends (seen with Qt 6.2), after every test has passed.
QApplication& application() {
    static int argc = 1;
    static char name[] = "clothcad_qt_window_tests";
    static char* argv[] = {name, nullptr};
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication& app = *new QApplication(argc, argv);
    static const bool themed = (theme::apply(app), true);
    (void)themed;
    return app;
}

// The window with the T-shirt body open, the cloth stepping once a tick.
struct Window {
    QApplication& app = application();  // first: members are made in this order
    MainWindow window{session::Threading::OneStepPerAdvance};
    Window() {
        window.openSample(5);
        window.show();
        waitForTheBody();
    }
    // An avatar is there at once; working out how to collide with it takes a
    // moment, and longer in a build that is not optimised.
    void waitForTheBody() {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!window.session().bodyReady() && std::chrono::steady_clock::now() < limit) {
            window.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(window.session().bodyReady());
        window.tick();
    }
    const Document& doc() const { return window.session().document(); }
    PanelId panel(const std::string& name) const {
        for (PanelId p : doc().panels())
            if (doc().get(p).name == name) return p;
        FAIL("no panel " << name);
    }
    Param parameter(const std::string& name) const {
        for (Param p : doc().parameters())
            if (doc().get(p).name == name) return p;
        FAIL("no parameter " << name);
    }
};

// Types into a field and presses Enter, as a person does.
void type(QLineEdit* field, const QString& text) {
    REQUIRE(field != nullptr);
    field->setFocus();
    field->setText(text);
    emit field->editingFinished();
}

template <class T> OpId first(const Document& doc, PanelId panel) {
    for (OpId op : doc.operations(panel))
        if (std::holds_alternative<T>(doc.get(op))) return op;
    FAIL("no such operation");
    return {};
}

} // namespace

TEST_CASE("the window opens with its shelf, docks and two views", "[qt]")
{
    Window w;
    EXPECT((w.window.shelf()->tabCount() == 3), "the shelf has its Draft, Sew and Simulate tabs");
    for (const char* name : {"Outliner", "Parameters", "Attribute Editor"}) EXPECT((w.window.dock(name) != nullptr), std::string("there is a dock ") + name);
    EXPECT((w.window.outliner()->panelsShown().size() == 2), "the Outliner lists the sample's two panels");
    EXPECT((w.window.parameters()->field(w.parameter("chest"))->text() == "96 cm"), "a parameter shows its unit");
    EXPECT((w.window.space()->isVisible() && w.window.table()->isVisible()), "both views are shown, side by side");
}

TEST_CASE("undo, play and reset sit in a row under the menu; the tools and view layouts in a toolbox at the left; no button is shown twice", "[qt]")
{
    Window w;
    Shelf& shelf = *w.window.shelf();
    const auto shownIn = [&](QAction* action) {
        int bars = 0;
        for (QToolBar* bar : w.window.findChildren<QToolBar*>())
            if (bar->actions().contains(action)) ++bars;
        return bars;
    };
    EXPECT((w.window.toolBarArea(shelf.actionRow()) == Qt::TopToolBarArea), "the action row is at the top, above the shelf");
    EXPECT((w.window.toolBarArea(shelf.toolbox()) == Qt::LeftToolBarArea), "the toolbox is at the left edge, beside the docks");
    EXPECT((shelf.actionRow()->geometry().top() < shelf.parentWidget()->geometry().top()), "and the action row is above the shelf");
    EXPECT((shelf.toolbox()->geometry().right() <= w.window.dock("Outliner")->geometry().left()), "the toolbox is left of the Outliner");
    for (QAction* action : {shelf.undo(), shelf.redo(), shelf.simulate(), shelf.reset()})
        EXPECT((shelf.actionRow()->actions().contains(action) && shownIn(action) == 1), "undo, redo, simulate and reset are in the action row only");
    for (Tool tool : {Tool::Select, Tool::Move, Tool::Rotate, Tool::Scale})
        EXPECT((shelf.toolbox()->actions().contains(shelf.action(tool)) && shownIn(shelf.action(tool)) == 1), std::string(nameOf(tool)) + " is in the toolbox only");
    for (ViewLayout layout : {ViewLayout::SpaceOnly, ViewLayout::TableOnly, ViewLayout::SideBySide, ViewLayout::Stacked})
        EXPECT((shelf.toolbox()->actions().contains(shelf.layout(layout))), "the toolbox chooses how the views share the window");
    for (int tool = 0; tool <= static_cast<int>(Tool::Pull); ++tool)
        EXPECT((shownIn(shelf.action(static_cast<Tool>(tool))) == 1), std::string(nameOf(static_cast<Tool>(tool))) + " is shown in one place");
}

TEST_CASE("the toolbox keeps the tools on top and the view layouts at the bottom; the two views share the middle in half", "[qt]")
{
    Window w;
    Shelf& shelf = *w.window.shelf();
    QWidget* scale = shelf.toolbox()->widgetForAction(shelf.action(Tool::Scale));
    QWidget* first = shelf.toolbox()->widgetForAction(shelf.layout(ViewLayout::SpaceOnly));
    QWidget* last = shelf.toolbox()->widgetForAction(shelf.layout(ViewLayout::Stacked));
    REQUIRE((scale && first && last));
    EXPECT((scale->geometry().bottom() < shelf.toolbox()->height() / 2), "the tools are in the top half of the toolbox");
    EXPECT((first->geometry().top() > shelf.toolbox()->height() / 2), "the view layouts are in the bottom half");
    EXPECT((shelf.toolbox()->height() - last->geometry().bottom() < 120), "and the last one is close to the bottom");

    const auto half = [&] {
        const double space = w.window.space()->width(), table = w.window.table()->width();
        return std::abs(space - table) < 0.05 * (space + table);
    };
    EXPECT((half()), "side by side: the 3D view and the table are each half");
    w.window.shelf()->layout(ViewLayout::Stacked)->trigger();
    const double up = w.window.space()->height(), down = w.window.table()->height();
    EXPECT((std::abs(up - down) < 0.05 * (up + down)), "stacked: each is half the height");
    w.window.shelf()->layout(ViewLayout::SideBySide)->trigger();
    EXPECT((half()), "and half again when put side by side again");
}

TEST_CASE("the Attribute Editor opens without a horizontal scroll bar, in it or in its list of layers", "[qt]")
{
    Window w;
    AttributeEditor& editor = *w.window.attributes();
    const auto scrolls = [&](QScrollArea* area) {
        return area->horizontalScrollBar()->isVisible() || area->widget()->minimumSizeHint().width() > area->viewport()->width();
    };
    QScrollArea* layers = editor.findChild<QScrollArea*>("layerScroll");
    REQUIRE(layers != nullptr);
    for (const char* name : {"front", "back"}) {
        w.window.select(w.panel(name));
        w.window.tick();
        QApplication::processEvents();
        EXPECT((!scrolls(&editor)), std::string("the editor of ") + name + " fits its width as it opens");
        EXPECT((!scrolls(layers)), std::string("and so does its list of layers (") + name + ")");
    }
    w.window.select(std::nullopt);
    w.window.tick();
    EXPECT((!scrolls(&editor)), "nor does it with nothing selected");
}

TEST_CASE("Space starts and stops the simulation, from the 3D view as well; it keeps its state until Reset", "[qt]")
{
    Window w;
    QAction* simulate = w.window.shelf()->simulate();
    EXPECT((simulate->shortcut() == QKeySequence("Space")), "Space is the simulation's key");
    w.window.space()->key(QKeyCombination(Qt::Key_Space));
    EXPECT((simulate->isChecked() && simulate->text() == "Pause"), "Space starts it, and the button now says Pause");
    for (int i = 0; i < 10; ++i) w.window.tick();
    const double time = w.window.session().cloth().time;
    EXPECT((time > 0.0), "the cloth moves");
    w.window.space()->key(QKeyCombination(Qt::Key_Space));
    EXPECT((!simulate->isChecked() && simulate->text() == "Simulate"), "Space again pauses it");
    for (int i = 0; i < 10; ++i) w.window.tick();
    EXPECT((w.window.session().cloth().time == time), "paused, the cloth stands still");
    w.window.space()->key(QKeyCombination(Qt::Key_Space));
    for (int i = 0; i < 10; ++i) w.window.tick();
    EXPECT((w.window.session().cloth().time > time), "and starts again where it was");
}

TEST_CASE("a chosen panel's operations show as layers, newest on top", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.outliner()->choose(front);
    EXPECT((w.window.selected() == front), "choosing a panel in the Outliner selects it");
    const std::vector<OpId> layers = w.window.attributes()->layersShown();
    REQUIRE(layers.size() == w.doc().operations(front).size());
    EXPECT((std::holds_alternative<Mirror>(w.doc().get(layers.front()))), "a mirror applied last is the top layer");
    EXPECT((std::holds_alternative<Rect>(w.doc().get(layers.back()))), "the base rectangle is at the bottom");
    EXPECT((std::holds_alternative<Bezier>(w.doc().get(layers[1]))), "below the mirror, the newest step");
}

TEST_CASE("every layer shows its fields until it is closed with a click on its name", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    AttributeEditor& editor = *w.window.attributes();
    const OpId rect = first<Rect>(w.doc(), front);
    const OpId armhole = first<Curve>(w.doc(), front);
    EXPECT((editor.isOpen(rect) && editor.isOpen(armhole)), "layers are open by default");
    EXPECT((editor.field(rect, FieldId{0}) != nullptr && editor.field(armhole, FieldId{0}) != nullptr), "and show their fields together");
    editor.toggleLayer(armhole);
    EXPECT((!editor.isOpen(armhole) && editor.field(armhole, FieldId{0}) == nullptr && editor.isOpen(rect)), "a click closes that layer only");
    editor.toggleLayer(armhole);
    EXPECT((editor.isOpen(armhole)), "a second click opens it again");
}

TEST_CASE("a layer is switched off with its eye, and undo brings it back", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    const OpId armhole = first<Curve>(w.doc(), front);
    w.window.attributes()->switchLayer(armhole, false);
    EXPECT((!w.doc().enabled(armhole)), "the eye switches the layer off");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().enabled(armhole)), "undo switches it on again");
}

TEST_CASE("values are typed with or without their unit; what does not read is refused", "[qt]")
{
    Window w;
    const Param chest = w.parameter("chest");
    ValueField* field = w.window.parameters()->field(chest);
    type(field, "104");
    EXPECT((asNumber(w.doc().get(chest).value) == 104.0), "a bare number is centimetres");
    EXPECT((field->text() == "104 cm"), "and is shown with its unit");
    type(field, "1 m");
    EXPECT((asNumber(w.doc().get(chest).value) == 104.0 && field->property("refused").toBool()), "what does not read changes nothing and is marked");
    EXPECT((!w.window.helpLine().isEmpty()), "the help line says why");
    type(field, "= length + 30");
    EXPECT((!asNumber(w.doc().get(chest).value).has_value()), "a formula is taken");
    EXPECT((field->text().startsWith("=")), "and shown as one");
}

TEST_CASE("an open layer's fields show their units; a change that breaks the panel is refused", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    OpId underarm;
    for (OpId op : w.doc().operations(front))
        if (const AddPoint* point = std::get_if<AddPoint>(&w.doc().get(op)); point && point->at.edge.role == static_cast<uint16_t>(Rect::Edge::Right)) underarm = op;
    w.window.attributes()->openLayer(underarm);
    REQUIRE(w.window.attributes()->isOpen(underarm));
    ValueField* position = w.window.attributes()->field(underarm, FieldId{0});
    REQUIRE(position != nullptr);
    std::printf("      the underarm point's position shows as \"%s\"\n", position->text().toStdString().c_str());
    EXPECT((position->text().startsWith("=")), "it is a formula, shown as one");
    type(position, "50");
    EXPECT((asNumber(std::get<AddPoint>(w.doc().get(underarm)).at.along) == 0.5), "50 in a fraction is 50 %");
    EXPECT((w.window.attributes()->field(underarm, FieldId{0})->text() == "50 %"), "and is shown so");

    const OpId rect = first<Rect>(w.doc(), front);
    w.window.attributes()->openLayer(rect);
    type(w.window.attributes()->field(rect, FieldId{0}), "-5");
    EXPECT((asNumber(std::get<Rect>(w.doc().get(rect)).width) != -5.0), "a width below nothing is refused");
    EXPECT((w.window.helpLine().startsWith("Not changed")), "and the help line says so");
}

TEST_CASE("placement is typed in space; the views can be laid out", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    type(w.window.attributes()->spaceField(1), "120");
    EXPECT((w.doc().get(front).arrangement.y == 120.0), "y in space is typed in cm");
    type(w.window.attributes()->spaceField(6), "160");
    EXPECT((w.doc().get(front).arrangement.wrap == 160.0), "wrap in degrees");

    w.window.shelf()->layout(ViewLayout::Stacked)->trigger();
    EXPECT((w.window.viewLayout() == ViewLayout::Stacked && w.window.space()->isVisible() && w.window.table()->isVisible()), "stacked shows both");
    w.window.shelf()->layout(ViewLayout::SpaceOnly)->trigger();
    EXPECT((w.window.space()->isVisible() && !w.window.table()->isVisible()), "3D view only shows the 3D view");
    w.window.shelf()->layout(ViewLayout::TableOnly)->trigger();
    EXPECT((!w.window.space()->isVisible() && w.window.table()->isVisible()), "table only shows the table");
    w.window.shelf()->layout(ViewLayout::SpaceOnly)->trigger();
    EXPECT((w.window.viewLayout() == ViewLayout::SpaceOnly && !w.window.table()->isVisible()), "and each stays, whichever view was used last");
    w.window.shelf()->layout(ViewLayout::SideBySide)->trigger();
    EXPECT((w.window.space()->isVisible() && w.window.table()->isVisible()), "side by side brings both back");
}

TEST_CASE("simulate runs the cloth; copy and instance add a panel beside", "[qt]")
{
    Window w;
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 20; ++i) w.window.tick();
    EXPECT((w.window.session().clothShown() && w.window.session().cloth().time > 0.2), "Simulate lets the cloth fall");
    w.window.shelf()->reset()->trigger();
    w.window.tick();
    EXPECT((!w.window.shelf()->simulate()->isChecked()), "Reset stops it");

    const PanelId front = w.panel("front");
    w.window.select(front);
    w.window.outliner()->copy(front);
    EXPECT((w.doc().panels().size() == 3 && w.window.selected() != front), "Copy adds a panel and selects it");
    w.window.select(front);
    w.window.shelf()->instance()->trigger();
    EXPECT((w.doc().panels().size() == 4 && std::holds_alternative<Instance>(w.doc().get(w.doc().operations(*w.window.selected()).front()))), "Instance adds one that follows");
    EXPECT((w.window.outliner()->panelsShown().size() == 4), "and the Outliner shows them");
}

// ---- The table's gestures ----

namespace {

// A plain rectangle of numbers, and one whose width is a formula.
struct Table : Window {
    Param widthParameter;
    Table() {
        Document doc;
        widthParameter = doc.add(Parameter{.name = "width", .value = 20_cm});
        const PanelId plain = doc.add(Panel{.name = "plain"});
        doc.add(plain, Rect{.width = 30_cm, .height = 10_cm});
        const PanelId driven = doc.add(Panel{.name = "driven"});
        doc.add(driven, Rect{.width = widthParameter, .height = 10_cm});
        window.open(std::move(doc), "table");
        window.tick();
    }
    TableView* table() const { return window.table(); }
    // Where a panel's corner is on the table: the one farthest along (x, y).
    Vec2 corner(PanelId panel, double x, double y) const {
        const Pattern& shape = window.session().editor().shapes()[panel];
        const Panel& placed = doc().get(panel);
        Vec2 best{};
        double most = -1e300;
        for (const Vertex& v : shape.vertices)
            if (const double along = v.position.x * x + v.position.y * y; along > most) most = along, best = v.position;
        const Vec2 on = turned(best, placed.angle);
        return {on.x + placed.x, on.y + placed.y};
    }
    Vec2 middle(PanelId panel) const {
        const std::optional<Bounds> box = bounds(window.session().editor().shapes()[panel]);
        const Panel& placed = doc().get(panel);
        return {placed.x + (box->min.x + box->max.x) / 2, placed.y + (box->min.y + box->max.y) / 2};
    }
    double width(PanelId panel) const {
        const std::optional<Bounds> box = bounds(window.session().editor().shapes()[panel]);
        return box->max.x - box->min.x;
    }
};

void send(QWidget* widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons held, Qt::KeyboardModifiers keys) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, held, keys);
    QApplication::sendEvent(widget, &event);
}

void click(TableView* table, QPointF at, Qt::KeyboardModifiers keys = {}) {
    send(table, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, keys);
    send(table, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, keys);
}

// Presses, moves there in a few steps, and lets go, as a hand does.
void drag(TableView* table, QPointF from, QPointF to, Qt::KeyboardModifiers keys = {}) {
    send(table, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, keys);
    for (int i = 1; i <= 5; ++i) send(table, QEvent::MouseMove, from + (to - from) * (i / 5.0), Qt::NoButton, Qt::LeftButton, keys);
    send(table, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, keys);
}

QPointF by(QPointF at, TableView* table, double xCm, double yCm) { return at + QPointF(xCm * table->scale(), -yCm * table->scale()); }

} // namespace

TEST_CASE("a panel is dragged on the table as one undo step; Ctrl puts it on the grid", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    const Panel was = w.doc().get(plain);
    const QPointF at = w.table()->toScreen(w.middle(plain));
    drag(w.table(), at, by(at, w.table(), 12, -4));
    EXPECT((w.window.selected() == plain), "pressing a panel selects it");
    EXPECT((std::abs(w.doc().get(plain).x - (was.x + 12)) < 0.2 && std::abs(w.doc().get(plain).y - (was.y - 4)) < 0.2), "it moves with the pointer");
    EXPECT((w.width(plain) == 30.0), "placement is not shape");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(plain).x == was.x && w.doc().get(plain).y == was.y), "one undo puts it back");

    drag(w.table(), at, by(at, w.table(), 7.3, 2.1), Qt::ControlModifier);
    // The grid's step at this zoom, as the table draws it.
    double step = 0.1;
    while (step * w.table()->scale() < 12.0) step *= 10.0;
    const double x = w.doc().get(plain).x, y = w.doc().get(plain).y;
    EXPECT((std::abs(x / step - std::round(x / step)) < 1e-6 && std::abs(y / step - std::round(y / step)) < 1e-6), "Ctrl puts its place on the grid");

    click(w.table(), w.table()->toScreen({-1000, -1000}));
    EXPECT((!w.window.selected()), "a click on the empty table selects nothing");
}

TEST_CASE("a corner is dragged on the table; one held by a formula stays and says why", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    const QPointF corner = w.table()->toScreen(w.corner(plain, 1, 1));
    drag(w.table(), corner, by(corner, w.table(), 5, 0));
    EXPECT((std::abs(w.width(plain) - 35.0) < 0.2), "dragging the far corner widens the panel");
    EXPECT((std::holds_alternative<AdjustPoint>(w.doc().get(w.doc().operations(plain).back()))), "as a layer that moves the corner");
    EXPECT((w.window.selectedCorners().size() == 1), "and the corner is selected");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.width(plain) == 30.0 && w.doc().operations(plain).size() == 1), "one undo takes the whole drag back");

    const PanelId driven = w.panel("driven");
    w.window.select(driven);
    const QPointF held = w.table()->toScreen(w.corner(driven, 1, 1));
    const std::size_t layers = w.doc().operations(driven).size();
    drag(w.table(), held, by(held, w.table(), 5, 0));
    EXPECT((w.doc().operations(driven).size() == layers && w.width(driven) == 20.0), "a corner a formula places does not move");
    EXPECT((w.window.helpLine().contains("formula")), "and the help line says why");
}

TEST_CASE("Shift-drag selects corners in a box; dragging one moves them together", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    // A box around the right side's two corners.
    const Vec2 top = w.corner(plain, 1, 1), bottom = w.corner(plain, 1, -1);
    drag(w.table(), w.table()->toScreen({top.x - 2, top.y + 2}), w.table()->toScreen({bottom.x + 2, bottom.y - 2}), Qt::ShiftModifier);
    EXPECT((w.window.selectedCorners().size() == 2), "the box selects the two corners inside it");
    const QPointF from = w.table()->toScreen(top);
    drag(w.table(), from, by(from, w.table(), 6, 0));
    EXPECT((std::abs(w.width(plain) - 36.0) < 0.2), "both move: the panel is wider all the way down");
    click(w.table(), w.table()->toScreen(w.corner(plain, -1, 1)), Qt::ShiftModifier);
    EXPECT((w.window.selectedCorners().size() == 3), "Shift-click adds a corner");
}

TEST_CASE("a double-click on an edge opens its length for typing", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    // The bottom edge, halfway along.
    const Vec2 a = w.corner(plain, -1, -1), b = w.corner(plain, 1, -1);
    const QPointF at = w.table()->toScreen({(a.x + b.x) / 2, (a.y + b.y) / 2});
    send(w.table(), QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton, {});
    const OpId rect = first<Rect>(w.doc(), plain);
    EXPECT((w.window.attributes()->isOpen(rect)), "the layer that sets the length is open");
    ValueField* field = w.window.attributes()->field(rect, FieldId{0});
    REQUIRE(field != nullptr);
    type(field, "42");
    EXPECT((w.width(plain) == 42.0), "and the typed length is the edge's");
}

// ---- The drawing tools on the table ----

namespace {

void choose(Table& w, Tool tool) { w.window.shelf()->action(tool)->trigger(); }

Vec2 between(Vec2 a, Vec2 b, double along) { return {a.x + (b.x - a.x) * along, a.y + (b.y - a.y) * along}; }

template <class T> bool lastIs(const Document& doc, PanelId panel) {
    const std::vector<OpId> operations = doc.operations(panel);
    return !operations.empty() && std::holds_alternative<T>(doc.get(operations.back()));
}

} // namespace

TEST_CASE("Q, W, E and R choose Select, Move, Rotate and Scale; only the last three show a gizmo, in both views", "[qt]")
{
    Table w;
    w.window.select(w.panel("plain"));
    for (const auto& [tool, key] : {std::pair{Tool::Select, "Q"}, std::pair{Tool::Move, "W"}, std::pair{Tool::Rotate, "E"}, std::pair{Tool::Scale, "R"}})
        EXPECT((w.window.shelf()->action(tool)->shortcut() == QKeySequence(key)), std::string(nameOf(tool)) + " is on " + key + ", as in Maya");
    EXPECT((w.window.shelf()->action(Tool::Rectangle)->shortcut().isEmpty()), "Rectangle has no key: R scales");
    w.window.tick();
    EXPECT((!w.table()->gizmo() && !w.window.space()->gizmo()), "Select shows no gizmo");

    // Keys typed in the 3D view reach the shelf, whichever surface it draws on.
    SpaceView& space = *w.window.space();
    space.key(QKeyCombination(Qt::Key_E));
    w.window.tick();
    EXPECT((w.window.shelf()->tool() == Tool::Rotate), "E in the 3D view chooses Rotate");
    EXPECT((w.table()->gizmo() && w.table()->gizmo()->mode == session::GizmoMode::Rotate), "the table shows the rotate gizmo");
    EXPECT((space.gizmo() && space.gizmo()->mode == session::GizmoMode::Rotate), "and so does the 3D view");
    space.key(QKeyCombination(Qt::Key_R));
    EXPECT((w.window.shelf()->tool() == Tool::Scale && space.gizmo()->mode == session::GizmoMode::Scale), "R: Scale");
    space.key(QKeyCombination(Qt::Key_W));
    EXPECT((w.window.shelf()->tool() == Tool::Move && w.table()->gizmo()->mode == session::GizmoMode::Move), "W: Move");
    space.key(QKeyCombination(Qt::Key_Q));
    EXPECT((w.window.shelf()->tool() == Tool::Select && !space.gizmo() && !w.table()->gizmo()), "Q: back to Select, no gizmo");
}

TEST_CASE("on the table the move gizmo's arrow moves a panel along x only, its ring turns it, its handle scales it", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    TableView* table = w.table();

    choose(w, Tool::Move);
    REQUIRE(table->gizmo());
    const Panel was = w.doc().get(plain);
    const QPointF onArrow = table->toScreen(session::handleEnd(*table->gizmo(), session::TableHandle::AlongX)) - QPointF(25.0, 0.0);
    drag(table, onArrow, onArrow + QPointF(40.0, -30.0));
    EXPECT((std::abs(w.doc().get(plain).x - (was.x + 40.0 / table->scale())) < 0.06), "the x arrow moves the panel along x");
    EXPECT((w.doc().get(plain).y == was.y), "and not along y");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(plain).x == was.x), "the drag is one step to undo");

    // A quarter turn, from the ring's right to its top, in steps of 15° with Ctrl.
    choose(w, Tool::Rotate);
    REQUIRE(table->gizmo());
    const Vec2 middle = w.middle(plain);
    const QPointF pivot = table->toScreen(middle);
    drag(table, pivot + QPointF(session::tableRingPixels, 0.0), pivot + QPointF(0.0, -session::tableRingPixels), Qt::ControlModifier);
    EXPECT((std::abs(w.doc().get(plain).angle - 90.0) < 1e-6), "the ring turns the panel by the angle swept");
    const Vec2 turnedMiddle = [&] {
        const std::optional<Bounds> box = bounds(w.window.session().editor().shapes()[plain]);
        const Vec2 own = turned(Vec2{(box->min.x + box->max.x) / 2, (box->min.y + box->max.y) / 2}, w.doc().get(plain).angle);
        return Vec2{w.doc().get(plain).x + own.x, w.doc().get(plain).y + own.y};
    }();
    EXPECT((std::abs(turnedMiddle.x - middle.x) < 1e-6 && std::abs(turnedMiddle.y - middle.y) < 1e-6), "about its middle, which stays where it was");
    EXPECT((w.width(plain) == 30.0), "turning is not shape");

    // Turned a quarter, the panel's own x points up the table: so does the scale gizmo's x handle.
    choose(w, Tool::Scale);
    REQUIRE(table->gizmo());
    const QPointF centre = table->toScreen(table->gizmo()->centre);
    const QPointF end = table->toScreen(session::handleEnd(*table->gizmo(), session::TableHandle::AlongX));
    EXPECT((std::abs(end.x() - centre.x()) < 1e-6 && end.y() < centre.y()), "the x handle points up the screen, along the panel's own x");
    const QPointF unit = (end - centre) / session::tableArrowPixels;
    const std::size_t layers = w.doc().operations(plain).size();
    drag(table, centre + unit * 60.0, centre + unit * 90.0);
    REQUIRE(w.doc().operations(plain).size() == layers + 1);
    const Scale* scale = std::get_if<Scale>(&w.doc().get(w.doc().operations(plain).back()));
    REQUIRE(scale != nullptr);
    EXPECT((asNumber(scale->x) == 1.5 && asNumber(scale->y) == 1.0), "half as far again from the middle: a Scale layer of 150 % along x");
    EXPECT((std::abs(w.width(plain) - 45.0) < 1e-6), "the panel is half as wide again");
    const std::optional<Bounds> scaledBox = bounds(w.window.session().editor().shapes()[plain]);
    const Vec2 scaledMiddle = turned(Vec2{(scaledBox->min.x + scaledBox->max.x) / 2, (scaledBox->min.y + scaledBox->max.y) / 2}, w.doc().get(plain).angle);
    EXPECT((std::abs(w.doc().get(plain).x + scaledMiddle.x - middle.x) < 1e-6 && std::abs(w.doc().get(plain).y + scaledMiddle.y - middle.y) < 1e-6),
           "scaled about its middle, which stays where it was");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().operations(plain).size() == layers && w.width(plain) == 30.0), "undo takes the layer away");
}

TEST_CASE("Rectangle and Circle are dragged out and Ring is a click; each new panel is selected", "[qt]")
{
    Table w;
    TableView* table = w.table();
    choose(w, Tool::Rectangle);
    drag(table, table->toScreen({300, 300}), table->toScreen({320, 315}));
    REQUIRE(w.doc().panels().size() == 3);
    const PanelId drawn = w.doc().panels().back();
    EXPECT((w.window.selected() == drawn && std::abs(w.width(drawn) - 20.0) < 0.6), "a dragged rectangle is a new panel of that size, selected");
    EXPECT((w.window.shelf()->tool() == Tool::Select), "and the shelf goes back to Select, to move it straight away");

    choose(w, Tool::Rectangle);
    click(table, table->toScreen({400, 400}));
    EXPECT((w.doc().panels().size() == 3), "a click without a drag makes nothing");

    choose(w, Tool::Circle);
    drag(table, table->toScreen({400, 300}), table->toScreen({410, 300}));
    REQUIRE(w.doc().panels().size() == 4);
    EXPECT((std::abs(w.width(w.doc().panels().back()) - 20.0) < 1.1), "a circle is dragged from its centre out to its radius");

    choose(w, Tool::Ring);
    click(table, table->toScreen({500, 300}));
    EXPECT((w.doc().panels().size() == 5 && first<RingSector>(w.doc(), w.doc().panels().back()) == w.doc().operations(w.doc().panels().back()).front()), "Ring places a skirt with a click");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().panels().size() == 4), "each is one undo step");
}

TEST_CASE("Point puts a corner on an edge; Curve makes an edge a curve with handles", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain");
    const Vec2 bottomLeft = w.corner(plain, -1, -1), bottomRight = w.corner(plain, 1, -1), topRight = w.corner(plain, 1, 1);
    const std::size_t corners = w.window.session().editor().shapes()[plain].vertices.size();
    choose(w, Tool::Point);
    click(table, table->toScreen(between(bottomLeft, bottomRight, 0.4)));
    EXPECT((lastIs<AddPoint>(w.doc(), plain) && w.window.session().editor().shapes()[plain].vertices.size() == corners + 1), "a click on an edge adds a corner there");
    choose(w, Tool::Point);
    click(table, table->toScreen(bottomRight));
    EXPECT((w.window.session().editor().shapes()[plain].vertices.size() == corners + 1 && w.window.helpLine().contains("corner")), "on a corner it adds nothing and says why");

    choose(w, Tool::Curve);
    click(table, table->toScreen(between(bottomRight, topRight, 0.5)));
    EXPECT((lastIs<Bezier>(w.doc(), plain) && w.window.shelf()->tool() == Tool::Select), "a click on an edge makes it a curve, ready to drag its handles");
}

TEST_CASE("Line joins two points of one outline; Cut takes away the side clicked; Split makes two panels", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain");
    const Vec2 bottomLeft = w.corner(plain, -1, -1), bottomRight = w.corner(plain, 1, -1);
    const Vec2 topLeft = w.corner(plain, -1, 1), topRight = w.corner(plain, 1, 1);
    choose(w, Tool::Line);
    click(table, table->toScreen(between(bottomLeft, bottomRight, 1.0 / 3.0)));
    EXPECT((w.window.helpLine().contains("second point")), "after the first point the help line says what comes next");
    click(table, table->toScreen(between(topLeft, topRight, 1.0 / 3.0)));
    REQUIRE(lastIs<LineBetween>(w.doc(), plain));

    choose(w, Tool::Cut);
    click(table, table->toScreen(between(between(bottomLeft, topLeft, 0.5), between(bottomRight, topRight, 0.5), 0.1)));
    EXPECT((lastIs<Cut>(w.doc(), plain) && std::abs(w.width(plain) - 20.0) < 0.1), "a click left of the line cuts the left third away");
    w.window.shelf()->undo()->trigger();

    choose(w, Tool::Split);
    click(table, table->toScreen(between(between(bottomLeft, bottomRight, 1.0 / 3.0), between(topLeft, topRight, 1.0 / 3.0), 0.5)));
    EXPECT((w.doc().panels().size() == 3), "a click on the line splits the panel in two");
    EXPECT((std::abs(w.width(plain) + w.width(w.doc().panels().back()) - 30.0) < 0.1), "the two parts make the whole");
}

TEST_CASE("Sew joins two edges once; Mirror reflects a panel across a straight edge", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain"), driven = w.panel("driven");
    const Vec2 top = between(w.corner(plain, -1, 1), w.corner(plain, 1, 1), 0.5);
    const Vec2 otherTop = between(w.corner(driven, -1, 1), w.corner(driven, 1, 1), 0.5);
    choose(w, Tool::Sew);
    click(table, table->toScreen(top));
    EXPECT((w.window.helpLine().contains("sew it to")), "after the first edge the help line asks for the second");
    click(table, table->toScreen(otherTop));
    EXPECT((w.doc().seams().size() == 1), "the second click sews them");
    click(table, table->toScreen(top));
    click(table, table->toScreen(otherTop));
    EXPECT((w.doc().seams().size() == 1 && w.window.helpLine().contains("already sewn")), "the same pair is not sewn twice");

    choose(w, Tool::Mirror);
    click(table, table->toScreen(between(w.corner(plain, 1, -1), w.corner(plain, 1, 1), 0.5)));
    EXPECT((lastIs<Mirror>(w.doc(), plain) && std::abs(w.width(plain) - 60.0) < 0.1), "a click on the right edge mirrors the panel across it");
}

TEST_CASE("a layer's edge is chosen again by clicking it on the table", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain");
    choose(w, Tool::Mirror);
    click(table, table->toScreen(between(w.corner(plain, 1, -1), w.corner(plain, 1, 1), 0.5)));
    REQUIRE(lastIs<Mirror>(w.doc(), plain));
    const OpId mirror = w.doc().operations(plain).back();
    const Reference across = references(w.doc().get(mirror))[0].value;
    const double leftBefore = w.corner(plain, -1, 0).x;  // the rectangle's left edge, which the mirror kept

    emit w.window.attributes()->referenceClicked(mirror, references(w.doc().get(mirror))[0].id);
    EXPECT((table->choosing() && w.window.helpLine().contains("Click an edge")), "clicking the edge's name asks for one on the table");
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(table, &escape);
    EXPECT((!table->choosing() && references(w.doc().get(mirror))[0].value == across), "Esc drops it and changes nothing");

    emit w.window.attributes()->referenceClicked(mirror, references(w.doc().get(mirror))[0].id);
    // The rectangle as it was before the mirror: its left edge is where the panel's left still is.
    click(table, table->toScreen({leftBefore, w.corner(plain, 1, -1).y + 5.0}));
    EXPECT((!table->choosing() && !(references(w.doc().get(mirror))[0].value == across)), "a click on another edge makes the layer use it");
    EXPECT((std::abs(w.width(plain) - 60.0) < 0.1), "the panel is mirrored across that edge instead");
    w.window.shelf()->undo()->trigger();
    EXPECT((references(w.doc().get(mirror))[0].value == across), "and undo takes the choice back");
}

TEST_CASE("the cloth mesh can be drawn over the panels", "[qt]")
{
    Table w;
    EXPECT((!w.table()->meshShown()), "it is not shown at first");
    w.window.shelf()->mesh()->setChecked(true);
    for (int i = 0; i < 3 && !w.table()->meshShown(); ++i) w.window.tick();
    EXPECT((w.table()->meshShown()), "the switch on the shelf shows it");
    w.window.shelf()->mesh()->setChecked(false);
    w.window.tick();
    EXPECT((!w.table()->meshShown()), "and hides it again");
}

// ---- The 3D view ----

TEST_CASE("the 3D view draws the body and the cloth with Vulkan, or says why it cannot", "[qt]")
{
    Window w;
    if (w.window.space()->drawing() == SpaceView::Drawing::Nothing) {
        EXPECT((!w.window.space()->problem().isEmpty()), "without Vulkan the view says why");
        SKIP("No Vulkan on this machine: " << w.window.space()->problem().toStdString());
    }
    for (int i = 0; i < 3; ++i) w.window.tick();
    const render::Picture* picture = w.window.space()->picture();
    REQUIRE(picture != nullptr);
    REQUIRE(picture->width > 0);
    const render::Colour middle = picture->at(picture->width / 2, picture->height / 2), corner = picture->at(1, 1);
    EXPECT((std::abs(middle.red - corner.red) + std::abs(middle.green - corner.green) + std::abs(middle.blue - corner.blue) > 0.1f),
           "something is drawn in the middle: the body, framed when the view first shows");

    // Selecting a panel colours it amber in the picture somewhere.
    const auto amberPixels = [&] {
        int count = 0;
        const render::Picture* now = w.window.space()->picture();
        for (int y = 0; y < now->height; y += 2)
            for (int x = 0; x < now->width; x += 2) {
                const render::Colour c = now->at(x, y);
                count += c.red > 0.5f && c.red > c.blue * 2.0f && c.green > c.blue * 1.3f;
            }
        return count;
    };
    const int before = amberPixels();
    w.window.select(w.panel("front"));
    w.window.tick();
    EXPECT((amberPixels() > before + 20), "the selected panel is drawn amber");
}

TEST_CASE("Alt with the mouse turns, pans and dollies the camera; F frames everything", "[qt]")
{
    Window w;
    SpaceView* space = w.window.space();
    if (space->drawing() == SpaceView::Drawing::Nothing) SKIP("No Vulkan on this machine: " << space->problem().toStdString());
    w.window.tick();
    const render::OrbitCamera framed = space->camera();

    space->press({100, 100}, Qt::LeftButton, Qt::AltModifier);
    space->moveTo({160, 100}, Qt::LeftButton, Qt::AltModifier);
    space->release();
    EXPECT((std::abs(space->camera().yaw - (framed.yaw - 18.0)) < 1e-9), "Alt and the left button orbit");

    space->press({100, 100}, Qt::LeftButton, {});
    space->moveTo({160, 100}, Qt::LeftButton, {});
    space->release();
    EXPECT((std::abs(space->camera().yaw - (framed.yaw - 18.0)) < 1e-9), "the left button alone leaves the camera: it is for the cloth");

    const sim::Vec3 target = space->camera().target;
    space->press({100, 100}, Qt::MiddleButton, Qt::AltModifier);
    space->moveTo({100, 140}, Qt::MiddleButton, Qt::AltModifier);
    space->release();
    EXPECT((space->camera().target.y > target.y), "Alt and the middle button pan: dragging down moves the view's middle up");

    const double distance = space->camera().distance;
    space->wheel(2.0);
    EXPECT((space->camera().distance < distance), "the wheel comes nearer");
    space->press({100, 100}, Qt::RightButton, Qt::AltModifier);
    space->moveTo({60, 100}, Qt::RightButton, Qt::AltModifier);
    space->release();
    EXPECT((space->camera().distance > distance * 0.88 * 0.88), "Alt and the right button dragged left go farther");

    space->key(Qt::Key_F);
    EXPECT((std::abs(space->camera().distance - framed.distance) < 1e-6 && std::abs(space->camera().target.y - framed.target.y) < 1e-6),
           "F frames everything again, from where the camera looks");
}

namespace {

// Where a point of the picture is in the view's own coordinates, as press() takes them.
QPointF onView(const SpaceView& space, render::PicturePoint at) {
    const double scale = space.inPicture({1.0, 1.0}).x;
    return {at.x / scale, at.y / scale};
}

void click(SpaceView& space, QPointF at) {
    space.press(at, Qt::LeftButton, {});
    space.release();
}

} // namespace

TEST_CASE("a click in the 3D view selects the panel under it; its gizmo carries it along one axis", "[qt]")
{
    Window w;
    SpaceView& space = *w.window.space();
    w.window.select(std::nullopt);
    w.window.tick();
    EXPECT((!space.gizmo().has_value()), "with nothing selected there is no gizmo");

    // The panel nearest the camera, which looks at the body from the front.
    PanelId front = w.doc().panels().front();
    for (PanelId p : w.doc().panels())
        if (w.doc().get(p).arrangement.z > w.doc().get(front).arrangement.z) front = p;
    const Arrangement before = w.doc().get(front).arrangement;
    const sim::Vec3 centre{before.x, before.y, before.z};
    const std::optional<render::PicturePoint> middle = render::pictureOf(space.camera(), space.pictureSize(), centre);
    REQUIRE(middle.has_value());

    click(space, onView(space, *middle));
    w.window.tick();
    EXPECT((w.window.selected() == front), "clicking the panel in 3D selects it");
    EXPECT((!space.gizmo().has_value()), "with Select, the selected panel shows no gizmo");
    w.window.shelf()->action(Tool::Move)->trigger();
    const std::optional<render::Gizmo> gizmo = space.gizmo();
    REQUIRE(gizmo.has_value());

    // Take the x arrow a little way out from the middle and pull it outwards.
    const render::PicturePoint tip = *render::pictureOf(space.camera(), space.pictureSize(), centre + sim::Vec3{1.0, 0.0, 0.0});
    const double dx = tip.x - middle->x, dy = tip.y - middle->y, length = std::hypot(dx, dy);
    REQUIRE(length > 0.0);
    const auto out = [&](double pixels) { return render::PicturePoint{middle->x + dx / length * pixels * gizmo->pixel, middle->y + dy / length * pixels * gizmo->pixel}; };
    REQUIRE(render::handleAt(*gizmo, space.camera(), space.pictureSize(), out(40.0)) == render::GizmoHandle::AlongX);
    const double yaw = space.camera().yaw;
    space.press(onView(space, out(40.0)), Qt::LeftButton, {});
    for (int step = 1; step <= 5; ++step) {
        space.moveTo(onView(space, out(40.0 + 20.0 * step)), Qt::LeftButton, {});
        w.window.tick();
    }
    space.release();
    w.window.tick();
    const Arrangement after = w.doc().get(front).arrangement;
    EXPECT((after.x > before.x + 1.0), "pulling the x arrow moves the panel along x");
    EXPECT((after.y == before.y && after.z == before.z), "and not along y or z");
    EXPECT((w.window.selected() == front), "the panel stays selected");
    EXPECT((space.camera().yaw == yaw), "the camera stays where it was");

    w.window.shelf()->undo()->trigger();
    w.window.tick();
    EXPECT((w.doc().get(front).arrangement.x == before.x), "the whole drag is one step to undo");

    click(space, {2.0, 2.0});
    w.window.tick();
    EXPECT((!w.window.selected().has_value()), "a click on nothing clears the selection");
}

namespace {

// The panel nearest the camera, which looks at the body from the front, and
// where the middle of it is drawn.
std::pair<PanelId, render::PicturePoint> frontPanel(const Window& w) {
    const SpaceView& space = *w.window.space();
    PanelId front = w.doc().panels().front();
    for (PanelId p : w.doc().panels())
        if (w.doc().get(p).arrangement.z > w.doc().get(front).arrangement.z) front = p;
    const Arrangement& at = w.doc().get(front).arrangement;
    const std::optional<render::PicturePoint> middle = render::pictureOf(space.camera(), space.pictureSize(), {at.x, at.y, at.z});
    REQUIRE(middle.has_value());
    return {front, *middle};
}

void dragBy(Window& w, QPointF from, QPointF by) {
    SpaceView& space = *w.window.space();
    space.press(from, Qt::LeftButton, {});
    for (int step = 1; step <= 5; ++step) {
        space.moveTo(from + by * (step / 5.0), Qt::LeftButton, {});
        w.window.tick();
    }
}

} // namespace

TEST_CASE("in 3D the rotate gizmo turns the selected panel in space; the scale gizmo scales its shape", "[qt]")
{
    Window w;
    SpaceView& space = *w.window.space();
    const auto [front, middle] = frontPanel(w);
    w.window.select(front);
    w.window.shelf()->action(Tool::Rotate)->trigger();
    w.window.tick();
    REQUIRE((space.gizmo() && space.gizmo()->mode == session::GizmoMode::Rotate));
    const double pixel = space.gizmo()->pixel;
    const Arrangement before = w.doc().get(front).arrangement;

    // The outer ring, from its right a quarter of the way round clockwise, in steps of 15° with Ctrl.
    const auto onRing = [&](double degrees) {
        const double radians = degrees * std::numbers::pi / 180.0;
        return render::PicturePoint{middle.x + render::gizmoViewRingPixels * pixel * std::cos(radians), middle.y - render::gizmoViewRingPixels * pixel * std::sin(radians)};
    };
    REQUIRE(render::handleAt(*space.gizmo(), space.camera(), space.pictureSize(), onRing(0.0)) == render::GizmoHandle::AboutView);
    space.press(onView(space, onRing(0.0)), Qt::LeftButton, {});
    for (int step = 1; step <= 6; ++step) {
        space.moveTo(onView(space, onRing(-15.0 * step)), Qt::LeftButton, Qt::ControlModifier);
        w.window.tick();
    }
    space.release();
    w.window.tick();
    const Arrangement after = w.doc().get(front).arrangement;
    // The line of sight through the panel's anchor, from it to the eye.
    const sim::Vec3 sight = sim::normalized(space.camera().eye() - sim::Vec3{before.x, before.y, before.z});
    const sim::Vec3 expected = sim::inSpace(sim::turnedAbout(before, sight, -90.0), {1.0, 0.0, 0.0});
    EXPECT((sim::length(sim::inSpace(after, {1.0, 0.0, 0.0}) - expected) < 1e-6), "the panel turns a quarter about the line of sight");
    EXPECT((after.x == before.x && after.y == before.y && after.z == before.z), "about its anchor, which stays where it is");
    w.window.shelf()->undo()->trigger();
    w.window.tick();
    EXPECT((w.doc().get(front).arrangement == before), "the whole drag is one step to undo");

    // The ball between the rings: dragged to the right, the panel turns about the screen's up, freely.
    const render::PicturePoint ball{middle.x + 40.0 * pixel, middle.y - 40.0 * pixel};
    REQUIRE(render::handleAt(*space.gizmo(), space.camera(), space.pictureSize(), ball) == render::GizmoHandle::Free);
    space.press(onView(space, ball), Qt::LeftButton, {});
    for (int step = 1; step <= 5; ++step) {
        space.moveTo(onView(space, {ball.x + 12.0 * pixel * step, ball.y}), Qt::LeftButton, {});
        w.window.tick();
    }
    space.release();
    w.window.tick();
    const Arrangement rolled = w.doc().get(front).arrangement;
    EXPECT((std::abs(rolled.turn - before.turn) > 20.0), "dragged 60 pixels to the right, the panel turns about the vertical, freely");
    EXPECT((rolled.x == before.x && rolled.y == before.y && rolled.z == before.z), "about its anchor");
    w.window.shelf()->undo()->trigger();
    w.window.tick();

    // The scale gizmo's x handle, along the panel's own x, pulled out half as far again.
    w.window.shelf()->action(Tool::Scale)->trigger();
    w.window.tick();
    REQUIRE((space.gizmo() && space.gizmo()->mode == session::GizmoMode::Scale));
    const render::Gizmo scale = *space.gizmo();
    const render::PicturePoint tip = *render::pictureOf(space.camera(), space.pictureSize(), scale.centre + scale.ownX);
    const double dx = tip.x - middle.x, dy = tip.y - middle.y, length = std::hypot(dx, dy);
    const auto out = [&](double pixels) { return render::PicturePoint{middle.x + dx / length * pixels * pixel, middle.y + dy / length * pixels * pixel}; };
    REQUIRE(render::handleAt(scale, space.camera(), space.pictureSize(), out(40.0)) == render::GizmoHandle::AlongX);
    const std::size_t layers = w.doc().operations(front).size();
    space.press(onView(space, out(40.0)), Qt::LeftButton, {});
    for (int step = 1; step <= 4; ++step) {
        space.moveTo(onView(space, out(40.0 + 5.0 * step)), Qt::LeftButton, {});
        w.window.tick();
    }
    space.release();
    w.window.tick();
    const std::vector<OpId> operations = w.doc().operations(front);
    REQUIRE(operations.size() == layers + 1);
    const Scale* added = nullptr;
    for (OpId op : operations)
        if (const Scale* s = std::get_if<Scale>(&w.doc().get(op))) added = s;
    REQUIRE(added != nullptr);
    EXPECT((std::abs(*asNumber(added->x) - 1.5) < 0.03 && asNumber(added->y) == 1.0), "a Scale layer of about 150 % along the panel's own x");
    EXPECT((w.doc().get(front).arrangement.x == before.x), "scaling is shape: the panel's place stays");
}

TEST_CASE("in 3D the selected panel is carried by dragging it; draped cloth is pulled instead", "[qt]")
{
    Window w;
    SpaceView& space = *w.window.space();
    const auto [front, middle] = frontPanel(w);
    // A spot on the panel a little off its middle, away from the gizmo's square.
    const QPointF onPanel = onView(space, {middle.x - 30.0 * space.inPicture({1, 1}).x, middle.y + 30.0 * space.inPicture({1, 1}).x});
    click(space, onPanel);
    w.window.tick();
    REQUIRE(w.window.selected() == front);

    const Arrangement before = w.doc().get(front).arrangement;
    dragBy(w, onPanel, {40.0, 0.0});
    space.release();
    w.window.tick();
    EXPECT((w.doc().get(front).arrangement == before), "with Select, dragging the panel only selects it");
    w.window.shelf()->action(Tool::Move)->trigger();
    dragBy(w, onPanel, {40.0, 0.0});
    space.release();
    w.window.tick();
    EXPECT((w.doc().get(front).arrangement.x != before.x), "with Move, dragging the selected panel carries it");
    EXPECT((w.window.selected() == front), "and it stays selected");

    // Drape it, then drag the cloth: the cloth is held, the document stays.
    w.window.shelf()->simulate()->setChecked(true);
    for (int i = 0; i < 5; ++i) w.window.tick();
    REQUIRE(w.window.session().clothShown());
    w.window.shelf()->simulate()->setChecked(false);
    w.window.tick();
    const std::uint64_t version = w.window.session().editor().version();
    // Off the middle again, so that the press is on the cloth and not on the gizmo.
    const render::PicturePoint nowMiddle = frontPanel(w).second;
    const double scale = space.inPicture({1, 1}).x;
    dragBy(w, onView(space, {nowMiddle.x - 30.0 * scale, nowMiddle.y + 30.0 * scale}), {0.0, -40.0});
    EXPECT((w.window.session().pulling()), "while the button is held the cloth is held");
    space.release();
    w.window.tick();
    EXPECT((!w.window.session().pulling()), "let go, it is not");
    EXPECT((w.window.session().editor().version() == version), "pulling the cloth changes nothing in the document");
}

namespace {

// A skirt cut at an angle along a line: three layers, the cut using the line.
struct Hem {
    Document doc;
    PanelId skirt;
    OpId rect, line, cut;
    Hem() {
        skirt = doc.add(Panel{.name = "skirt"});
        const OpRef<Rect> r = doc.add(skirt, Rect{.width = 70_cm, .height = 60_cm});
        const OpRef<Line> l = doc.add(skirt, Line{.from = r.bottomLeft(), .angle = 25_deg});
        const OpRef<Cut> c = doc.add(skirt, Cut{.along = l.line(), .keep = Cut::Side::Left});
        rect = r.id, line = l.id, cut = c.id;
    }
};

std::size_t indexOf(const Document& doc, PanelId panel, OpId op) {
    const std::vector<OpId> made = doc.operations(panel);
    return static_cast<std::size_t>(std::find(made.begin(), made.end(), op) - made.begin());
}

} // namespace

TEST_CASE("layers are dragged to a new place, are removed, show their problem, and undo brings them back", "[qt]")
{
    Window w;
    const Hem hem;
    w.window.open(hem.doc, "hem");
    w.window.select(hem.skirt);
    AttributeEditor& editor = *w.window.attributes();
    EXPECT((editor.problemShown(hem.cut).isEmpty()), "a working cut shows no problem");

    editor.moveLayer(hem.line, 2);
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 2), "dropped above the cut, the line is applied after it");
    EXPECT((!editor.problemShown(hem.cut).isEmpty()), "the cut, before its line now, says what is wrong under it");
    w.window.shelf()->undo()->trigger();
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 1 && editor.problemShown(hem.cut).isEmpty()), "undo puts it back");

    editor.moveLayer(hem.line, 0);
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 1), "nothing goes below the base shape");
    editor.moveLayer(hem.rect, 2);
    EXPECT((indexOf(w.doc(), hem.skirt, hem.rect) == 0), "and the base shape does not move");

    editor.removeLayer(hem.line);
    EXPECT((!w.doc().contains(hem.line)), "the bin removes the layer");
    EXPECT((!editor.problemShown(hem.cut).isEmpty()), "the cut without its line shows its problem");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().contains(hem.line)), "undo brings the layer back");
}

TEST_CASE("a layer dragged by its name leaves a gap where it will land, and moves when let go", "[qt]")
{
    Window w;
    const Hem hem;
    w.window.open(hem.doc, "hem");
    w.window.select(hem.skirt);
    QApplication::processEvents();
    AttributeEditor& editor = *w.window.attributes();
    const auto row = [&](OpId layer) { return editor.findChild<QWidget*>(QString("layer%1").arg(layer.value)); };
    const auto centre = [&](QWidget* r) { return r->mapToGlobal(QPoint(r->width() / 2, r->height() / 2)); };
    REQUIRE((row(hem.line) && row(hem.cut)));
    auto* name = row(hem.line)->findChild<QPushButton*>("layerName");
    REQUIRE(name != nullptr);
    const auto at = [&](QPoint global) { return QPointF(name->mapFromGlobal(global)); };
    const QPoint start = centre(name), above = centre(row(hem.cut)) + QPoint(0, -row(hem.cut)->height());

    send(name, QEvent::MouseButtonPress, at(start), Qt::LeftButton, Qt::LeftButton, {});
    send(name, QEvent::MouseMove, at(start + QPoint(0, -20)), Qt::NoButton, Qt::LeftButton, {});
    send(name, QEvent::MouseMove, at(above), Qt::NoButton, Qt::LeftButton, {});
    EXPECT((row(hem.line)->graphicsEffect() != nullptr), "while dragging, its row stays behind as a dim gap");
    for (const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300); std::chrono::steady_clock::now() < until;) QApplication::processEvents();  // the rows slide
    EXPECT((row(hem.line)->y() < row(hem.cut)->y()), "and the gap has moved above the cut already");

    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 1), "but nothing is changed yet");
    send(name, QEvent::MouseButtonRelease, at(above), Qt::LeftButton, Qt::NoButton, {});
    QApplication::processEvents();
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 2), "letting go applies it");
    EXPECT((editor.layersShown().front() == hem.line), "and the line is the top layer");
}

TEST_CASE("a step is looked back at; the table changes nothing meanwhile, and Escape shows all steps", "[qt]")
{
    Window w;
    const Hem hem;
    w.window.open(hem.doc, "hem");
    w.window.select(hem.skirt);
    AttributeEditor& editor = *w.window.attributes();

    editor.viewStep(hem.line);
    EXPECT((w.window.viewedStep() == hem.line), "Alt+click looks back to that step");
    const std::uint64_t version = w.window.session().editor().version();
    w.window.table()->gestured(session::MovePanel{hem.skirt, 10.0, 0.0, session::DragPhase::Begin});
    EXPECT((w.window.session().editor().version() == version), "while looking back, the table moves nothing");

    editor.viewStep(hem.line);
    EXPECT((!w.window.viewedStep().has_value()), "a second click shows all steps again");
    editor.viewStep(hem.line);
    QShortcut* leave = w.window.findChild<QShortcut*>();
    REQUIRE(leave != nullptr);
    EXPECT((leave->isEnabled()), "while looking back, Escape is taken");
    emit leave->activated();
    EXPECT((!w.window.viewedStep().has_value()), "and shows all steps");
    EXPECT((!leave->isEnabled()), "then Escape is left to the others");
    editor.viewStep(hem.cut);
    EXPECT((!w.window.viewedStep().has_value()), "the last step is the panel as it is");
}

TEST_CASE("an open layer offers its choices: the side a cut keeps, a mirror applied last, an instance mirrored and its source", "[qt]")
{
    Window w;
    Hem hem;
    const PanelId other = hem.doc.add(Panel{.name = "other"});
    hem.doc.add(other, Rect{.width = 20_cm, .height = 20_cm});
    const PanelId copy = hem.doc.add(Panel{.name = "copy"});
    const OpId follows = hem.doc.add(copy, Instance{.source = hem.skirt}).id;
    w.window.open(hem.doc, "hem");
    AttributeEditor& editor = *w.window.attributes();

    w.window.select(hem.skirt);
    editor.openLayer(hem.cut);
    auto* keep = editor.findChild<QComboBox*>("keep");
    REQUIRE(keep != nullptr);
    emit keep->activated(1);
    EXPECT((std::get<Cut>(w.doc().get(hem.cut)).keep == Cut::Side::Right), "the cut now keeps the right side");
    w.window.shelf()->undo()->trigger();
    EXPECT((std::get<Cut>(w.doc().get(hem.cut)).keep == Cut::Side::Left), "and undo keeps the left again");
    EXPECT((editor.findChild<QComboBox*>("keep")->currentIndex() == 0), "and the list shows it");

    w.window.select(copy);
    editor.openLayer(follows);
    auto* mirrored = editor.findChild<QCheckBox*>("mirrored");
    REQUIRE(mirrored != nullptr);
    mirrored->click();
    EXPECT((std::get<Instance>(w.doc().get(follows)).mirrored), "the instance follows its source mirrored");
    auto* source = editor.findChild<QComboBox*>("panelReference");
    REQUIRE(source != nullptr);
    const int otherRow = source->findText("other");
    REQUIRE(otherRow >= 0);
    EXPECT((source->findText("copy") < 0), "a panel is not offered as its own source");
    emit source->activated(otherRow);
    EXPECT((std::get<Instance>(w.doc().get(follows)).source == other), "the instance now follows the other panel");
}

TEST_CASE("a mirror can be applied in its place instead of last", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    const OpId mirror = first<Mirror>(w.doc(), front);
    w.window.attributes()->openLayer(mirror);
    auto* last = w.window.attributes()->findChild<QCheckBox*>("appliedLast");
    REQUIRE(last != nullptr);
    REQUIRE(last->isChecked());
    last->click();
    EXPECT((!std::get<Mirror>(w.doc().get(mirror)).appliedLast), "unticked, the mirror runs in its place");
}

TEST_CASE("a panel's seams are listed and reversed in the Attribute Editor, deleted in the Outliner, and undone", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    AttributeEditor& editor = *w.window.attributes();
    const std::vector<SeamId> seams = editor.seamsShown();
    REQUIRE(!seams.empty());
    for (SeamId id : seams) EXPECT((w.doc().get(id).a.panel == front || w.doc().get(id).b.panel == front), "only seams that touch the panel");

    const SeamId seam = seams.front();
    const bool was = w.doc().get(seam).reversed;
    auto* card = editor.findChild<QWidget*>(QString("seam%1").arg(seam.value));
    REQUIRE(card != nullptr);
    EXPECT((card->findChild<QPushButton*>() == nullptr), "a seam's card has no Remove: the Outliner deletes seams");
    auto* twist = card->findChild<QToolButton*>("reversed");
    REQUIRE(twist != nullptr);
    twist->click();
    EXPECT((w.doc().get(seam).reversed == !was), "the twist button reverses the seam");

    const std::vector<SeamId> listed = w.window.outliner()->seamsShown();
    EXPECT((std::find(listed.begin(), listed.end(), seam) != listed.end()), "the Outliner lists the seam");
    w.window.outliner()->removeSeam(seam);
    EXPECT((!w.doc().contains(seam)), "Delete in the Outliner takes the seam away");
    EXPECT((editor.seamsShown().size() == seams.size() - 1), "and the Attribute Editor no longer lists it");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().contains(seam) && w.doc().get(seam).reversed == !was), "undo brings it back as it was");
}

TEST_CASE("a seam whose edges differ in length shows its lengths in orange; an even one does not", "[qt]")
{
    Window w;
    Document doc;
    const PanelId wide = doc.add(Panel{.name = "wide"});
    const OpRef<Rect> wideRect = doc.add(wide, Rect{.width = 30_cm, .height = 10_cm});
    const PanelId narrow = doc.add(Panel{.name = "narrow"});
    const OpRef<Rect> narrowRect = doc.add(narrow, Rect{.width = 20_cm, .height = 10_cm});
    const SeamId uneven = doc.add(Seam{.a = {wide, wideRect.top()}, .b = {narrow, narrowRect.top()}});
    const SeamId even = doc.add(Seam{.a = {wide, wideRect.left()}, .b = {narrow, narrowRect.right()}});
    w.window.open(std::move(doc), "seams");
    w.window.tick();
    w.window.select(wide);
    const auto colourOf = [&](SeamId seam) {
        auto* card = w.window.attributes()->findChild<QWidget*>(QString("seam%1").arg(seam.value));
        REQUIRE(card != nullptr);
        auto* lengths = card->findChild<QLabel*>("seamLengths");
        REQUIRE(lengths != nullptr);
        return lengths->styleSheet();
    };
    EXPECT((colourOf(uneven).contains(theme::uneven.name())), "30 cm sewn to 20 cm is shown in orange");
    EXPECT((!colourOf(even).contains(theme::uneven.name())), "10 cm sewn to 10 cm is not");
}

TEST_CASE("a change of a number updates the docks in place: seam cards, layer summaries and Outliner rows are not made again", "[qt]")
{
    Window w;
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 60_cm});
    const PanelId wide = doc.add(Panel{.name = "wide"});
    const OpRef<Rect> wideRect = doc.add(wide, Rect{.width = waist / 2, .height = 10_cm});
    const PanelId narrow = doc.add(Panel{.name = "narrow"});
    const OpRef<Rect> narrowRect = doc.add(narrow, Rect{.width = 20_cm, .height = 10_cm});
    const SeamId seam = doc.add(Seam{.a = {wide, wideRect.top()}, .b = {narrow, narrowRect.top()}});
    w.window.open(std::move(doc), "numbers");
    w.window.tick();
    w.window.select(wide);

    // Each of them is marked, to tell whether it is the same one afterwards:
    // a new widget can be given the old one's address, so addresses prove nothing.
    bool cardRemade = false;
    QWidget* card = w.window.attributes()->findChild<QWidget*>(QString("seam%1").arg(seam.value));
    REQUIRE(card != nullptr);
    QObject::connect(card, &QObject::destroyed, [&] { cardRemade = true; });
    QLabel* lengths = card->findChild<QLabel*>("seamLengths");
    REQUIRE(lengths != nullptr);
    const QString lengthsBefore = lengths->text();
    const auto summary = [&]() -> QLabel* {
        for (QLabel* label : w.window.attributes()->findChildren<QLabel*>())
            if (label->text().contains(QChar(0x00D7))) return label;  // "30 × 10 cm": the Rectangle's layer
        return nullptr;
    };
    bool summaryRemade = false;
    REQUIRE(summary() != nullptr);
    QObject::connect(summary(), &QObject::destroyed, [&] { summaryRemade = true; });
    const QString summaryBefore = summary()->text();
    const QList<QTreeWidgetItem*> rows = w.window.outliner()->findItems("wide", Qt::MatchExactly | Qt::MatchRecursive);
    REQUIRE(rows.size() == 1);
    rows[0]->setData(0, Qt::UserRole + 99, 12345);

    emit w.window.parameters()->valueTyped(waist, "64 cm");
    w.window.tick();

    EXPECT((!cardRemade), "the seam's card is the same one");
    const QWidget* cardNow = w.window.attributes()->findChild<QWidget*>(QString("seam%1").arg(seam.value));
    const QLabel* lengthsNow = cardNow ? cardNow->findChild<QLabel*>("seamLengths") : nullptr;
    EXPECT((lengthsNow != nullptr && lengthsNow->text() != lengthsBefore && lengthsNow->text().contains("32")), "and shows the new lengths");
    EXPECT((!summaryRemade), "the layer's row is the same one");
    EXPECT((summary() != nullptr && summary()->text() != summaryBefore && summary()->text().contains("32")), "and its summary shows the new number, not the old");
    const QList<QTreeWidgetItem*> rowsAfter = w.window.outliner()->findItems("wide", Qt::MatchExactly | Qt::MatchRecursive);
    EXPECT((rowsAfter.size() == 1 && rowsAfter[0]->data(0, Qt::UserRole + 99).toInt() == 12345), "the Outliner's row is the same one");

    // What does change the docks' structure still does: a new name is a new row.
    w.window.outliner()->rename(wide, "bodice");
    EXPECT((w.window.outliner()->findItems("bodice", Qt::MatchExactly | Qt::MatchRecursive).size() == 1), "a rename shows in the Outliner");
}

TEST_CASE("a panel is renamed, made base only and deleted in the Outliner", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    w.window.outliner()->rename(front, "  bodice ");
    EXPECT((w.doc().get(front).name == "bodice"), "a name typed in the Outliner renames the panel, trimmed");
    w.window.outliner()->rename(front, "   ");
    EXPECT((w.doc().get(front).name == "bodice"), "an empty name is refused and the old one stays");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(front).name == "front"), "undo brings the old name back");

    w.window.outliner()->setBaseOnly(front, true);
    EXPECT((w.doc().get(front).construction), "ticked in the menu, the panel is a base only");
    w.window.outliner()->setBaseOnly(front, false);
    EXPECT((!w.doc().get(front).construction), "unticked, it is cut from cloth again");

    const std::size_t before = w.doc().panels().size();
    w.window.outliner()->remove(front);
    EXPECT((w.doc().panels().size() == before - 1 && !w.window.selected()), "Delete removes the panel and the selection");
}

TEST_CASE("a panel's particle distance is set in the Attribute Editor, for that panel only; its counts are shown", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    const PanelId back = w.panel("back");
    EXPECT((w.window.dock("Cloth") == nullptr), "there is no Cloth tab any more");
    w.window.select(front);
    w.window.tick();
    auto* editor = w.window.attributes();
    EXPECT((editor->meshShown().contains("particles") && editor->meshShown().contains("stitches")), "the panel's particles, triangles and stitches are shown");
    const QString before = editor->meshShown();

    type(editor->particleDistance(), "1");
    EXPECT((w.doc().get(front).particleDistance == 1.0 && !w.doc().get(back).particleDistance), "typing 1 sets the selected panel's distance, not the other's");
    w.window.tick();
    EXPECT((editor->meshShown() != before), "its counts follow the finer mesh");

    type(editor->particleDistance(), "0.1");
    EXPECT((w.doc().get(front).particleDistance == 1.0 && editor->particleDistance()->property("refused").toBool()), "a distance out of range is refused, with the field marked");
    w.window.shelf()->undo()->trigger();
    EXPECT((!w.doc().get(front).particleDistance), "undo gives the default back");
}

TEST_CASE("the shelf gives a mirrored instance and bakes the selected panel", "[qt]")
{
    Window w;
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const PanelId band = doc.add(Panel{.name = "band"});
    const OpId rect = doc.add(band, Rect{.width = waist / 2, .height = 4_cm}).id;
    w.window.open(doc, "band");
    w.window.select(band);
    EXPECT((w.window.shelf()->bake()->isEnabled()), "with a panel selected the shelf offers it");

    w.window.shelf()->mirroredInstance()->trigger();
    REQUIRE(w.window.selected().has_value());
    const PanelId mirrored = *w.window.selected();
    EXPECT((!(mirrored == band)), "the new panel is selected");
    const Instance& follows = std::get<Instance>(w.doc().get(w.doc().operations(mirrored).front()));
    EXPECT((follows.source == band && follows.mirrored), "it follows the band, mirrored");
    EXPECT((w.doc().get(mirrored).x > w.doc().get(band).x), "and stands to its right on the table");

    w.window.select(band);
    w.window.shelf()->bake()->trigger();
    const Field width = fields(w.doc().get(rect))[0];
    EXPECT((asNumber(width.value) && *asNumber(width.value) == 35.0), "the width formula is baked into its number");
    w.window.shelf()->undo()->trigger();
    EXPECT((!asNumber(fields(w.doc().get(rect))[0].value)), "undo brings the formula back");
}

TEST_CASE("a parameter is renamed, refused a name that cannot be typed, and removed with its problem shown", "[qt]")
{
    Window w;
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const Param hip = doc.add(Parameter{.name = "hip", .value = waist + 20_cm});
    const PanelId band = doc.add(Panel{.name = "band"});
    const OpId rect = doc.add(band, Rect{.width = waist / 2, .height = 4_cm}).id;
    w.window.open(doc, "band");
    ParametersPanel& parameters = *w.window.parameters();

    QLineEdit* name = parameters.nameField(waist);
    REQUIRE(name != nullptr);
    name->setText("waist_girth");
    name->setModified(true);
    emit name->editingFinished();
    EXPECT((w.doc().get(waist).name == "waist_girth"), "the parameter is renamed");
    EXPECT((formatExpr(fields(w.doc().get(rect))[0].value, w.doc()).find("waist_girth") != std::string::npos), "and formulas show the new name");

    name = parameters.nameField(waist);
    name->setText("2 waist");
    name->setModified(true);
    emit name->editingFinished();
    EXPECT((w.doc().get(waist).name == "waist_girth"), "a name that cannot be typed in a formula is refused");
    EXPECT((w.window.helpLine().contains("can't be a parameter name")), "and the help line says why");

    name = parameters.nameField(waist);
    name->setText("hip");
    name->setModified(true);
    emit name->editingFinished();
    EXPECT((w.doc().get(waist).name == "waist_girth"), "a name another parameter has is refused");

    parameters.remove(waist);
    EXPECT((!w.doc().contains(waist)), "the bin removes the parameter");
    EXPECT((!parameters.problemShown(hip).isEmpty()), "a parameter that used it shows its problem in red");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().contains(waist) && parameters.problemShown(hip).isEmpty()), "undo brings it back, and the problem goes");
}

TEST_CASE("Add operation offers a shape for an empty panel, then changes; one needing an edge asks for it", "[qt]")
{
    Window w;
    Document doc;
    const PanelId piece = doc.add(Panel{.name = "piece"});
    w.window.open(doc, "piece");
    w.window.select(piece);
    AttributeEditor& editor = *w.window.attributes();
    const std::vector<session::Offer> shapes = editor.offered();
    EXPECT((std::find(shapes.begin(), shapes.end(), session::Offer::Rectangle) != shapes.end() &&
            std::find(shapes.begin(), shapes.end(), session::Offer::Curve) == shapes.end()),
           "an empty panel is offered shapes, not changes");

    editor.addOperation(session::Offer::Rectangle);
    REQUIRE(w.doc().operations(piece).size() == 1);
    EXPECT((std::holds_alternative<Rect>(w.doc().get(w.doc().operations(piece).front()))), "the rectangle is the panel's shape");
    const std::vector<session::Offer> changes = editor.offered();
    EXPECT((std::find(changes.begin(), changes.end(), session::Offer::Curve) != changes.end()), "then it is offered changes");

    editor.addOperation(session::Offer::Curve);
    const OpId curve = w.doc().operations(piece).back();
    EXPECT((std::holds_alternative<Curve>(w.doc().get(curve))), "a curve is added on top");
    EXPECT((editor.isOpen(curve)), "and its layer is open");
    EXPECT((w.window.helpLine().contains("Click an edge")), "its edge is chosen next on the table");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().operations(piece).size() == 1), "undo takes it away");
}

namespace {

// Presses a field, moves the mouse sideways in steps with the button held, and lets go.
void dragField(QWidget& field, double pixels) {
    const QPointF from(10.0, 5.0);
    const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons held) {
        QMouseEvent event(type, at, field.mapToGlobal(at), button, held, Qt::NoModifier);
        QApplication::sendEvent(&field, &event);
    };
    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 5; ++step) send(QEvent::MouseMove, from + QPointF(pixels * step / 5.0, 0.0), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, from + QPointF(pixels, 0.0), Qt::LeftButton, Qt::NoButton);
}

} // namespace

TEST_CASE("a number is dragged sideways to change it, as one undo step; a formula is not dragged", "[qt]")
{
    Window w;
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const Param hip = doc.add(Parameter{.name = "hip", .value = waist + 20_cm});
    const PanelId band = doc.add(Panel{.name = "band"});
    doc.add(band, Rect{.width = waist / 2, .height = 4_cm});
    w.window.open(doc, "band");
    ParametersPanel& parameters = *w.window.parameters();

    dragField(*parameters.field(waist), 50.0);
    EXPECT_NEAR(*asNumber(w.doc().get(waist).value), 75.0, "fifty pixels right add 5 cm (0.1 cm a pixel)");
    EXPECT_NEAR(w.window.session().editor().shapes()[hip], 95.0, "and what follows it follows");
    w.window.shelf()->undo()->trigger();
    EXPECT_NEAR(*asNumber(w.doc().get(waist).value), 70.0, "one undo takes back the whole drag");

    dragField(*parameters.field(hip), 50.0);
    EXPECT((!asNumber(w.doc().get(hip).value).has_value()), "a formula stays a formula: it is typed, not dragged");
}

namespace {

// Presses one key on a widget, with the text it types.
void press(QWidget& widget, Qt::Key key, const QString& text = {}) {
    QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(&widget, &down);
    QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QApplication::sendEvent(&widget, &up);
}

// Clicks a field open (everything selected, so typing replaces it) and types keys.
void typeKeys(ValueField& field, const QString& keys) {
    dragField(field, 0.0);
    REQUIRE(field.hasFocus());
    for (QChar key : keys) press(field, static_cast<Qt::Key>(key.unicode()), QString(key));
}

// The band with a waist, in an active window, as on a desktop.
struct Band {
    Window w;
    Param waist;
    Band() {
        Document doc;
        waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
        const PanelId band = doc.add(Panel{.name = "band"});
        doc.add(band, Rect{.width = waist / 2, .height = 4_cm});
        w.window.open(doc, "band");
        w.window.activateWindow();
        QApplication::processEvents();
        REQUIRE(w.window.isActiveWindow());
    }
    ValueField& field() { return *w.window.parameters()->field(waist); }
    double value() { return *asNumber(w.doc().get(waist).value); }
};

} // namespace

// On a desktop the window is active, and Qt gives a clicked widget focus
// before the press reaches it; a field that took that focus typed instead of
// dragging. Off-screen the window is not active unless asked, which hid this.
TEST_CASE("a number is dragged in an active window too; a click without a drag opens it for typing", "[qt]")
{
    Band b;
    dragField(b.field(), 50.0);
    EXPECT_NEAR(b.value(), 75.0, "the press starts a drag, not typing");
    EXPECT((!b.field().hasFocus()), "and the field is not open for typing after it");

    dragField(b.field(), 0.0);
    EXPECT((b.field().hasFocus() && b.field().selectedText() == b.field().text()), "a click without a drag opens the number for typing, all selected");
    EXPECT_NEAR(b.value(), 75.0, "and changes nothing");
}

TEST_CASE("Enter takes a typed number and closes the field, so the next press drags again", "[qt]")
{
    Band b;
    typeKeys(b.field(), "80");
    press(b.field(), Qt::Key_Return);
    EXPECT_NEAR(b.value(), 80.0, "Enter takes what was typed");
    EXPECT((!b.field().hasFocus()), "and closes the field");
    EXPECT((b.field().text() == "80 cm"), "which shows the number with its unit");
    dragField(b.field(), 50.0);
    EXPECT_NEAR(b.value(), 85.0, "the next press drags");
}

TEST_CASE("Escape puts back what was shown and closes the field, changing nothing", "[qt]")
{
    Band b;
    typeKeys(b.field(), "90");
    press(b.field(), Qt::Key_Escape);
    EXPECT_NEAR(b.value(), 70.0, "what was typed is not taken");
    EXPECT((b.field().text() == "70 cm" && !b.field().hasFocus()), "the value is shown again and the field is closed");
    EXPECT((!b.field().property("refused").toBool()), "nothing is marked as refused");
    dragField(b.field(), 50.0);
    EXPECT_NEAR(b.value(), 75.0, "the next press drags");
}

// A mark is only restyled when it changes; this checks that the marks still
// follow the value. That less restyling happens is not measured here: a
// custom QStyle cannot be put on a field while the window uses a style sheet.
TEST_CASE("a field's refused mark stays until the value is shown again", "[qt]")
{
    Band b;
    ValueField& field = b.field();
    type(&field, "not a number");
    EXPECT((field.property("refused").toBool()), "what does not read is marked");
    field.clearFocus();
    EXPECT((field.property("refused").toBool()), "and stays marked when the field is left");
    dragField(field, 50.0);
    EXPECT_NEAR(b.value(), 75.0, "a drag still changes the number");
    EXPECT((!field.property("refused").toBool() && field.text() == "75 cm"), "and the value shown again clears the mark");
}

TEST_CASE("the body is hidden in 3D and taken out of the simulation together; the table's grid can be hidden", "[qt]")
{
    Window w;
    w.window.showBody(false);
    EXPECT((!w.window.bodyShown()), "the body is hidden");
    const render::Scene scene = w.window.space()->sceneFor(400, 300);
    bool anyBody = false;
    for (const render::Surface& surface : scene.surfaces) anyBody = anyBody || !surface.panel.has_value();
    EXPECT((!anyBody), "and not drawn in 3D");
    w.window.tick();
    EXPECT((w.window.outliner()->bodyShown() == "avatarV1.obj"), "the Outliner still names it");
    w.window.outliner()->setBodyShown(true);  // a click on its eye
    EXPECT((w.window.bodyShown()), "and shows it again from there");
    w.window.outliner()->setBodyShown(false);
    EXPECT((!w.window.bodyShown()), "or hides it again");
    w.window.outliner()->setBodyShown(true);

    w.window.table()->setGridShown(false);
    EXPECT((!w.window.table()->gridShown()), "the grid can be hidden");
}

// ---- The table's floor ----

namespace {

// The columns of the table's picture where a grid line crosses its second row
// (the grid's own colours, as TableView draws them).
std::vector<int> gridColumns(TableView* table) {
    const QImage picture = table->grab().toImage();
    std::vector<int> columns;
    for (int x = 0; x < picture.width(); ++x) {
        const QColor at = picture.pixelColor(x, 2);
        if (at == QColor(32, 34, 39) || at == QColor(44, 47, 54)) columns.push_back(x);
    }
    return columns;
}

} // namespace

TEST_CASE("the table's floor is kept, and drawn again when the view or the grid changes", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const std::vector<int> before = gridColumns(table);
    EXPECT((before.size() > 5), "the grid's lines are drawn");
    EXPECT((gridColumns(table) == before), "painted again without a change, the same picture");

    QWheelEvent zoom(QPointF(100, 100), table->mapToGlobal(QPointF(100, 100)), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(table, &zoom);
    const std::vector<int> zoomed = gridColumns(table);
    EXPECT((!zoomed.empty() && zoomed != before), "zoomed, the lines are where the new view puts them, not where they were");

    table->setGridShown(false);
    EXPECT((gridColumns(table).empty()), "a hidden grid is not painted from the picture that had it");
    table->setGridShown(true);
    EXPECT((gridColumns(table) == zoomed), "shown again, it is as it was");
}

// ---- Lengths, seam ends, placing from the table, hints ----

TEST_CASE("the selected panel's lengths are labelled on the table; a double-click on one opens it for typing", "[qt]")
{
    Table w;
    const PanelId driven = w.panel("driven");
    w.window.select(driven);
    w.table()->grab();  // paints it
    REQUIRE(w.table()->lengthLabels().size() == 4);
    const OpRef<Rect> rect(first<Rect>(w.doc(), driven));
    const auto label = std::find_if(w.table()->lengthLabels().begin(), w.table()->lengthLabels().end(),
                                    [&](const auto& l) { return l.second == rect.bottom(); });
    REQUIRE(label != w.table()->lengthLabels().end());
    send(w.table(), QEvent::MouseButtonDblClick, label->first.center(), Qt::LeftButton, Qt::LeftButton, {});
    EXPECT((w.window.attributes()->isOpen(OpId(rect)) && w.window.attributes()->field(OpId(rect), FieldId{0}) != nullptr), "the layer that sets that length is open, to type it");

    w.window.select(std::nullopt);
    w.table()->grab();  // paints it
    EXPECT((w.table()->lengthLabels().empty()), "with nothing selected nothing is labelled");
}

TEST_CASE("the ends of a seam's edges that meet are linked; reversing the seam crosses the links", "[qt]")
{
    Table w;
    TableView* table = w.table();
    const PanelId plain = w.panel("plain"), driven = w.panel("driven");
    choose(w, Tool::Sew);
    click(table, table->toScreen(between(w.corner(plain, -1, 1), w.corner(plain, 1, 1), 0.5)));
    click(table, table->toScreen(between(w.corner(driven, -1, 1), w.corner(driven, 1, 1), 0.5)));
    REQUIRE(w.doc().seams().size() == 1);
    REQUIRE(table->seamLinks().size() == 2);
    const Vec2 wentTo = table->seamLinks()[0].second;
    emit w.window.attributes()->seamReversed(w.doc().seams().front(), true);
    REQUIRE(table->seamLinks().size() == 2);
    EXPECT((!(table->seamLinks()[0].second == wentTo)), "reversed, each end goes to the other end");
}

TEST_CASE("a panel is placed in space again where the table has it", "[qt]")
{
    Table w;
    const PanelId plain = w.panel("plain");
    w.window.select(plain);
    const double was = w.doc().get(plain).arrangement.x;
    emit w.window.attributes()->spaceTyped(plain, 0, "250");
    REQUIRE(w.doc().get(plain).arrangement.x == 250.0);
    REQUIRE(w.window.shelf()->placeFromTable()->isEnabled());
    w.window.shelf()->placeFromTable()->trigger();
    EXPECT((w.doc().get(plain).arrangement.placed && std::abs(w.doc().get(plain).arrangement.x - was) < 1e-9), "it stands where it stood when it was new");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().get(plain).arrangement.x == 250.0), "and undo puts it back");
}

TEST_CASE("an empty table says how to start; the grid's step is written in its corner", "[qt]")
{
    Window w;
    w.window.open(Document{}, "empty");
    EXPECT((w.window.helpLine().contains("table is empty")), "an empty document says how to draw the first panel");

    const QString near = w.window.table()->gridKey();
    EXPECT((near.startsWith("grid ") && near.endsWith("cm")), "the grid's step, with its unit");
    QWheelEvent away(QPointF(50, 50), w.window.table()->mapToGlobal(QPointF(50, 50)), {}, {0, -2400}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(w.window.table(), &away);
    EXPECT((w.window.table()->gridKey() != near), "zoomed far out, the grid's lines are farther apart and it says so");
}

// ---- Files, the avatar, and random input ----

TEST_CASE("every sample saves and opens unchanged; a file that cannot be read says why", "[qt]")
{
    Window w;
    // A folder of its own to write in, beside the program: a place that is
    // certainly writable (some machines keep programs from writing in the
    // system's temp folder), and gone again when the test is done.
    QTemporaryDir folder(QCoreApplication::applicationDirPath() + "/clothcad-samples-XXXXXX");
    REQUIRE(folder.isValid());
    const QString path = folder.filePath("sample.clothcad");
    for (std::size_t i = 0; i < session::samples().size(); ++i) {
        w.window.openSample(i);
        const std::string before = toText(w.doc());
        REQUIRE(w.window.saveAs(path));
        REQUIRE(w.window.openFile(path));
        EXPECT((toText(w.doc()) == before), std::string(session::samples()[i].name) + " saves and opens unchanged");
        EXPECT((w.window.session().editor().shapes().errors.empty()), "and evaluates without a problem");
    }
    const std::string kept = toText(w.doc());
    EXPECT((!w.window.openFile(folder.filePath("no such file.clothcad")) && !w.window.helpLine().isEmpty()), "a missing file is refused, with why");
    EXPECT((toText(w.doc()) == kept), "and the open document stays");
    EXPECT((!w.window.saveAs(folder.filePath("no such folder/x.clothcad")) && !w.window.helpLine().isEmpty()), "a place that cannot be written says why");
}

TEST_CASE("each sample opens on its avatar, found from any folder; one that cannot be read leaves the body", "[qt]")
{
    Window w;
    EXPECT((w.window.session().avatarPath().ends_with("avatarV1.obj")), "the T-shirt is draped on the project's avatar");
    EXPECT((w.window.session().bodySurface().triangles.size() > 1000), "its surface is there");
    EXPECT((w.window.session().bodyReady()), "and ready to collide with soon after");
    EXPECT((w.window.outliner()->bodyShown() == "avatarV1.obj"), "the Outliner names it");

    const QString started = QDir::currentPath();
    QDir::setCurrent(QDir::tempPath());  // as when the program is started from its build folder
    for (std::size_t i = 0; i < session::samples().size(); ++i) {
        w.window.openSample(i);
        const char* avatar = session::samples()[i].avatar;
        EXPECT((avatar != nullptr && w.window.session().avatarPath().ends_with(avatar)), std::string(session::samples()[i].name) + " opens on its avatar");
    }
    QDir::setCurrent(started);

    const std::string before = w.window.session().avatarPath();
    EXPECT((!w.window.loadAvatar("no/such/avatar.obj") && !w.window.helpLine().isEmpty()), "a missing avatar file is refused, with why");
    EXPECT((w.window.session().avatarPath() == before), "and the body stays");
}

TEST_CASE("every sample meshes; random clicks, drags and keys on both views and the shelf find no crash", "[qt]")
{
    Window w;
    w.window.shelf()->mesh()->setChecked(true);
    std::mt19937 random(12345);
    const auto below = [&](int n) { return static_cast<int>(random() % static_cast<unsigned>(n)); };
    const int rounds = qEnvironmentVariableIsSet("FUZZ") ? qEnvironmentVariableIntValue("FUZZ") : 60;
    const std::vector<QAction*> tools = [&] {
        std::vector<QAction*> found;
        for (const Tool tool : {Tool::Select, Tool::Move, Tool::Rotate, Tool::Scale, Tool::Rectangle, Tool::Circle, Tool::Ring, Tool::Point, Tool::Curve,
                                Tool::Line, Tool::Mirror, Tool::Cut, Tool::Split, Tool::Sew})
            found.push_back(w.window.shelf()->action(tool));
        return found;
    }();
    for (std::size_t i = 0; i < session::samples().size(); ++i) {
        w.window.openSample(i);
        w.window.tick();
        std::size_t problems = 0;
        for (const sim::PanelMesh& mesh : w.window.session().mesh()->panels) problems += !mesh.problem.empty();
        EXPECT((problems == 0 && !w.window.session().mesh()->panels.empty()), std::string(session::samples()[i].name) + ": every cloth panel meshes");

        w.window.shelf()->simulate()->setChecked(true);  // the cloth moves while everything below happens
        SpaceView& space = *w.window.space();
        TableView* table = w.window.table();
        for (int round = 0; round < rounds; ++round) {
            if (round % 7 == 0) tools[static_cast<std::size_t>(below(static_cast<int>(tools.size())))]->trigger();
            if (round % 2 == 0) {
                const QPointF at(below(std::max(table->width(), 1)), below(std::max(table->height(), 1)));
                send(table, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, round % 4 == 0 ? Qt::ShiftModifier : Qt::NoModifier);
                send(table, QEvent::MouseMove, at + QPointF(below(60) - 30, below(60) - 30), Qt::NoButton, Qt::LeftButton, {});
                send(table, QEvent::MouseButtonRelease, at + QPointF(below(60) - 30, below(60) - 30), Qt::LeftButton, Qt::NoButton, {});
                if (round % 5 == 0) send(table, QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton, {});
            } else {
                const QPointF at(below(std::max(space.width(), 1)), below(std::max(space.height(), 1)));
                const Qt::KeyboardModifiers keys = round % 3 == 0 ? Qt::AltModifier : Qt::NoModifier;
                space.press(at, round % 5 == 0 ? Qt::MiddleButton : Qt::LeftButton, keys);
                space.moveTo(at + QPointF(below(80) - 40, below(80) - 40), Qt::LeftButton, keys);
                space.release();
            }
            if (round % 11 == 0) {
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(table, &escape);
            }
            if (round % 13 == 0) w.window.shelf()->undo()->trigger();
            if (round % 17 == 0) w.window.shelf()->redo()->trigger();
            w.window.tick();
        }
        w.window.shelf()->simulate()->setChecked(false);
        INFO(session::samples()[i].name << ": " << w.doc().panels().size() << " panels, " << w.window.session().editor().shapes().errors.size() << " problems");
        SUCCEED("survived the random input");
    }
}

TEST_CASE("the layers scroll on their own, apart from the rest of the Attribute Editor", "[qt]")
{
    Window w;
    const PanelId front = w.panel("front");
    w.window.select(front);
    AttributeEditor& editor = *w.window.attributes();
    auto* list = editor.findChild<QScrollArea*>("layerScroll");
    REQUIRE(list != nullptr);
    const OpId rect = first<Rect>(w.doc(), front);
    QWidget* row = editor.findChild<QWidget*>(QString("layer%1").arg(OpId(rect).value));
    REQUIRE(row != nullptr);
    EXPECT((list->widget()->isAncestorOf(row)), "the layer rows are inside the list's own scroll area");
    EXPECT((list->minimumHeight() > 0), "which keeps some height of its own");
}
