// Benchmarks of the window of 0.1: how long a frame takes while nothing happens,
// while the 3D picture is redrawn, while the cloth runs and while a panel is
// dragged. Judged against tests/benchmarks/baselines/Window.txt; see tests/benchmarks/Perf.h.
//
// The whole interface runs here without a window or a graphics card: frames
// are built and thrown away. The display is taken to be 3840 x 2160.

#include <cmath>
#include <thread>

#include <imgui.h>

#include "App.h"
#include "Picture.h"
#include "Samples.h"
#include "Perf.h"

using namespace clothcad;
using namespace clothcad::app;

static void frame(App& app) {
    ImGui::NewFrame();
    app.frame();
    ImGui::Render();
}
static void frames(App& app, int count) {
    for (int i = 0; i < count; ++i) frame(app);
}

// A garment the size of a skirt, arranged around the body and sewn.
static Document skirt() {
    Document doc;
    const PanelId front = doc.add(Panel{.name = "front", .x = 20, .y = 20});
    const OpRef<Rect> frontRect = doc.add(front, Rect{.width = 60_cm, .height = 80_cm});
    const PanelId back = doc.add(Panel{.name = "back", .x = 100, .y = 20});
    const OpRef<Rect> backRect = doc.add(back, Rect{.width = 60_cm, .height = 80_cm});
    doc.add(Seam{.a = {front, frontRect.right()}, .b = {back, backRect.left()}});
    doc.add(Seam{.a = {front, frontRect.left()}, .b = {back, backRect.right()}});
    doc.arrange(front, Arrangement{.placed = true, .x = 0, .y = 90, .z = 26, .wrap = 100});
    doc.arrange(back, Arrangement{.placed = true, .x = 0, .y = 90, .z = -26, .turn = 180, .wrap = 100});
    return doc;
}

// A closed, smooth surface with about as many triangles as an avatar.
static sim::Surface egg() {
    sim::Surface surface;
    const int around = 140, rings = 70;
    for (int r = 0; r <= rings; ++r)
        for (int a = 0; a < around; ++a) {
            const double up = pi * r / rings, turn = 2.0 * pi * a / around;
            surface.points.push_back({15.0 * std::sin(up) * std::cos(turn), 100.0 - 30.0 * std::cos(up), 12.0 * std::sin(up) * std::sin(turn)});
        }
    const auto at = [&](int r, int a) { return static_cast<uint32_t>(r * around + a % around); };
    for (int r = 0; r < rings; ++r)
        for (int a = 0; a < around; ++a) {
            surface.triangles.push_back({at(r, a), at(r + 1, a + 1), at(r + 1, a)});
            surface.triangles.push_back({at(r, a), at(r, a + 1), at(r + 1, a + 1)});
        }
    return surface;
}

