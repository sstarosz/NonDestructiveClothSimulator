// The details pane and the 3D gizmo, used the way a person would: mouse and keys go in through
// Dear ImGui, frames are built and thrown away, and what the document became
// is checked. No window or graphics card is needed.

#include <string>

#include <imgui.h>

#include "App.h"
#include "Expect.h"
#include "ImGuiForTests.h"

#include <catch2/catch_test_macros.hpp>

using namespace clothcad;
using namespace clothcad::app;

namespace {

void frame(App& app) {
    ImGui::NewFrame();
    app.frame();
    ImGui::Render();
}

void frames(App& app, int count) {
    for (int i = 0; i < count; ++i) frame(app);
}

void key(App& app, ImGuiKey which) {
    ImGui::GetIO().AddKeyEvent(which, true);
    frame(app);
    ImGui::GetIO().AddKeyEvent(which, false);
    frame(app);
}

// One panel on the table, arranged in space.
Document onePanel() {
    Document doc;
    const PanelId panel = doc.add(Panel{.name = "front", .x = 20, .y = 20});
    doc.add(panel, Rect{.width = 40_cm, .height = 50_cm});
    doc.arrange(panel, Arrangement{.placed = true, .x = 0, .y = 90, .z = 26});
    return doc;
}


// Drags with the left button from one place on the screen to another, in steps.
void drag(App& app, ImVec2 from, ImVec2 to) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(from.x, from.y);
    frame(app);
    io.AddMouseButtonEvent(0, true);
    frame(app);
    for (int step = 1; step <= 8; ++step) {
        const float t = step / 8.0f;
        io.AddMousePosEvent(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
        frame(app);
    }
    io.AddMouseButtonEvent(0, false);
    frames(app, 2);
}

// An app with one panel arranged in space, selected, and the 3D view shown.
PanelId selectTheOnePanel(App& app) {
    ImGuiIO& io = ImGui::GetIO();
    app.runClothOnItsOwnThread(false);
    app.useMannequin();
    app.open(onePanel(), "one panel");
    app.setShow3D(true);
    frames(app, 3);
    const PanelId panel = app.document().panels().front();
    const Panel& placed = app.document().get(panel);
    const Canvas::ScreenPoint onPanel = app.canvas().toScreen({placed.x + 20, placed.y + 25});
    io.AddMousePosEvent(onPanel.x, onPanel.y);
    frame(app);
    io.AddMouseButtonEvent(0, true);
    frame(app);
    io.AddMouseButtonEvent(0, false);
    frames(app, 2);
    EXPECT((app.selected() == panel), "clicking the panel on the table selects it");
    return panel;
}



} // namespace

// A single click on an "In space" value opens it for typing, as the formula
// fields do. It used to need a double click, so a click often did nothing.
TEST_CASE("one click types a place in space", "[window]")
{
    const ImGuiForTests imgui(ImVec2(3840, 2160), 2.0f);
    ImGuiIO& io = ImGui::GetIO();
    App app;
    app.runClothOnItsOwnThread(false);
    app.useMannequin();
    app.open(onePanel(), "one panel");
    app.setShow3D(true);
    frames(app, 3);

    // Select the panel by clicking it on the table.
    const PanelId panel = app.document().panels().front();
    const Panel& placed = app.document().get(panel);
    const Canvas::ScreenPoint onPanel = app.canvas().toScreen({placed.x + 20, placed.y + 25});
    io.AddMousePosEvent(onPanel.x, onPanel.y);
    frame(app);
    io.AddMouseButtonEvent(0, true);
    frame(app);
    io.AddMouseButtonEvent(0, false);
    frames(app, 2);
    EXPECT((app.selected() == panel), "clicking the panel on the table selects it");

    // The first number field from the top of the details pane is "Across (x)".
    const float x = io.DisplaySize.x - 6.0f * ImGui::GetFontSize();
    float found = -1.0f;
    for (float y = 0.0f; y < io.DisplaySize.y && found < 0.0f; y += 4.0f) {
        io.AddMousePosEvent(x, y);
        frame(app);
        if (ImGui::GetMouseCursor() == ImGuiMouseCursor_ResizeEW) found = y;
    }
    EXPECT((found >= 0.0f), "the details pane shows where the panel is in space as number fields");
    if (found < 0.0f) return;

    io.AddMouseButtonEvent(0, true);
    frame(app);
    io.AddMouseButtonEvent(0, false);
    frames(app, 3);
    EXPECT((ImGui::IsAnyItemActive()), "one click opens the field for typing");

    io.AddInputCharactersUTF8("12.5");
    frame(app);
    key(app, ImGuiKey_Enter);
    frames(app, 2);
    EXPECT_NEAR((app.document().get(panel).arrangement.x), (12.5), "the typed value moves the panel across");
    EXPECT_NEAR((app.document().get(panel).arrangement.y), (90.0), "and leaves its height alone");

    // A value outside the limits is refused, not clamped silently.
    io.AddMousePosEvent(x, found);
    frame(app);
    io.AddMouseButtonEvent(0, true);
    frame(app);
    io.AddMouseButtonEvent(0, false);
    frames(app, 3);
    io.AddInputCharactersUTF8("5000");
    frame(app);
    key(app, ImGuiKey_Enter);
    frames(app, 2);
    EXPECT_NEAR((app.document().get(panel).arrangement.x), (12.5), "a place far outside the limits is not taken");
}

