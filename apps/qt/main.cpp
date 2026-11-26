// The Qt window of Non-Destructive Cloth CAD.
#include <QApplication>
#include <QStandardPaths>

#include "MainWindow.h"
#include "Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Cloth CAD");
    clothcad::ui::theme::apply(app);
    clothcad::ui::MainWindow window;
    window.openSample(5);  // the T-shirt body
    window.show();
    // Unsaved work is copied here now and then, and offered again if the program stopped before it was saved.
    window.keepRecoveryIn(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery");
    return QApplication::exec();
}