int main(int argc, char** argv) {
    perf::Report report(argc, argv);
    if (!report.worthRunning()) return 0;

    // ---- The 3D picture on its own ----
    {
        const Document doc = skirt();
        const sim::ClothMesh mesh = sim::meshOf(doc, evaluate(doc), {.particleDistance = 2_cm});
        const sim::Placement placement = sim::arranged(doc, mesh);
        const sim::Surface body = egg();
        Scene scene;
        scene.cloth = &mesh;
        scene.placement = &placement;
        scene.body = &body;
        const Eye eye{{0, 100, 260}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, 1400.0f};
        Picture picture;
        report.time("3D picture, 1500 x 1000, one thread", 14.0, [&] { picture.draw(1500, 1000, eye, scene, 1); });
        const double many = report.time("3D picture, 1500 x 1000, all threads", 14.0, [&] { picture.draw(1500, 1000, eye, scene); });
        scene.wire = true;
        report.time("3D picture with the mesh's lines", 20.0, [&] { picture.draw(1500, 1000, eye, scene); });
        std::size_t painted = 0;
        for (std::uint32_t colour : picture.colours()) painted += colour != picture.colours().front();
        report.expect(painted > 100000 && picture.thingAt(750, 500) != Picture::nothing, "the picture has the body and the cloth in it");
        (void)many;
    }

    // ---- Whole frames ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(3840, 2160);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::GetStyle().FontScaleMain = 2.0f;
    unsigned char* fontPixels;
    int fontWidth, fontHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    {
        App app;
        app.runClothOnItsOwnThread(false);
        app.useMannequin();
        app.open(skirt(), "skirt");
        const auto best = [&](int runs) { return perf::bestMilliseconds([&] { frame(app); }, runs); };

        app.setShow3D(false);
        frames(app, 5);
        const double flat = report.record("frame, 2D only, nothing changing", 1.0, best(60));

        app.setShow3D(true);
        frames(app, 5);
        const std::uint64_t picturesBefore = app.view3D().picturesDrawn(), meshesBefore = app.meshesMade();
        const double idle = report.record("frame, 3D shown, nothing changing", 1.0, best(60));
        report.expect(app.view3D().picturesDrawn() == picturesBefore, "an unchanged 3D view is not drawn again");
        report.expect(app.meshesMade() == meshesBefore, "and the mesh is not made again");
        report.expect(idle < flat + 0.5, "showing the 3D view costs an idle frame under half a millisecond");

        // Orbiting: the mouse dragged across empty space in the 3D view, a little each frame.
        const float inViewX = 900.0f, inViewY = 1900.0f;
        io.AddMousePosEvent(inViewX, inViewY);
        frames(app, 2);
        io.AddMouseButtonEvent(0, true);
        frames(app, 2);
        float dragged = 0.0f;
        const std::uint64_t beforeOrbit = app.view3D().picturesDrawn();
        const double yawBefore = app.view3D().yaw();
        const auto turnABit = [&] {
            dragged += 4.0f;
            io.AddMousePosEvent(inViewX + dragged, inViewY);
            frame(app);
        };
        for (int i = 0; i < 6; ++i) turnABit();  // the first few pixels of a drag are not yet a drag
        const double orbit = perf::bestMilliseconds(turnABit, 40);
        io.AddMouseButtonEvent(0, false);
        frames(app, 2);
        report.record("frame while orbiting (3D picture drawn again)", 12.0, orbit);
        report.expect(app.view3D().yaw() != yawBefore && app.view3D().picturesDrawn() >= beforeOrbit + 40, "orbiting turns the camera and redraws the picture");

        // Dragging a panel across the table: placement only, so nothing is meshed or simulated again.
        const Panel& front = app.document().get(app.document().panels().front());
        const Canvas::ScreenPoint onPanel = app.canvas().toScreen({front.x + 30, front.y + 40});
        io.AddMousePosEvent(onPanel.x, onPanel.y);
        frames(app, 2);
        io.AddMouseButtonEvent(0, true);
        frames(app, 2);
        const std::uint64_t meshesBeforeDrag = app.meshesMade();
        const double xBefore = front.x;
        dragged = 0.0f;
        const double drag = perf::bestMilliseconds(
            [&] {
                dragged += 4.0f;
                io.AddMousePosEvent(onPanel.x + dragged, onPanel.y);
                frame(app);
            },
            40);
        io.AddMouseButtonEvent(0, false);
        frames(app, 2);
        report.record("frame while dragging a panel on the table", 2.0, drag);
        report.expect(app.document().get(app.document().panels().front()).x != xBefore && app.meshesMade() == meshesBeforeDrag, "the panel moves and the mesh is not made again");

        // Dragging a corner with the 3D view shown, as the window runs (the cloth's
        // work on other threads), with fine particles: a mesh takes some 50 ms to
        // make. The shape changes every frame and the mesh follows it, but the
        // frame does not wait for it: what is left is drawing the 3D picture
        // again, as when orbiting. The average counts, not the best: one slow
        // frame in three is what a lag feels like.
        {
            app.runClothOnItsOwnThread(true);
            app.setParticleDistance(1.0f);
            frames(app, 2);
            for (int i = 0; i < 100 && app.meshesMade() == 0; ++i) frames(app, 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            frames(app, 2);
            const PanelId panel = app.document().panels().front();
            const Panel& placed = app.document().get(panel);
            const Canvas::ScreenPoint corner = app.canvas().toScreen({placed.x + 60, placed.y + 80});
            io.AddMousePosEvent(corner.x, corner.y);
            frames(app, 2);
            io.AddMouseButtonEvent(0, true);
            frames(app, 2);
            const std::uint64_t meshesAtGrab = app.meshesMade();
            float pulled = 0.0f;
            double sum = 0.0;
            const int watched = 40;
            for (int i = 0; i < watched + 6; ++i) {
                pulled += 3.0f;
                io.AddMousePosEvent(corner.x + pulled, corner.y - pulled);
                const double one = perf::bestMilliseconds([&] { frame(app); }, 1);
                if (i >= 6) sum += one;  // the first few pixels of a drag are not yet a drag
                std::this_thread::sleep_for(std::chrono::milliseconds(12));  // as a 60 Hz screen spaces frames out
            }
            const std::uint64_t meshesDuringDrag = app.meshesMade() - meshesAtGrab;
            io.AddMouseButtonEvent(0, false);
            report.record("frame while dragging a corner, 3D shown, 1 cm particles", 12.0, sum / watched);
            for (int i = 0; i < 200 && app.meshesMade() == meshesAtGrab; ++i) {
                frame(app);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::printf("(%llu meshes made during the drag of %d frames)\n", static_cast<unsigned long long>(meshesDuringDrag), watched + 6);
            report.expect(meshesDuringDrag >= 5, "the mesh follows the corner while it is dragged");
            app.setParticleDistance(2.0f);
            app.runClothOnItsOwnThread(false);
            frames(app, 2);
        }

        // The cloth, one step a frame: the frame then includes the step and the redrawn picture.
        app.setSimulating(true);
        frames(app, 60);
        report.record("frame with a cloth step in it (2,800 particles)", 28.0, best(40));
        report.expect(app.simulatedTime().value_or(0.0) > 1.6, "the cloth's time moves on");
        app.setSimulating(false);

        // The cloth on its own thread: the frame only picks up the news and redraws.
        app.runClothOnItsOwnThread(true);
        app.setSimulating(true);
        frames(app, 3);
        double worst = 0.0, sum = 0.0;
        const int watched = 90;
        for (int i = 0; i < watched; ++i) {
            const double one = perf::bestMilliseconds([&] { frame(app); }, 1);
            worst = std::max(worst, one);
            sum += one;
            std::this_thread::sleep_for(std::chrono::milliseconds(4));  // as a screen's refresh would space frames out
        }
        report.record("frame while the cloth runs on its own thread", 12.0, sum / watched);
        report.expect(app.simulatedTime().value_or(0.0) > 0.2, "the cloth moves without the frames stepping it");
        (void)worst;
        app.setSimulating(false);
    }
    ImGui::DestroyContext();
    return report.finish();
}
