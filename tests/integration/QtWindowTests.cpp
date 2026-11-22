// The Qt window, used through its own widgets the way a person would, without
// a screen (QT_QPA_PLATFORM=offscreen): what the document became is checked.

#include <QAction>
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
#include <QTemporaryDir>
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
#include "ClothPanel.h"
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
    for (const char* name : {"Outliner", "Parameters", "Attribute Editor", "Cloth"}) EXPECT((w.window.dock(name) != nullptr), std::string("there is a dock ") + name);
    EXPECT((w.window.outliner()->panelsShown().size() == 2), "the Outliner lists the sample's two panels");
    EXPECT((w.window.parameters()->field(w.parameter("chest"))->text() == "96 cm"), "a parameter shows its unit");
    EXPECT((w.window.space()->isVisible() && w.window.table()->isVisible()), "both views are shown, side by side");
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
    REQUIRE(w.window.attributes()->openLayer() == underarm);
    ValueField* position = w.window.attributes()->field(FieldId{0});
    REQUIRE(position != nullptr);
    std::printf("      the underarm point's position shows as \"%s\"\n", position->text().toStdString().c_str());
    EXPECT((position->text().startsWith("=")), "it is a formula, shown as one");
    type(position, "50");
    EXPECT((asNumber(std::get<AddPoint>(w.doc().get(underarm)).at.along) == 0.5), "50 in a fraction is 50 %");
    EXPECT((w.window.attributes()->field(FieldId{0})->text() == "50 %"), "and is shown so");

    const OpId rect = first<Rect>(w.doc(), front);
    w.window.attributes()->openLayer(rect);
    type(w.window.attributes()->field(FieldId{0}), "-5");
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
    w.window.shelf()->layout(ViewLayout::One)->trigger();
    EXPECT((w.window.space()->isVisible() != w.window.table()->isVisible()), "one view shows one");
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
    emit w.window.attributes()->copyRequested(front);
    EXPECT((w.doc().panels().size() == 3 && w.window.selected() != front), "Copy adds a panel and selects it");
    emit w.window.attributes()->instanceRequested(front);
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
    EXPECT((w.window.attributes()->openLayer() == rect), "the layer that sets the length opens");
    ValueField* field = w.window.attributes()->field(FieldId{0});
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

TEST_CASE("layers move up and down, are removed, show their problem, and undo brings them back", "[qt]")
{
    Window w;
    const Hem hem;
    w.window.open(hem.doc, "hem");
    w.window.select(hem.skirt);
    AttributeEditor& editor = *w.window.attributes();
    EXPECT((editor.problemShown(hem.cut).isEmpty()), "a working cut shows no problem");

    editor.moveLayer(hem.line, true);
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 2), "up moves the line after the cut");
    EXPECT((!editor.problemShown(hem.cut).isEmpty()), "the cut, before its line now, says what is wrong under it");
    w.window.shelf()->undo()->trigger();
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 1 && editor.problemShown(hem.cut).isEmpty()), "undo puts it back");

    editor.moveLayer(hem.line, false);
    EXPECT((indexOf(w.doc(), hem.skirt, hem.line) == 1), "nothing goes below the base shape");

    editor.removeLayer(hem.line);
    EXPECT((!w.doc().contains(hem.line)), "the bin removes the layer");
    EXPECT((!editor.problemShown(hem.cut).isEmpty()), "the cut without its line shows its problem");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().contains(hem.line)), "undo brings the layer back");
}

TEST_CASE("the clock shows a panel as it was at a step; the table changes nothing meanwhile", "[qt]")
{
    Window w;
    const Hem hem;
    w.window.open(hem.doc, "hem");
    w.window.select(hem.skirt);
    AttributeEditor& editor = *w.window.attributes();

    editor.viewStep(hem.line);
    EXPECT((w.window.viewedStep() == hem.line), "the clock looks back to that step");
    const std::uint64_t version = w.window.session().editor().version();
    w.window.table()->gestured(session::MovePanel{hem.skirt, 10.0, 0.0, session::DragPhase::Begin});
    EXPECT((w.window.session().editor().version() == version), "while looking back, the table moves nothing");

    editor.viewStep(hem.line);
    EXPECT((!w.window.viewedStep().has_value()), "a second click shows all steps again");
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

TEST_CASE("a panel's seams are listed; each can be reversed or removed, and undone", "[qt]")
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
    editor.reverseSeam(seam, !was);
    EXPECT((w.doc().get(seam).reversed == !was), "the tick reverses the seam");
    editor.removeSeam(seam);
    EXPECT((!w.doc().contains(seam)), "Remove takes the seam away");
    EXPECT((editor.seamsShown().size() == seams.size() - 1), "and it is no longer listed");
    w.window.shelf()->undo()->trigger();
    EXPECT((w.doc().contains(seam) && w.doc().get(seam).reversed == !was), "undo brings it back as it was");
}

TEST_CASE("a panel is made base only, given a mirrored instance, and baked", "[qt]")
{
    Window w;
    Document doc;
    const Param waist = doc.add(Parameter{.name = "waist", .value = 70_cm});
    const PanelId band = doc.add(Panel{.name = "band"});
    const OpId rect = doc.add(band, Rect{.width = waist / 2, .height = 4_cm}).id;
    w.window.open(doc, "band");
    w.window.select(band);
    AttributeEditor& editor = *w.window.attributes();

    editor.findChild<QCheckBox*>("baseOnly")->click();
    EXPECT((w.doc().get(band).construction), "ticked, the panel is a base only");
    editor.findChild<QCheckBox*>("baseOnly")->click();
    EXPECT((!w.doc().get(band).construction), "unticked, it is cut from cloth again");

    editor.findChild<QToolButton*>("mirroredInstancePanel")->click();
    REQUIRE(w.window.selected().has_value());
    const PanelId mirrored = *w.window.selected();
    EXPECT((!(mirrored == band)), "the new panel is selected");
    const Instance& follows = std::get<Instance>(w.doc().get(w.doc().operations(mirrored).front()));
    EXPECT((follows.source == band && follows.mirrored), "it follows the band, mirrored");
    EXPECT((w.doc().get(mirrored).x > w.doc().get(band).x), "and stands to its right on the table");

    w.window.select(band);
    editor.findChild<QToolButton*>("bakePanel")->click();
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
    EXPECT((editor.openLayer() == curve), "and its layer is open");
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
    auto* shown = w.window.cloth()->findChild<QCheckBox*>("bodyShown");
    REQUIRE(shown != nullptr);
    EXPECT((!shown->isChecked()), "the Cloth tab shows it hidden too");
    shown->click();
    EXPECT((w.window.bodyShown()), "and shows it again from there");

    w.window.table()->setGridShown(false);
    EXPECT((!w.window.table()->gridShown()), "the grid can be hidden");
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
    EXPECT((w.window.attributes()->openLayer() == OpId(rect)), "the layer that sets that length opens, to type it");

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
    auto* button = w.window.attributes()->findChild<QToolButton*>("placeFromTable");
    REQUIRE(button != nullptr);
    button->click();
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
    auto* body = w.window.cloth()->findChild<QLabel*>("bodyName");
    REQUIRE(body != nullptr);
    EXPECT((body->text() == "avatarV1.obj"), "the Cloth tab names it");

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
