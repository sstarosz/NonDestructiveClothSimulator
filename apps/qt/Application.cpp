#include "Application.h"

#include <exception>

namespace clothcad::ui {

bool Application::notify(QObject* receiver, QEvent* event) {
    try {
        return QApplication::notify(receiver, event);
    } catch (const std::exception& problem) {
        if (!handler_) throw;
        handler_(QString::fromUtf8(problem.what()));
    } catch (...) {
        if (!handler_) throw;
        handler_("an error that says nothing of itself");
    }
    return true;  // the event is done with
}

} // namespace clothcad::ui
