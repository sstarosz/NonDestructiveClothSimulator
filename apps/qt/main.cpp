// The Qt window of Non-Destructive Cloth CAD.
//
//   clothcad            opens the window
//   clothcad --version  says which build it is, and ends
#include <QMessageBox>
#include <QStandardPaths>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdio>
#endif

#include "Application.h"
#include "MainWindow.h"
#include "Theme.h"
#include "Version.h"

namespace {

// Writes a line where the program was started from. On Windows a program with
// a window has no console of its own: output sent to a file or a pipe is
// there already, and for one typed in a console that console is borrowed.
void print(const std::string& line) {
#ifdef _WIN32
    const auto write = [](HANDLE to, const std::string& text) {
        DWORD written = 0;
        return to != nullptr && to != INVALID_HANDLE_VALUE && WriteFile(to, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    };
    if (write(GetStdHandle(STD_OUTPUT_HANDLE), line + "\r\n")) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;  // started from nothing that could show it
    const HANDLE console = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    // The console did not wait for the program: its prompt is on this line already.
    write(console, "\r\n" + line + "\r\n");
    if (console != INVALID_HANDLE_VALUE) CloseHandle(console);
#else
    std::puts(line.c_str());
#endif
}

} // namespace

int main(int argc, char** argv) {
    // Before Qt: no window, and nothing that needs a screen.
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        print(clothcad::ui::versionLine().toStdString());
        return 0;
    }
    clothcad::ui::Application app(argc, argv);
    QApplication::setApplicationName("Cloth CAD");
    try {
        clothcad::ui::theme::apply(app);
        clothcad::ui::MainWindow window;
        // An error nothing else handles: the work is copied, the window says so,
        // and the program ends. At once: what may be broken is not closed down
        // in order, and the copy stays for the next start.
        QObject::connect(&window, &clothcad::ui::MainWindow::mustEnd, &window, [] { std::_Exit(EXIT_FAILURE); });
        app.handleErrors([&window](const QString& error) { window.cannotGoOn(error); });
        try {
            window.openSample(5);  // the T-shirt body
            window.show();
            const QString own = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            window.keepSettingsIn(own + "/settings.ini");
            // Unsaved work is copied here now and then, and offered again if the program stopped before it was saved.
            window.keepRecoveryIn(own + "/recovery");
        } catch (const std::exception& problem) {
            window.show();
            window.cannotGoOn(QString::fromUtf8(problem.what()));
        }
        const int ended = QApplication::exec();
        app.handleErrors({});  // the window it goes to is about to go
        return ended;
    } catch (const std::exception& problem) {
        QMessageBox::critical(nullptr, "Cloth CAD", QString("Cloth CAD could not start (%1).").arg(QString::fromUtf8(problem.what())));
        return EXIT_FAILURE;
    }
}
