// The window of 0.1 used the way a person would: mouse and keys go in
// through Dear ImGui, frames are built and thrown away, and what the document
// became is checked. No window or graphics card is needed.

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "ImGuiForTests.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include "clothcad/core/File.h"
#include "Samples.h"
#include "App.h"
#include "clothcad/core/Drawing.h"
#include "clothcad/core/Evaluate.h"
using namespace clothcad;
using namespace clothcad::app;
static void frames(App& app, int n) { for (int i = 0; i < n; ++i) { ImGui::NewFrame(); app.frame(); ImGui::Render(); } }
static double first(const App& app) { return evaluate(app.document())[app.document().parameters().front()]; }
static void moveTo(App& app, float x, float y) { ImGui::GetIO().AddMousePosEvent(x, y); frames(app, 2); }
static void press(App& app) { ImGui::GetIO().AddMouseButtonEvent(0, true); frames(app, 2); }
static void release(App& app) { ImGui::GetIO().AddMouseButtonEvent(0, false); frames(app, 3); }
static void click(App& app, float x, float y) { moveTo(app, x, y); press(app); release(app); }
static void drag(App& app, float x0, float y0, float x1, float y1) {
    moveTo(app, x0, y0); press(app);
    for (int i = 1; i <= 6; ++i) { ImGui::GetIO().AddMousePosEvent(x0 + (x1 - x0) * i / 6, y0 + (y1 - y0) * i / 6); frames(app, 1); }
    frames(app, 1); release(app);
}
static void key(App& app, ImGuiKey k, bool ctrl = false) {
    ImGuiIO& io = ImGui::GetIO();
    if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(k, true); frames(app, 1);
    io.AddKeyEvent(k, false);
    if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frames(app, 2);
}
// Clicks along the toolbar until the wanted tool is active.
static bool pickTool(App& app, Tool tool) {
    for (float x = 300; x < 1150; x += 7) { click(app, x, 50); if (app.tool() == tool) return true; }
    return false;
}
// Where a point of a panel (in the panel's own coordinates) is on screen.
static ImVec2 at(const App& app, PanelId panel, Vec2 local) {
    const Panel& p = app.document().get(panel);
    const Canvas::ScreenPoint s = app.canvas().toScreen({local.x + p.x, local.y + p.y});
    return {s.x, s.y};
}
template <class T> static int count(const Document& doc, PanelId panel) {
    int n = 0;
    for (OpId op : doc.operations(panel)) n += std::holds_alternative<T>(doc.get(op));
    return n;
}
template <class T> static const T& last(const Document& doc, PanelId panel) {
    const T* found = nullptr;
    for (OpId op : doc.operations(panel)) if (const T* t = std::get_if<T>(&doc.get(op))) found = t;
    return *found;
}


namespace {

// The app as these tests start it: one cloth step a frame, so that the
// motion is the same every time; the mannequin, whose measurements the
// checks know; the table alone (the 3D view has its own tests below).
struct Window {
    ImGuiForTests imgui;
    App app;
    Window() {
        app.runClothOnItsOwnThread(false);
        app.useMannequin();
        app.setShow3D(false);
        frames(app, 3);
    }
};

} // namespace

TEST_CASE("Formula fields: drag sideways, undo, type, refuse", "[window]")
{
    Window window;
    App& app = window.app;
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    const double before = first(app);
    float fieldY = -1;
    for (float y = 40; y < 200 && fieldY < 0; y += 4) {
        drag(app, 170, y, 195, y);
        if (first(app) != before) fieldY = y;
        key(app, ImGuiKey_Escape);
    }
    EXPECT((fieldY > 0 && first(app) > before), "dragging a number to the right increases it");
    key(app, ImGuiKey_Z, true);
    EXPECT((first(app) == before), "one undo takes the whole drag back");
    click(app, 170, fieldY);
    for (char c : {'4', '3'}) { io.AddInputCharacter(c); frames(app, 1); }
    key(app, ImGuiKey_Enter);
    EXPECT((first(app) == 43.0), "click, type 43, Enter sets 43");
    click(app, 170, fieldY);
    for (char c : {'-', '5'}) { io.AddInputCharacter(c); frames(app, 1); }
    key(app, ImGuiKey_Enter); frames(app, 2);
    EXPECT((first(app) == 43.0 && evaluate(doc).ok()), "typing -5 for waist is refused: it would break the rectangles");
    drag(app, 170, fieldY, -900, fieldY);
    EXPECT((first(app) > 0.0 && first(app) < 43.0 && evaluate(doc).ok()), "dragging far left stops while still above zero");
}

TEST_CASE("A corner driven by a formula can't be dragged", "[window]")
{
    Window window;
    App& app = window.app;
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    {
        const PanelId band = doc.panels().front();
        click(app, at(app, band, {5, 2}).x, at(app, band, {5, 2}).y);
        EXPECT((app.selected() && *app.selected() == band), "clicking a panel selects it");
        const std::size_t operations = doc.operations(band).size();
        const ImVec2 corner = at(app, band, {0, 0});
        drag(app, corner.x, corner.y, corner.x - 40, corner.y + 30);
        EXPECT((doc.operations(band).size() == operations), "its corner, whose width is the formula waist, does not move");
    }
}

