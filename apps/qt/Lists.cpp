#include "Lists.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>

namespace twintailor::ui {

namespace {

// Qt's lists take the wheel as it passes over them. An event filter is what Qt
// offers to say otherwise without a list of our own.
class WheelPassedOn : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* watched, QEvent* event) override {
        const auto* list = qobject_cast<QWidget*>(watched);
        if (event->type() != QEvent::Wheel || !list || list->hasFocus()) return false;
        event->ignore();  // not taken: Qt hands it on to what holds the list
        return true;
    }
};

} // namespace

void takeWheelOnlyWithFocus(QComboBox& list) {
    // Not by the wheel itself, which would give it the focus first.
    if (list.focusPolicy() == Qt::WheelFocus) list.setFocusPolicy(Qt::StrongFocus);
    list.installEventFilter(new WheelPassedOn(&list));
}

void fitItsPlace(QComboBox& list) {
    list.setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    list.setMinimumContentsLength(4);
    list.view()->setMinimumWidth(list.view()->sizeHintForColumn(0));
}

} // namespace twintailor::ui
