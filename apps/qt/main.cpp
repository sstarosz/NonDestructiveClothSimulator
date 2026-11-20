// The Qt window of Non-Destructive Cloth CAD.
#include <QApplication>

#include "MainWindow.h"
#include "Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Cloth CAD");
    clothcad::ui::theme::apply(app);
    clothcad::ui::MainWindow window;
    window.openSample(5);  // the T-shirt body
    window.show();
    return QApplication::exec();
}