TEST_CASE("Making a pattern on the canvas, from nothing", "[window]")
{
    Window window;
    App& app = window.app;
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    app.openEmpty();
    frames(app, 3);
    EXPECT((pickTool(app, Tool::Rectangle)), "the Rectangle tool can be picked from the toolbar");
    const double pxPerCm = app.canvas().toScreen({1, 0}).x - app.canvas().toScreen({0, 0}).x;
    const double wide = std::round(200 / pxPerCm * 2) / 2, tall = std::round(320 / pxPerCm * 2) / 2;  // drawn sizes snap to half centimetres
    drag(app, 600, 300, 800, 620);
    EXPECT((doc.panels().size() == 1), "dragging on the table draws a panel");
    if (doc.panels().size() != 1) FAIL("cannot continue");
    const PanelId panel = doc.panels().front();
    const OpId rectOp = doc.operations(panel).front();
    const OpRef<Rect> rect(rectOp);
    EXPECT((app.tool() == Tool::Select && app.selected() && *app.selected() == panel), "it is selected and the tool returns to Select");
    frames(app, 3);
    {
        const Result r = evaluate(doc);
        std::printf("      drawn: %g x %g cm\n", r[panel].length(rect.bottom()), r[panel].length(rect.left()));
        EXPECT((r.ok() && std::abs(r[panel].length(rect.bottom()) - wide) < 0.6 && std::abs(r[panel].length(rect.left()) - tall) < 0.6), "the size dragged, in real centimetres");
    }

    // Drag the panel.
    const double xBefore = doc.get(panel).x;
    const double width = evaluate(doc)[panel].length(rect.bottom()), height = evaluate(doc)[panel].length(rect.left());
    { const ImVec2 inside = at(app, panel, {width / 2, height / 2}); drag(app, inside.x, inside.y, inside.x + 60, inside.y); }
    EXPECT((doc.get(panel).x > xBefore), "dragging the panel moves it on the table");
    EXPECT((evaluate(doc)[panel].length(rect.bottom()) == width), "without changing its shape");

    // Drag a corner.
    { const ImVec2 corner = at(app, panel, {width, height}); drag(app, corner.x, corner.y, corner.x + 30, corner.y - 20); }
    EXPECT((count<AdjustPoint>(doc, panel) == 1), "dragging a corner records one adjust-point operation");
    if (count<AdjustPoint>(doc, panel) != 1) FAIL("cannot continue");
    {
        const AdjustPoint& adjust = last<AdjustPoint>(doc, panel);
        std::printf("      corner moved by %g, %g cm\n", *asNumber(adjust.dx), *asNumber(adjust.dy));
        EXPECT((adjust.corner == rect.topRight() && *asNumber(adjust.dx) > 0 && *asNumber(adjust.dy) > 0), "for that corner, right and up");
    }
    { const Vec2 now = evaluate(doc)[panel].position(rect.topRight()); const ImVec2 corner = at(app, panel, now); drag(app, corner.x, corner.y, corner.x + 10, corner.y); }
    EXPECT((count<AdjustPoint>(doc, panel) == 1), "dragging the same corner again continues the same operation");

    // Curve: click the bottom edge, then drag a handle.
    EXPECT((pickTool(app, Tool::Curve)), "the Curve tool can be picked");
    { const ImVec2 edge = at(app, panel, {width / 2, 0}); click(app, edge.x, edge.y); }
    EXPECT((count<Bezier>(doc, panel) == 1 && last<Bezier>(doc, panel).edge == rect.bottom()), "clicking an edge makes it a free curve");
    EXPECT((app.tool() == Tool::Select), "and the tool returns to Select so the handles can be dragged");
    {
        const Edge edge = evaluate(doc)[panel].edge(rect.bottom());
        const ImVec2 handle = at(app, panel, edge.cubic->control1);
        drag(app, handle.x, handle.y, handle.x, handle.y + 40);
        const Bezier& curve = last<Bezier>(doc, panel);
        std::printf("      start handle now %g, %g\n", *asNumber(curve.startHandle.x), *asNumber(curve.startHandle.y));
        EXPECT((*asNumber(curve.startHandle.y) < -2.0), "dragging a handle down changes that handle");
        EXPECT((count<Bezier>(doc, panel) == 1), "in the same operation");
    }

    // Line: left edge to right edge.
    EXPECT((pickTool(app, Tool::Line)), "the Line tool can be picked");
    {
        const Pattern shape = evaluate(doc)[panel];
        const ImVec2 a = at(app, panel, midpoint(shape, shape.edge(rect.left())));
        const ImVec2 b = at(app, panel, midpoint(shape, shape.edge(rect.right())));
        click(app, a.x, a.y);
        EXPECT((count<LineBetween>(doc, panel) == 0), "one click places the first end only");
        click(app, b.x, b.y);
    }
    EXPECT((count<LineBetween>(doc, panel) == 1 && evaluate(doc)[panel].lines.size() == 1 && evaluate(doc).ok()), "the second click draws the internal line");
    {
        const LineBetween& line = last<LineBetween>(doc, panel);
        EXPECT((line.from.edge == rect.left() && line.to.edge == rect.right()), "from the left edge to the right edge");
    }

    // Split along it, then undo.
    EXPECT((pickTool(app, Tool::Split)), "the Split tool can be picked");
    {
        const InternalLine line = evaluate(doc)[panel].lines.front();
        const ImVec2 middle = at(app, panel, {(line.start.x + line.end.x) / 2, (line.start.y + line.end.y) / 2});
        click(app, middle.x, middle.y);
    }
    EXPECT((doc.panels().size() == 2 && count<Cut>(doc, panel) == 1 && count<Offcut>(doc, doc.panels().back()) == 1), "clicking the line splits the panel in two");
    EXPECT((evaluate(doc).ok() && !evaluate(doc)[doc.panels().back()].edges.empty()), "both pieces have outlines, with no errors");
    key(app, ImGuiKey_Z, true);
    EXPECT((doc.panels().size() == 1 && count<Cut>(doc, panel) == 0), "one undo takes the whole split back");

    // Cut: click the part above the line to remove it.
    EXPECT((pickTool(app, Tool::Cut)), "the Cut tool can be picked");
    {
        const Pattern shape = evaluate(doc)[panel];
        const Vec2 upper{width / 2, height * 0.85}, lower{width / 2, height * 0.2};
        const ImVec2 above = at(app, panel, upper);
        click(app, above.x, above.y);
        const Result r = evaluate(doc);
        EXPECT((count<Cut>(doc, panel) == 1 && r.ok()), "clicking a part cuts it away");
        EXPECT((!encloses(r[panel], upper) && encloses(r[panel], lower)), "the clicked part is gone, the other part stays");
        (void)shape;
    }

    // Mirror across the left edge.
    EXPECT((pickTool(app, Tool::Mirror)), "the Mirror tool can be picked");
    {
        const Pattern shape = evaluate(doc)[panel];
        const ImVec2 edge = at(app, panel, midpoint(shape, shape.edge(rect.left())));
        click(app, edge.x, edge.y);
    }
    EXPECT((count<Mirror>(doc, panel) == 1 && evaluate(doc)[panel].axes.size() == 1 && evaluate(doc).ok()), "clicking a straight edge mirrors the panel across it");

    // A toolbar action on the selection.
    {
        const std::size_t panels = doc.panels().size();
        bool added = false;
        for (float x = 700; x < 1150 && !added; x += 7) { click(app, x, 50); added = doc.panels().size() > panels; }
        EXPECT((added && evaluate(doc).ok()), "a toolbar button adds a copy or instance beside the selection");
        if (added) EXPECT((doc.get(doc.panels().back()).x > doc.get(panel).x), "placed to its right");
    }
    for (int i = 0; i < 30; ++i) key(app, ImGuiKey_Z, true);
    EXPECT((doc.panels().empty()), "undo walks all the way back to the empty table");
}

