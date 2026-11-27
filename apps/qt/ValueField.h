#pragma once
// A field for one number or formula, shown with its unit: "26 cm", "25°",
// "66.7 %"; a formula shows as "= chest / 4" in teal, with its value as the
// tooltip. A number is dragged left or right to change it (Shift: faster,
// Alt: finer); a click opens it for typing, as does any click on a formula.
// Enter takes what was typed and Escape puts the value back; both close the
// field, so the next press drags again.
// What is typed is reported as text: whoever owns the document reads it
// (parseValue) and says if it was not taken. A drag is reported as numbers,
// in the field's unit, from its beginning to its end.
//
//   auto* width = new ValueField(Unit::Length);
//   width->show(rect.width, doc, shapes);
//   connect(width, &ValueField::typed, [&](QString text) { ... width->refuse(why); });
//   connect(width, &ValueField::dragged, [&](double value, session::DragPhase phase) { ... });

#include <QLineEdit>
#include <optional>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/session/Drafting.h"

namespace clothcad::ui {

class ValueField : public QLineEdit {
    Q_OBJECT
public:
    explicit ValueField(Unit unit, QWidget* parent = nullptr);

    // Shows a value; leaves the text alone while the user is typing in it.
    void show(const Expr& value, const Document& doc, const Result& shapes);
    // Marks what was typed as not taken, with why in words.
    void refuse(const QString& why);
    bool refused() const;  // marked so, and not shown anew or put back since
    Unit unit() const { return unit_; }
    // How much a drag changes the number per pixel; by default it follows the number's size.
    void setDragStep(double perPixel) { dragStep_ = perPixel; }
    // A field that is open for typing from the start (the length bar) is dragged as well: a press and
    // a pull sideways changes the number in it (a click places the cursor, as in any field), taking
    // the number that is typed in it now, if it is a plain number in the field's unit.
    void setDragWhileTyping(bool on) { dragWhileTyping_ = on; }

signals:
    // The user typed something different from what was shown.
    void typed(QString text);
    // The number was dragged to `value`; one drag goes Begin, Continue ..., End.
    void dragged(double value, clothcad::session::DragPhase phase);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void showCursor();
    std::optional<double> typedNumber() const;

    Unit unit_;
    QString shown_;                 // the text as last shown, to tell whether anything was typed
    std::optional<double> number_;  // the number shown; empty for a formula
    std::optional<double> dragStep_;
    bool dragWhileTyping_ = false;
    struct Drag {
        double pressedX;
        double start;    // the number when the button went down
        double step;     // change per pixel
        bool moving = false;
        double value = 0.0;  // the last value reported
    };
    std::optional<Drag> drag_;
};

} // namespace clothcad::ui
