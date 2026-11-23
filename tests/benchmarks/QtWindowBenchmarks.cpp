// Benchmarks of the Qt window: how long a frame takes while nothing happens,
// while a panel or a corner is dragged on the table, while the 3D view is
// orbited and while the cloth runs. Judged against baselines/QtWindow.txt;
// see Perf.h.
//
// The window runs without a screen (Qt's off-screen platform). There the 3D
// view draws off screen and reads the picture back each time it changes,
// which costs more than showing it in a window: the limits allow for that.
// With no Vulkan at all, the 3D view draws nothing and costs nothing.

#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <chrono>
#include <thread>

#include "AttributeEditor.h"
#include "Avatar.h"
#include "MainWindow.h"
#include "Perf.h"
#include "SpaceView.h"
#include "TableView.h"
#include "Theme.h"

using namespace clothcad;
using namespace clothcad::ui;

namespace {

// A garment the size of a skirt, arranged around the body and sewn.
Document skirt() {
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

// One frame as the window runs it: the session catches up, then Qt paints.
void frame(MainWindow& window) {
    window.tick();
    QApplication::processEvents();
}

void mouse(QWidget* widget, QEvent::Type type, QPointF at, Qt::MouseButtons held) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, held, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

} // namespace

int main(int argc, char** argv) {
    perf::Report report(argc, argv);
    if (!report.worthRunning()) return 0;
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    int qtArguments = 1;
    QApplication app(qtArguments, argv);
    theme::apply(app);
    {
        MainWindow window(session::Threading::OneStepPerAdvance);
        window.setTicking(false);  // the frames are run here, one at a time
        window.resize(1600, 900);
        window.open(skirt(), "skirt");
        // On the project's avatar, as the samples are; the cloth waits until it can collide with it.
        if (!window.loadAvatar(QString::fromStdString(project::avatarPath()))) return 1;
        for (int i = 0; i < 1000 && !window.session().bodyReady(); ++i) {
            window.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        window.show();
        const auto best = [&](int runs) { return perf::bestMilliseconds([&] { frame(window); }, runs); };

        // ---- Nothing changing ----
        window.shelf()->layout(ViewLayout::TableOnly)->trigger();
        for (int i = 0; i < 5; ++i) frame(window);
        report.record("frame, table only, nothing changing", 1.0, best(60));
        window.shelf()->layout(ViewLayout::SideBySide)->trigger();
        for (int i = 0; i < 5; ++i) frame(window);
        const std::uint64_t meshes = window.session().meshesMade();
        report.record("frame, both views, nothing changing", 1.0, best(60));
        report.expect(window.session().meshesMade() == meshes, "an unchanged document is not meshed again");

        // ---- Orbiting the 3D view: the picture drawn again each time ----
        SpaceView& space = *window.space();
        if (space.drawing() != SpaceView::Drawing::Nothing) {
            const double yaw = space.camera().yaw;
            QPointF at(200, 300);
            space.press(at, Qt::LeftButton, Qt::AltModifier);
            const double orbit = perf::bestMilliseconds(
                [&] {
                    at += QPointF(4, 0);
                    space.moveTo(at, Qt::LeftButton, Qt::AltModifier);
                    QApplication::processEvents();
                },
                40);
            space.release();
            report.record("3D view orbited (drawn off screen and read back)", 60.0, orbit);
            report.expect(space.camera().yaw != yaw, "orbiting turns the camera");
        } else {
            std::printf("(no Vulkan: the 3D view is not timed) %s\n", space.problem().toStdString().c_str());
        }

        // ---- Dragging a panel on the table: placement only, nothing meshed again ----
        window.shelf()->layout(ViewLayout::TableOnly)->trigger();
        for (int i = 0; i < 3; ++i) frame(window);
        TableView* table = window.table();
        const PanelId front = window.session().document().panels().front();
        const double xBefore = window.session().document().get(front).x;
        QPointF on = table->toScreen({xBefore + 30, window.session().document().get(front).y + 40});
        mouse(table, QEvent::MouseButtonPress, on, Qt::LeftButton);
        const std::uint64_t meshesBeforeDrag = window.session().meshesMade();
        const double drag = perf::bestMilliseconds(
            [&] {
                on += QPointF(4, 0);
                mouse(table, QEvent::MouseMove, on, Qt::LeftButton);
                frame(window);
            },
            40);
        mouse(table, QEvent::MouseButtonRelease, on, Qt::NoButton);
        report.record("frame while dragging a panel on the table", 8.0, drag);
        report.expect(window.session().document().get(front).x != xBefore && window.session().meshesMade() == meshesBeforeDrag, "the panel moves and is not meshed again");

        // ---- Dragging a corner, the mesh made on another thread, 1 cm particles ----
        // The shape changes every frame and the mesh follows it, but the frame
        // does not wait for it. The average counts, not the best: one slow
        // frame in three is what a lag feels like.
        {
            window.shelf()->layout(ViewLayout::SideBySide)->trigger();  // the 3D view shown, as one works
            window.setThreading(session::Threading::OwnThread);
            window.select(front);
            frame(window);
            emit window.attributes()->particleDistanceTyped(front, "1");
            for (int i = 0; i < 200 && window.session().meshesMade() == meshesBeforeDrag; ++i) {
                frame(window);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            const Panel& placed = window.session().document().get(front);
            QPointF corner = table->toScreen({placed.x + 60, placed.y + 80});
            mouse(table, QEvent::MouseButtonPress, corner, Qt::LeftButton);
            const std::uint64_t meshesAtGrab = window.session().meshesMade();
            double sum = 0.0;
            const int watched = 40;
            for (int i = 0; i < watched + 3; ++i) {
                corner += QPointF(3, -3);
                const double one = perf::bestMilliseconds(
                    [&] {
                        mouse(table, QEvent::MouseMove, corner, Qt::LeftButton);
                        frame(window);
                    },
                    1);
                if (i >= 3) sum += one;  // the first few pixels of a drag are not yet a drag
                std::this_thread::sleep_for(std::chrono::milliseconds(12));  // as a 60 Hz screen spaces frames out
            }
            mouse(table, QEvent::MouseButtonRelease, corner, Qt::NoButton);
            const std::uint64_t meshesDuringDrag = window.session().meshesMade() - meshesAtGrab;
            report.record("frame while dragging a corner, 3D shown, 1 cm particles", 16.0, sum / watched);
            std::printf("(%llu meshes made during the drag of %d frames)\n", static_cast<unsigned long long>(meshesDuringDrag), watched + 3);
            report.expect(meshesDuringDrag >= 5, "the mesh follows the corner while it is dragged");
            emit window.attributes()->particleDistanceTyped(front, "2");
            window.setThreading(session::Threading::OneStepPerAdvance);
            for (int i = 0; i < 3; ++i) frame(window);
        }

        // ---- The cloth, one step a frame, both views shown ----
        window.shelf()->simulate()->setChecked(true);
        for (int i = 0; i < 60; ++i) frame(window);
        report.record("frame with a cloth step in it, 3D shown", 40.0, best(40));
        report.expect(window.session().clothShown() && window.session().cloth().time > 1.0, "the cloth's time moves on");
        window.shelf()->simulate()->setChecked(false);

        // ---- The cloth on its own thread: the frame only picks up the news ----
        window.setThreading(session::Threading::OwnThread);
        window.shelf()->simulate()->setChecked(true);
        for (int i = 0; i < 3; ++i) frame(window);
        const double timeBefore = window.session().clothShown() ? window.session().cloth().time : 0.0;
        double sum = 0.0;
        const int watched = 90;
        for (int i = 0; i < watched; ++i) {
            sum += perf::bestMilliseconds([&] { frame(window); }, 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(4));  // as a screen's refresh would space frames out
        }
        report.record("frame while the cloth runs on its own thread, 3D shown", 16.0, sum / watched);
        report.expect(window.session().clothShown() && window.session().cloth().time > timeBefore, "the cloth moves without the frames stepping it");
        window.shelf()->simulate()->setChecked(false);
    }
    return report.finish();
}
