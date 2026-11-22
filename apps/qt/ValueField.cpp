#include "ValueField.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyle>
#include <cmath>

#include "clothcad/core/Text.h"

namespace clothcad::ui {

namespace {

// Restyling is not free and show() runs on every change of the document, so
// a mark that is already right is left alone. An unset mark styles as false.
void mark(QLineEdit& field, const char* property, bool on) {
    if (field.property(property).toBool() == on) return;
    field.setProperty(property, on);
    field.style()->unpolish(&field);
    field.style()->polish(&field);
}

// How far the mouse moves before a press is a drag, in pixels.
constexpr double dragSlack = 3.0;

// Change per pixel of a drag: small numbers move in finer steps.
double stepFor(Unit unit, double value) {
    if (unit == Unit::Fraction) return 0.001;
    return std::abs(value) < 5.0 ? 0.01 : 0.1;
}

} // namespace

ValueField::ValueField(Unit unit, QWidget* parent) : QLineEdit(parent), unit_(unit) {
    // A click must not give focus by itself: Qt gives it before the press
    // reaches the field, and a focused field types instead of dragging. The
    // field takes focus itself, on a click without a drag or on a formula.
    setFocusPolicy(Qt::TabFocus);
    connect(this, &QLineEdit::editingFinished, this, [this] {
        if (text() != shown_) emit typed(text());
    });
}

void ValueField::show(const Expr& value, const Document& doc, const Result& shapes) {
    if (hasFocus() && isModified()) return;  // the user is typing
    const std::optional<double> number = asNumber(value);
    const bool formula = !number.has_value();
    number_ = number;
    if (formula) {
        shown_ = QString::fromStdString("= " + formatExpr(value, doc));
        const std::optional<double> worked = valueOf(value, shapes);
        setToolTip(worked ? QString::fromStdString(formatValue(*worked, unit_)) : QString());
    } else {
        shown_ = QString::fromStdString(formatValue(*number, unit_));
        setToolTip({});
    }
    setText(shown_);
    setModified(false);
    mark(*this, "formula", formula);
    mark(*this, "refused", false);
    showCursor();
}

// A number not being typed in shows that it can be dragged sideways.
void ValueField::showCursor() { setCursor(number_ && !hasFocus() ? Qt::SizeHorCursor : Qt::IBeamCursor); }

void ValueField::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !hasFocus()) {
        if (number_) {
            drag_ = Drag{event->position().x(), *number_, dragStep_ ? *dragStep_ : stepFor(unit_, *number_)};
            event->accept();
            return;
        }
        setFocus(Qt::MouseFocusReason);  // a formula is only typed
    }
    QLineEdit::mousePressEvent(event);
}

void ValueField::mouseMoveEvent(QMouseEvent* event) {
    if (!drag_ || !(event->buttons() & Qt::LeftButton)) return QLineEdit::mouseMoveEvent(event);
    const double pulled = event->position().x() - drag_->pressedX;  // right is more
    if (!drag_->moving && std::abs(pulled) <= dragSlack) return;
    const double step = drag_->step * (event->modifiers() & Qt::ShiftModifier ? 10.0 : event->modifiers() & Qt::AltModifier ? 0.1 : 1.0);
    const double grain = drag_->step * 0.1;  // values land on a tenth of a step, whatever the keys
    const double value = std::round((drag_->start + std::round(pulled) * step) / grain) * grain;
    const bool first = !drag_->moving;
    drag_->moving = true;
    if (!first && value == drag_->value) return;
    drag_->value = value;
    emit dragged(value, first ? clothcad::session::DragPhase::Begin : clothcad::session::DragPhase::Continue);
}

void ValueField::mouseReleaseEvent(QMouseEvent* event) {
    if (!drag_) return QLineEdit::mouseReleaseEvent(event);
    const Drag was = *drag_;
    drag_.reset();
    if (was.moving) {
        emit dragged(was.value, clothcad::session::DragPhase::End);
    } else {
        // A click without a drag: type.
        setFocus(Qt::MouseFocusReason);
        selectAll();
    }
}

// While typing, Enter takes what was typed and Escape puts back what was
// shown; both close the field, so the next press drags again. What was typed
// is reported once, as the field loses focus (editingFinished).
void ValueField::keyPressEvent(QKeyEvent* event) {
    if (hasFocus()) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            clearFocus();
            event->accept();
            return;
        case Qt::Key_Escape:
            setText(shown_);
            setModified(false);
            mark(*this, "refused", false);
            clearFocus();
            event->accept();
            return;
        default: break;
        }
    }
    QLineEdit::keyPressEvent(event);
}

void ValueField::focusInEvent(QFocusEvent* event) {
    QLineEdit::focusInEvent(event);
    showCursor();
}

void ValueField::focusOutEvent(QFocusEvent* event) {
    QLineEdit::focusOutEvent(event);
    showCursor();
}

void ValueField::refuse(const QString& why) {
    mark(*this, "refused", true);
    setToolTip(why);
}

} // namespace clothcad::ui