// Dragging an arrow of the gizmo moves the panel along that axis only, and
// does not turn the camera.
TEST_CASE("gizmo arrows move along one axis", "[window]")
{
    const ImGuiForTests imgui(ImVec2(3840, 2160), 2.0f);
    App app;
    const PanelId panel = selectTheOnePanel(app);
    const View3D& view = app.view3D();
    const std::optional<View3D::ScreenPoint> centre = view.gizmoHandle(GizmoHandle::AcrossTheView);
    EXPECT((centre.has_value()), "the selected panel shows a gizmo");
    if (!centre) return;
    const double yaw = view.yaw(), pitch = view.pitch();

    const struct {
        GizmoHandle handle;
        double Arrangement::* moves;
        const char* what;
    } arrows[] = {{GizmoHandle::AlongX, &Arrangement::x, "x"}, {GizmoHandle::AlongY, &Arrangement::y, "y"}, {GizmoHandle::AlongZ, &Arrangement::z, "z"}};
    for (const auto& arrow : arrows) {
        const std::optional<View3D::ScreenPoint> grip = view.gizmoHandle(arrow.handle);
        const std::optional<View3D::ScreenPoint> middle = view.gizmoHandle(GizmoHandle::AcrossTheView);
        EXPECT((grip && middle), std::string("the gizmo has an arrow along ") + arrow.what);
        if (!grip || !middle) continue;
        const Arrangement before = app.document().get(panel).arrangement;
        // Pull outwards along the arrow as it is drawn.
        const float dx = grip->x - middle->x, dy = grip->y - middle->y;
        drag(app, {grip->x, grip->y}, {grip->x + dx, grip->y + dy});
        const Arrangement after = app.document().get(panel).arrangement;
        EXPECT((after.*arrow.moves > before.*arrow.moves + 1.0), std::string("pulling the arrow moves the panel along ") + arrow.what);
        for (double Arrangement::* other : {&Arrangement::x, &Arrangement::y, &Arrangement::z})
            if (other != arrow.moves) EXPECT_NEAR((after.*other), (before.*other), std::string("and not along any other axis than ") + arrow.what);
        EXPECT_NEAR((after.turn), (before.turn), "nor turns it");
    }
    EXPECT((view.yaw() == yaw && view.pitch() == pitch), "dragging the gizmo does not orbit the camera");
}

// The square in the middle of the gizmo moves the panel across the view, as
// dragging the panel itself does.
TEST_CASE("gizmo centre moves across the view", "[window]")
{
    const ImGuiForTests imgui(ImVec2(3840, 2160), 2.0f);
    App app;
    const PanelId panel = selectTheOnePanel(app);
    const std::optional<View3D::ScreenPoint> centre = app.view3D().gizmoHandle(GizmoHandle::AcrossTheView);
    EXPECT((centre.has_value()), "the selected panel shows a gizmo");
    if (!centre) return;
    const Arrangement before = app.document().get(panel).arrangement;
    drag(app, {centre->x, centre->y}, {centre->x, centre->y - 80.0f});
    const Arrangement after = app.document().get(panel).arrangement;
    EXPECT((after.y > before.y + 1.0), "dragging the centre up raises the panel");
}
