#pragma once
// The window's QApplication: an error that comes out of an event, which would
// otherwise end the program without a word, is handed to whoever can still
// save the work.
//
//   ui::Application app(argc, argv);
//   app.handleErrors([&](const QString& error) { window.cannotGoOn(error); });
//
// Inheritance, where nothing else will do: Qt hands every event to
// QApplication::notify, and only an override of it sees what escapes one.

#include <QApplication>
#include <QString>
#include <functional>

namespace twintailor::ui {

class Application : public QApplication {
public:
    Application(int& argc, char** argv) : QApplication(argc, argv) {}
    // From now on an error that comes out of an event goes to `then`, in its
    // own words. Empty, as at first: none is caught.
    void handleErrors(std::function<void(const QString&)> then) { handler_ = std::move(then); }
    bool notify(QObject* receiver, QEvent* event) override;

private:
    std::function<void(const QString&)> handler_;
};

} // namespace twintailor::ui