TEST_CASE("Snap, symmetry, typing a length, circle", "[window]")
{
    Window window;
    App& app = window.app;
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    app.openEmpty();
    frames(app, 3);
    EXPECT((pickTool(app, Tool::Rectangle)), "Rectangle tool");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    drag(app, 903, 301, 1023, 517);
    io.AddKeyEvent(ImGuiMod_Ctrl, false); frames(app, 3);
    if (doc.panels().size() != 1) FAIL("cannot continue");
    const PanelId half = doc.panels().front();
    const OpRef<Rect> halfRect(doc.operations(half).front());
    {
        const Result r = evaluate(doc);
        const double w = r[half].length(halfRect.bottom()), h = r[half].length(halfRect.left());
        std::printf("      with Ctrl: %g x %g cm at %g, %g\n", w, h, doc.get(half).x, doc.get(half).y);
        const auto whole = [](double v) { return std::abs(v - std::round(v)) < 1e-9; };
        EXPECT((whole(w) && whole(h) && whole(doc.get(half).x) && whole(doc.get(half).y)), "holding Ctrl snaps the rectangle to whole grid steps");
    }
    const double hh = evaluate(doc)[half].length(halfRect.left());

    EXPECT((pickTool(app, Tool::Mirror)), "Mirror tool");
    { const ImVec2 edge = at(app, half, {0, hh / 2}); click(app, edge.x, edge.y); }
    EXPECT((count<Mirror>(doc, half) == 1 && last<Mirror>(doc, half).appliedLast), "a mirror made with the tool is applied last");
    const OpRef<Mirror> fold([&] { OpId id; for (OpId op : doc.operations(half)) if (std::holds_alternative<Mirror>(doc.get(op))) id = op; return id; }());
    frames(app, 3);
    moveTo(app, 700, 450); frames(app, 1);
    ImGui::GetIO().AddMouseWheelEvent(0, -3); frames(app, 3);

    // Double-click a length label to type it. The bottom edge's label sits just below its middle.
    {
        const Pattern shape = evaluate(doc)[half];
        // Nothing has changed the top edge yet, so it is still exactly its width setting.
        bool opened = false;
        const ImVec2 middle = at(app, half, midpoint(shape, shape.edge(halfRect.top())));
        for (float off = 6; off < 40 && !opened; off += 4) {
            moveTo(app, middle.x, middle.y - off);
            io.AddMouseButtonEvent(0, true); frames(app, 1); io.AddMouseButtonEvent(0, false); frames(app, 1);
            io.AddMouseButtonEvent(0, true); frames(app, 1); io.AddMouseButtonEvent(0, false); frames(app, 3);
            opened = io.WantTextInput;
            if (!opened) key(app, ImGuiKey_Escape);
        }
        EXPECT((opened), "double-clicking a length label opens it for typing");
        key(app, ImGuiKey_A, true);
        for (char c : {'1', '2'}) { io.AddInputCharacter(c); frames(app, 1); }
        key(app, ImGuiKey_Enter); frames(app, 2);
        std::printf("      width is now %g\n", *valueOf(doc.get(halfRect).width, evaluate(doc)));
        EXPECT((asNumber(doc.get(halfRect).width) && *asNumber(doc.get(halfRect).width) == 12.0 && evaluate(doc).ok()), "typing 12 sets the rectangle's width");
        frames(app, 3);
    }

    const auto widthNow = [&] { return evaluate(doc)[half].length(halfRect.bottom()); };
    // Drag the MIRRORED bottom corner outward (to the left): the drawn corner moves the mirrored way (to the right).
    { const ImVec2 c0 = at(app, half, {-widthNow(), 0}); {auto a=app.canvas().toScreen({0,0}), b=app.canvas().toScreen({100,0}); std::printf("      origin %g,%g ; +100cm %g,%g\n",a.x,a.y,b.x,b.y);}
      std::printf("      font %g\n", ImGui::GetFontSize());
      std::printf("      sel %d tool? corner at %g,%g display %g x %g\n", app.selected().has_value(), c0.x, c0.y, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
      const Pattern sh = evaluate(doc)[half]; for (auto& v : sh.vertices) std::printf("        v %g,%g\n", v.position.x, v.position.y); }
    { const ImVec2 corner = at(app, half, {-widthNow(), 0}); drag(app, corner.x, corner.y, corner.x - 40, corner.y); }
    {
        const Result r = evaluate(doc);
        EXPECT((count<AdjustPoint>(doc, half) == 1 && last<AdjustPoint>(doc, half).corner == halfRect.bottomRight()), "dragging a mirrored corner adjusts its original");
        const double drawn = r[half].position(halfRect.bottomRight()).x, image = r[half].position(fold.image(halfRect.bottomRight())).x;
        std::printf("      drawn corner x %g, mirrored corner x %g\n", drawn, image);
        EXPECT((r.ok() && drawn > widthNow() - 1e-9 && drawn > 12.0 && std::abs(drawn + image) < 1e-9), "both sides move, and the panel stays symmetric");
    }
    // A corner on the mirror's axis only slides along the axis.
    { const ImVec2 corner = at(app, half, {0, hh}); drag(app, corner.x, corner.y, corner.x + 50, corner.y - 30); }
    {
        const Result r = evaluate(doc);
        const Vec2 top = r[half].position(halfRect.topLeft());
        std::printf("      axis corner now at %g, %g\n", top.x, top.y);
        EXPECT((r.ok() && std::abs(top.x) < 1e-9 && top.y > hh), "a corner on the axis slides along it and cannot leave it");
        EXPECT((r[half].axes.size() == 1 && r[half].contains(fold.image(halfRect.top()))), "the mirror still holds");
    }
    // Curve on the mirrored side bends the original, so both sides curve.
    EXPECT((pickTool(app, Tool::Curve)), "Curve tool");
    { const Pattern shape = evaluate(doc)[half]; const ImVec2 edge = at(app, half, midpoint(shape, shape.edge(fold.image(halfRect.right())))); click(app, edge.x, edge.y); }
    EXPECT((count<Bezier>(doc, half) == 1 && last<Bezier>(doc, half).edge == halfRect.right()), "bending a mirrored edge bends its original");
    EXPECT((evaluate(doc).ok() && evaluate(doc)[half].edge(fold.image(halfRect.right())).cubic.has_value()), "so both sides are curves");

    // Circle: press at the centre, drag out the radius.
    EXPECT((pickTool(app, Tool::Circle)), "Circle tool");
    {
        const std::size_t panels = doc.panels().size();
        const double perCm = app.canvas().toScreen({1, 0}).x - app.canvas().toScreen({0, 0}).x;
        drag(app, 480, 700, 480 + static_cast<float>(10 * perCm), 700);
        EXPECT((doc.panels().size() == panels + 1 && count<Circle>(doc, doc.panels().back()) == 1), "dragging with the Circle tool draws a circle panel");
        const Result r = evaluate(doc);
        const PanelId circle = doc.panels().back();
        std::printf("      radius %g cm, circumference %g\n", *valueOf(last<Circle>(doc, circle).radius, r), r[circle].length(OpRef<Circle>(doc.operations(circle).front()).arc()));
        EXPECT((r.ok() && std::abs(*valueOf(last<Circle>(doc, circle).radius, r) - 10.0) < 0.6 && r[circle].edges.size() == 1), "of the radius dragged, as one closed edge");
    }
}

TEST_CASE("Arcs, turning, groups of corners", "[window]")
{
    Window window;
    App& app = window.app;
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    {
        Document d;
        const PanelId slicePanel = d.add(Panel{.name = "slice", .x = 60, .y = 60});
        d.add(slicePanel, Circle{.radius = 10_cm, .sweep = 90_deg});
        const PanelId boxPanel = d.add(Panel{.name = "box", .x = 100, .y = 40});
        d.add(boxPanel, Rect{.width = 20_cm, .height = 20_cm});
        app.open(std::move(d), "test");
    }
    frames(app, 3);
    const PanelId slice = doc.panels()[0], boxP = doc.panels()[1];
    const OpRef<Circle> sliceCircle(doc.operations(slice).front());
    const OpRef<Rect> boxRect(doc.operations(boxP).front());
    {
        click(app, 5, 5);  // nothing selected
        const ImVec2 inside = at(app, slice, {0, -5});
        click(app, inside.x, inside.y);
        EXPECT((app.selected() && *app.selected() == slice), "a slice can be selected");
        // Bend its arc: the free curve starts out following it.
        const double arcBefore = evaluate(doc)[slice].length(sliceCircle.arc());
        EXPECT((pickTool(app, Tool::Curve)), "Curve tool");
        { const Pattern shape = evaluate(doc)[slice]; const ImVec2 edge = at(app, slice, midpoint(shape, shape.edge(sliceCircle.arc()))); click(app, edge.x, edge.y); }
        EXPECT((count<Bezier>(doc, slice) == 1), "bending an arc makes a free curve");
        const Pattern bent = evaluate(doc)[slice];
        std::printf("      arc %g cm, free curve %g cm\n", arcBefore, bent.length(sliceCircle.arc()));
        EXPECT((evaluate(doc).ok() && bent.edge(sliceCircle.arc()).cubic.has_value() && std::abs(bent.length(sliceCircle.arc()) - arcBefore) < 0.02 * arcBefore), "which follows the arc it replaced");
        key(app, ImGuiKey_Z, true); frames(app, 2);
        EXPECT((count<Bezier>(doc, slice) == 0), "undo takes the curve away");
        // Move a corner of the arc.
        const Vec2 end = evaluate(doc)[slice].position(sliceCircle.end());
        const ImVec2 from = at(app, slice, end);
        drag(app, from.x, from.y, from.x + 30, from.y + 20);
        const Result r = evaluate(doc);
        EXPECT((count<AdjustPoint>(doc, slice) == 1 && r.ok()), "a corner of a slice can be dragged");
        EXPECT((std::abs(r[slice].edge(sliceCircle.arc()).arc->sweep - 90.0) < 1e-6 && r[slice].position(sliceCircle.end()).x > end.x + 1), "the arc follows it and keeps its angle");
    }
    // A whole circle's single point cannot be moved, and says so.
    {
        Document d;
        const PanelId disc = d.add(Panel{.name = "disc", .x = 60, .y = 60});
        d.add(disc, Circle{.radius = 10_cm});
        app.open(std::move(d), "test");
        frames(app, 3);
        const PanelId only = doc.panels().front();
        const ImVec2 inside = at(app, only, {0, 0});
        click(app, inside.x, inside.y);
        const Vec2 point = evaluate(doc)[only].vertices.front().position;
        const ImVec2 from = at(app, only, point);
        drag(app, from.x, from.y, from.x + 30, from.y);
        EXPECT((count<AdjustPoint>(doc, only) == 0 && evaluate(doc).ok()), "the only point of a whole circle stays put");
    }
    {
        Document d;
        const PanelId b = d.add(Panel{.name = "box", .x = 100, .y = 40});
        d.add(b, Rect{.width = 20_cm, .height = 20_cm});
        app.open(std::move(d), "test");
        frames(app, 3);
    }
    const PanelId turnedBox = doc.panels().front();
    const OpRef<Rect> turnedRect(doc.operations(turnedBox).front());
    {
        const ImVec2 inside = at(app, turnedBox, {10, 10});
        click(app, inside.x, inside.y);
        const Panel before = doc.get(turnedBox);
        const Canvas::ScreenPoint top = app.canvas().toScreen({before.x + 10, before.y + 20});
        const Canvas::ScreenPoint centre = app.canvas().toScreen({before.x + 10, before.y + 10});
        const float knobY = top.y - 30.0f;
        // Turn it from "above the centre" to "right of the centre": minus 90 degrees.
        const float radius = centre.y - knobY;
        drag(app, top.x, knobY, top.x + radius, centre.y);
        const Panel after = doc.get(turnedBox);
        std::printf("      angle %g, origin %g, %g\n", after.angle, after.x, after.y);
        EXPECT((std::abs(after.angle + 90.0) < 1.5), "dragging the knob turns the panel");
        const Vec2 middle = turned(Vec2{10, 10}, after.angle);
        EXPECT((std::abs(after.x + middle.x - (before.x + 10)) < 0.3 && std::abs(after.y + middle.y - (before.y + 10)) < 0.3), "about the centre of its box, which stays put");
        EXPECT((evaluate(doc).ok() && doc.operations(turnedBox).size() == 1), "turning only places the panel");
    }
    {
        // On the turned panel a drag to the right on screen is "up" in the panel's own coordinates (it is turned minus 90).
        const Panel placed = doc.get(turnedBox);
        const auto corner = [&](CornerRef c) { const Vec2 p = turned(evaluate(doc)[turnedBox].position(c), placed.angle); return app.canvas().toScreen({placed.x + p.x, placed.y + p.y}); };
        const Canvas::ScreenPoint from = corner(turnedRect.topRight());
        drag(app, from.x, from.y, from.x + 40, from.y);
        const AdjustPoint& adjust = last<AdjustPoint>(doc, turnedBox);
        const double dx = asNumber(adjust.dx).value_or(99), dy = asNumber(adjust.dy).value_or(99);
        std::printf("      own-coordinates move %g, %g\n", dx, dy);
        EXPECT((count<AdjustPoint>(doc, turnedBox) == 1 && std::abs(dx) < 0.2 && dy > 1.0), "dragging a corner of a turned panel moves it the way the cursor went");
        EXPECT((app.selectedCorners().size() == 1), "and selects it");
    }
    {
        const Panel placed = doc.get(turnedBox);
        const auto corner = [&](CornerRef c) { const Vec2 p = turned(evaluate(doc)[turnedBox].position(c), placed.angle); return app.canvas().toScreen({placed.x + p.x, placed.y + p.y}); };
        const Canvas::ScreenPoint a = corner(turnedRect.bottomLeft()), b = corner(turnedRect.bottomRight());
        click(app, a.x, a.y);  // a plain click selects just that corner
        EXPECT((app.selectedCorners().size() == 1), "a plain click on a corner selects only it");
        io.AddKeyEvent(ImGuiMod_Shift, true);
        click(app, b.x, b.y);
        io.AddKeyEvent(ImGuiMod_Shift, false); frames(app, 2);
        EXPECT((app.selectedCorners().size() == 2), "Shift-click adds a second corner to the selection");
        const int adjustments = count<AdjustPoint>(doc, turnedBox);
        const Result r0 = evaluate(doc);
        const Vec2 a0 = r0[turnedBox].position(turnedRect.bottomLeft()), b0 = r0[turnedBox].position(turnedRect.bottomRight());
        drag(app, a.x, a.y, a.x + 25, a.y + 25);
        const Result r1 = evaluate(doc);
        const Vec2 a1 = r1[turnedBox].position(turnedRect.bottomLeft()), b1 = r1[turnedBox].position(turnedRect.bottomRight());
        std::printf("      moved %g, %g and %g, %g\n", a1.x - a0.x, a1.y - a0.y, b1.x - b0.x, b1.y - b0.y);
        EXPECT((count<AdjustPoint>(doc, turnedBox) == adjustments + 2 && r1.ok()), "dragging one of them moves both");
        EXPECT((std::abs((a1.x - a0.x) - (b1.x - b0.x)) < 1e-6 && std::abs((a1.y - a0.y) - (b1.y - b0.y)) < 1e-6 && std::abs(a1.x - a0.x) + std::abs(a1.y - a0.y) > 1.0), "by the same amount");
        key(app, ImGuiKey_Z, true); frames(app, 2);
        EXPECT((count<AdjustPoint>(doc, turnedBox) == adjustments), "and one undo takes the group move back");
        // A box dragged over everything selects every corner.
        const Canvas::ScreenPoint c = corner(turnedRect.topRight()), d2 = corner(turnedRect.topLeft());
        const float x0 = std::min({a.x, b.x, c.x, d2.x}) - 25, x1 = std::max({a.x, b.x, c.x, d2.x}) + 25;
        const float y0 = std::min({a.y, b.y, c.y, d2.y}) - 25, y1 = std::max({a.y, b.y, c.y, d2.y}) + 25;
        io.AddKeyEvent(ImGuiMod_Shift, true);
        drag(app, x0, y0, x1, y1);
        io.AddKeyEvent(ImGuiMod_Shift, false); frames(app, 2);
        EXPECT((app.selectedCorners().size() == 4), "Shift-dragging a box selects the corners inside it");
        key(app, ImGuiKey_Escape); frames(app, 2);
        EXPECT((app.selectedCorners().empty()), "Esc clears the selection");
    }
}

TEST_CASE("Sewing", "[window]")
{
    Window window;
    App& app = window.app;
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    {
        Document d;
        const PanelId a = d.add(Panel{.name = "front", .x = 40, .y = 40});
        d.add(a, Rect{.width = 20_cm, .height = 30_cm});
        const PanelId b = d.add(Panel{.name = "back", .x = 80, .y = 40});
        d.add(b, Rect{.width = 25_cm, .height = 30_cm});
        app.open(std::move(d), "test");
        frames(app, 3);
        const PanelId front = doc.panels()[0], back = doc.panels()[1];
        const OpRef<Rect> frontRect(doc.operations(front).front()), backRect(doc.operations(back).front());
        EXPECT((pickTool(app, Tool::Sew)), "Sew tool");
        const ImVec2 first = at(app, front, {20, 15}), second = at(app, back, {0, 15});
        click(app, first.x, first.y);
        EXPECT((doc.seams().empty()), "one click sews nothing yet");
        click(app, second.x, second.y);
        frames(app, 2);
        EXPECT((doc.seams().size() == 1), "two clicks on two edges make a seam");
        if (doc.seams().size() == 1) {
            const Seam& seam = doc.get(doc.seams().front());
            EXPECT((seam.a == SeamSide{front, frontRect.right()} && seam.b == SeamSide{back, backRect.left()}), "between the edges clicked");
            EXPECT((evaluate(doc).seams.front().sewn()), "and it is sewn");
        }
        click(app, first.x, first.y); click(app, second.x, second.y); frames(app, 2);
        EXPECT((doc.seams().size() == 1), "the same two edges are not sewn twice");
        key(app, ImGuiKey_Escape); frames(app, 2);
        for (int i = 0; i < 300; ++i) frames(app, 1);  // the details pane draws its seam list without trouble
        key(app, ImGuiKey_Z, true); frames(app, 2);
        EXPECT((doc.seams().empty()), "undo unpicks the seam");
    }
}

TEST_CASE("Files", "[window]")
{
    Window window;
    App& app = window.app;
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    for (const Sample& sample : samples()) {
        const Document built = sample.build();
        const std::string path = std::string("/tmp/clothcad_test_") + std::to_string(&sample - samples().data()) + ".clothcad";
        const std::string problem = save(built, path);
        std::variant<Document, FileError> read = clothcad::open(path);
        const bool same = problem.empty() && std::holds_alternative<Document>(read) && toText(std::get<Document>(read)) == toText(built);
        EXPECT((same), (std::string(sample.name) + " saves and opens unchanged").c_str());
        if (same) {
            const Result a = evaluate(built), b = evaluate(std::get<Document>(read));
            EXPECT((a.errors.size() == b.errors.size() && a.panels.size() == b.panels.size()), "and evaluates the same");
            app.open(std::move(std::get<Document>(read)), "opened");
            frames(app, 3);
        }
    }

    {
        app.open(samples().front());
        frames(app, 3);
        std::remove("/tmp/clothcad_typed.clothcad");
        key(app, ImGuiKey_S, true); frames(app, 3);
        EXPECT((io.WantTextInput), "Ctrl+S on an unsaved document asks for a file name");
        key(app, ImGuiKey_A, true);
        for (const char* c = "/tmp/clothcad_typed.clothcad"; *c; ++c) { io.AddInputCharacter(*c); frames(app, 1); }
        key(app, ImGuiKey_Enter); frames(app, 3);
        std::variant<Document, FileError> read = clothcad::open("/tmp/clothcad_typed.clothcad");
        EXPECT((std::holds_alternative<Document>(read) && toText(std::get<Document>(read)) == toText(doc)), "typing a name and Enter saves it there");
        EXPECT((!io.WantTextInput), "and the question closes");
    }
}

TEST_CASE("Meshing every example, and drawing the mesh", "[window]")
{
    Window window;
    App& app = window.app;
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    app.setShowMesh(true);
    for (const Sample& sample : samples()) {
        app.open(sample);
        frames(app, 3);
        std::size_t particles = 0, triangles = 0, problems = 0;
        for (const sim::PanelMesh& mesh : app.mesh().panels) { particles += mesh.points.size(); triangles += mesh.triangles.size(); problems += !mesh.problem.empty(); }
        std::printf("      %s: %zu panels, %zu particles, %zu triangles, %zu without a mesh\n", sample.name, app.mesh().panels.size(), particles, triangles, problems);
        EXPECT((!app.mesh().panels.empty() && triangles > 0 && problems == 0), "every cloth panel of the example meshes");
    }
}

TEST_CASE("The 3D view", "[window]")
{
    Window window;
    App& app = window.app;
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    app.setShow3D(true);
    app.open(samples().front());
    frames(app, 4);
    {
        EXPECT((app.placement().size() == app.mesh().panels.size() && !app.placement().empty()), "every cloth panel has a place in space");
        const PanelId firstPanel = app.mesh().panels.front().panel;  // not a reference into the mesh: the mesh is made again when the document changes
        const std::vector<sim::Vec3>& positions = app.placement().front();
        sim::Vec3 middle;
        bool allSeen = true;
        float lowX = 1e9f, highX = -1e9f;
        for (const sim::Vec3& p : positions) {
            middle = middle + p * (1.0 / positions.size());
            const auto at = app.view3D().toScreen(p);
            allSeen = allSeen && at && at->x > 0 && at->x < 1500 && at->y > 0 && at->y < 900;
            if (at) { lowX = std::min(lowX, at->x); highX = std::max(highX, at->x); }
        }
        const float tableLeft = app.canvas().toScreen({0, 0}).x;  // somewhere on the table, which is to the right
        std::printf("      panel spans x %g..%g on screen; table origin at x %g\n", lowX, highX, tableLeft);
        EXPECT((allSeen), "the fitted camera shows the whole first panel");
        const auto centre = app.view3D().toScreen(middle);
        click(app, 330, 850);
        EXPECT((!app.selected()), "a click on empty space in 3D selects nothing");
        click(app, centre->x, centre->y);
        EXPECT((app.selected() && *app.selected() == firstPanel), "a click on a panel in 3D selects it");
        const double yaw = app.view3D().yaw(), far = app.view3D().distance();
        // Dragging the selected panel carries it through space; the camera stays.
        EXPECT((!doc.get(firstPanel).arrangement.placed), "a new panel is not arranged yet");
        drag(app, centre->x, centre->y, centre->x + 60, centre->y - 30);
        const Arrangement carried = doc.get(firstPanel).arrangement;
        std::printf("      carried to %g, %g, %g\n", carried.x, carried.y, carried.z);
        EXPECT((carried.placed && app.view3D().yaw() == yaw), "dragging the selected panel carries it and leaves the camera alone");
        const auto after = app.view3D().toScreen({carried.x, carried.y, carried.z});
        EXPECT((after && std::abs(after->x - (centre->x + 60)) < 6 && std::abs(after->y - (centre->y - 30)) < 6), "its centre follows the cursor");
        key(app, ImGuiKey_Z, true); frames(app, 2);
        EXPECT((!doc.get(firstPanel).arrangement.placed), "one undo puts it back");
        drag(app, 330, 850, 390, 860);
        EXPECT((std::abs(app.view3D().yaw() - yaw) > 5.0), "dragging empty space orbits the camera");
        EXPECT((app.selected() && *app.selected() == firstPanel), "and a drag is not a click");
        moveTo(app, centre->x, centre->y); io.AddMouseWheelEvent(0, 2); frames(app, 2);
        EXPECT((app.view3D().distance() < far), "the wheel zooms in");
        // The table beside it still works.
        const PanelId second = doc.panels().back();
        const Result r = evaluate(doc);
        const auto box = bounds(r[second]);
        const ImVec2 onTable = at(app, second, {(box->min.x + box->max.x) / 2, (box->min.y + box->max.y) / 2});
        click(app, onTable.x, onTable.y);
        EXPECT((app.selected() && *app.selected() == second), "and the table beside it still selects panels");
    }
    app.setShowMesh(true);
}

TEST_CASE("Simulation", "[window]")
{
    Window window;
    App& app = window.app;
    app.setShow3D(true);  // these checks are about the cloth in space
    app.setShowMesh(true);
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    {
        Document d;
        const PanelId a = d.add(Panel{.name = "front", .x = 60, .y = 40});
        const OpRef<Rect> ra = d.add(a, Rect{.width = 42_cm, .height = 10_cm});
        const PanelId b = d.add(Panel{.name = "back", .x = 120, .y = 40});
        const OpRef<Rect> rb = d.add(b, Rect{.width = 42_cm, .height = 10_cm});
        d.add(Seam{.a = {a, ra.right()}, .b = {b, rb.left()}});
        d.add(Seam{.a = {a, ra.left()}, .b = {b, rb.right()}});
        d.arrange(a, Arrangement{.placed = true, .x = 0, .y = 105, .z = 22, .wrap = 120});
        d.arrange(b, Arrangement{.placed = true, .x = 0, .y = 105, .z = -22, .turn = 180, .wrap = 120});
        app.open(std::move(d), "tube");
    }
    frames(app, 3);
    {
        EXPECT((!app.simulatedTime() && !app.simulating()), "a newly opened document is arranged, not simulated");
        const sim::Placement before = app.placement();
        app.setSimulating(true);
        const auto started = std::chrono::steady_clock::now();
        frames(app, 180);
        const double each = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() / 180;
        std::size_t particles = 0;
        for (const auto& panel : app.placement()) particles += panel.size();
        std::printf("      %zu particles: %.1f ms a frame, drawing included\n", particles, each);
        EXPECT((app.simulatedTime() && std::abs(*app.simulatedTime() - 3.0) < 1e-6), "180 frames are 3 seconds of cloth");
        double widest = 0, lowest = 1e9, highest = -1e9, deepest = 1e9;
        const sim::Body body = sim::mannequin();
        for (const sim::Stitch& st : app.mesh().stitches) widest = std::max(widest, sim::length(app.placement()[st.panelA][st.pointA] - app.placement()[st.panelB][st.pointB]));
        for (const auto& panel : app.placement()) for (const sim::Vec3& p : panel) { lowest = std::min(lowest, p.y); highest = std::max(highest, p.y); deepest = std::min(deepest, sim::distanceTo(body, p)); }
        std::printf("      after 3 s: widest stitch %.2f cm, cloth y %.1f..%.1f, nearest to the body %.2f cm\n", widest, lowest, highest, deepest);
        EXPECT((widest < 0.2), "the side seams have closed around the body");
        EXPECT((deepest > 0.1), "no cloth is inside the body");
        EXPECT((highest > 80.0), "the tube hangs on the body instead of lying on the floor");
        app.setSimulating(false);
        const double paused = *app.simulatedTime();
        frames(app, 5);
        EXPECT((app.simulatedTime() && *app.simulatedTime() == paused), "pausing keeps the cloth as it is");
        // Changing the document keeps the cloth draped.
        const PanelId front = doc.panels().front();
        const ImVec2 onTable = at(app, front, {21, 5});
        drag(app, onTable.x, onTable.y, onTable.x + 30, onTable.y);  // move an arranged panel on the table: a change, but not to the cloth
        frames(app, 2);
        double stillHighest = -1e9;
        for (const auto& panel : app.placement()) for (const sim::Vec3& p : panel) stillHighest = std::max(stillHighest, p.y);
        EXPECT((app.simulatedTime() && *app.simulatedTime() == paused && std::abs(stillHighest - highest) < 0.05), "a change to the document leaves the cloth draped where it was");
        // Carrying a panel somewhere else in 3D starts that panel again from there.
        click(app, 5, 5);
        const auto grab = app.view3D().toScreen(app.placement().front()[app.placement().front().size() / 2]);
        click(app, grab->x, grab->y);
        EXPECT((app.selected().has_value()), "a draped panel can be picked in 3D");
        const PanelId picked = *app.selected();
        const Arrangement was = doc.get(picked).arrangement;
        drag(app, grab->x, grab->y, grab->x + 40, grab->y - 60);
        frames(app, 2);
        EXPECT((!(doc.get(picked).arrangement == was) && app.simulatedTime().has_value()), "carrying it changes its arrangement and keeps the simulation");
        app.setSimulating(true);
        frames(app, 240);
        double widestAgain = 0;
        for (const sim::Stitch& st : app.mesh().stitches) widestAgain = std::max(widestAgain, sim::length(app.placement()[st.panelA][st.pointA] - app.placement()[st.panelB][st.pointB]));
        std::printf("      after carrying and 4 more seconds: widest stitch %.2f cm\n", widestAgain);
        EXPECT((widestAgain < 0.3), "and its seams draw closed again");
    }
    app.setSimulating(false);
}

TEST_CASE("The avatar from a file, and the picture", "[window]")
{
    Window window;
    App& app = window.app;
    app.setShow3D(true);  // these checks are about the cloth in space
    app.setShowMesh(true);
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    {
        EXPECT((!app.loadAvatar("no/such/file.obj").empty()), "a missing avatar file is reported and changes nothing");
        const std::string problem = app.loadAvatar(std::getenv("AVATAR") ? std::getenv("AVATAR") : "assets/avatarV1.obj");
        EXPECT((problem.empty()), "the project's avatar loads");
        if (problem.empty()) {
            Document d;
            const PanelId a = d.add(Panel{.name = "front", .x = 60, .y = 40});
            const OpRef<Rect> ra = d.add(a, Rect{.width = 44_cm, .height = 30_cm});
            const PanelId b = d.add(Panel{.name = "back", .x = 120, .y = 40});
            const OpRef<Rect> rb = d.add(b, Rect{.width = 44_cm, .height = 30_cm});
            d.add(Seam{.a = {a, ra.right()}, .b = {b, rb.left()}});
            d.add(Seam{.a = {a, ra.left()}, .b = {b, rb.right()}});
            d.arrange(a, Arrangement{.placed = true, .x = 0, .y = 108, .z = 24, .wrap = 110});
            d.arrange(b, Arrangement{.placed = true, .x = 0, .y = 108, .z = -24, .turn = 180, .wrap = 110});
            app.open(std::move(d), "top");
            int waited = 0;
            while (!app.bodyReady() && waited++ < 5000) frames(app, 1);
            EXPECT((app.bodyReady()), "its collision shape is worked out in the background");
            app.setSimulating(true);
            frames(app, 240);
            double widest = 0, lowest = 1e9, highest = -1e9;
            for (const sim::Stitch& st : app.mesh().stitches) widest = std::max(widest, sim::length(app.placement()[st.panelA][st.pointA] - app.placement()[st.panelB][st.pointB]));
            for (const auto& panel : app.placement()) for (const sim::Vec3& p : panel) { lowest = std::min(lowest, p.y); highest = std::max(highest, p.y); }
            std::printf("      a tube on the avatar after 4 s: widest stitch %.2f cm, cloth y %.1f..%.1f\n", widest, lowest, highest);
            EXPECT((widest < 0.3 && lowest > 40.0), "a tube sewn around the avatar closes and stays on it");
            // The picture has the cloth in front of the body where it is in front, and the body's colour elsewhere.
            const auto& picture = app.view3D().picture();
            std::size_t painted = 0;
            for (std::uint32_t c : picture) painted += c != IM_COL32(24, 27, 34, 255);
            EXPECT((app.view3D().pictureWidth() > 100 && painted > picture.size() / 200), "the 3D picture is drawn pixel by pixel");
            app.setSimulating(false);
        }
        app.useMannequin();
        frames(app, 2);
    }
}

TEST_CASE("Every example draws; random input finds no crash", "[window]")
{
    Window window;
    App& app = window.app;
    app.setShow3D(true);  // these checks are about the cloth in space
    app.setShowMesh(true);
    ImGuiIO& io = ImGui::GetIO();
    const Document& doc = app.document();
    (void)io;
    (void)doc;
    std::srand(12345);
    for (const Sample& sample : samples()) {
        app.open(sample);
        frames(app, 3);
        app.setSimulating(true);  // the cloth moves and is carried over while everything below happens
        io.AddMousePosEvent(750, 450); frames(app, 2);
        io.AddMouseWheelEvent(0, 2); frames(app, 2);
        for (int i = 0; i < (std::getenv("FUZZ") ? std::atoi(std::getenv("FUZZ")) : 120); ++i) {
            const bool onCanvas = i % 2 == 0;
            const float x = onCanvas ? 300.0f + std::rand() % 850 : static_cast<float>(std::rand() % 1500);
            const float y = static_cast<float>(std::rand() % 900);
            io.AddMousePosEvent(x, y); frames(app, 1);
            io.AddMouseButtonEvent(0, true); frames(app, 1);
            if (i % 3 == 0) { io.AddMousePosEvent(x + 15, y - 12); frames(app, 1); io.AddMousePosEvent(x + 40, y + 9); frames(app, 1); }
            io.AddMouseButtonEvent(0, false); frames(app, 1);
            if (i % 5 == 0) { io.AddInputCharacter('7'); frames(app, 1); key(app, ImGuiKey_Enter); }
            if (i % 13 == 0) key(app, ImGuiKey_Escape);
            if (i % 17 == 0) key(app, ImGuiKey_Z, true);
            if (i % 9 == 0) click(app, 300.0f + std::rand() % 850, 50);  // a toolbar button
        }
        const Result result = evaluate(app.document());
        INFO(sample.name << ": " << app.document().panels().size() << " panels, " << result.errors.size() << " problems");
        SUCCEED("survived the random input");
    }
}
