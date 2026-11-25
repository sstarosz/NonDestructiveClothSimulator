#include "LengthCard.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Theme.h"
#include "ValueField.h"
#include "clothcad/core/Text.h"

namespace clothcad::ui {

LengthCard::LengthCard(QWidget* parent) : QFrame(parent) {
    setObjectName("lengthCard");
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);
    // A click on a part of the bar that has no use for it (its caption, a gap, the words under the field)
    // goes up to the widget that has the bar. That is the table, which would take it for a click on empty
    // table: it would cancel the bar and choose nothing. The bar keeps every click to itself.
    setAttribute(Qt::WA_NoMousePropagation, true);
    setStyleSheet(QString(
        "#lengthCard { background: transparent; }"
        "#lengthCaption { color: %3; }"
        "#lengthNumber { background: %4; border: 1px solid %6; border-radius: 6px; }"
        "#lengthNumber QLineEdit { background: transparent; color: %5; border: 0; padding: 5px 4px 5px 10px; font-size: 16px; font-weight: 500; selection-background-color: %6; selection-color: %4; }"
        "#lengthNumber QLabel { color: %3; padding-right: 10px; }"
        "#lengthCard QComboBox { background: %1; color: %5; border: 1px solid %2; border-radius: 6px; padding: 6px 8px; }"
        "#lengthCard QToolButton { background: %1; color: %5; border: 1px solid %2; border-radius: 6px; min-width: 30px; min-height: 30px; font-size: 15px; }"
        "#lengthCard QToolButton#lengthApply { background: %7; color: %6; border-color: %6; }"
        "#lengthCard QToolButton:disabled { color: %3; border-color: %2; background: %1; }"
        "#lengthProblem { color: %8; background: %1; border-radius: 4px; padding: 3px 8px; }")
                      .arg(theme::panel.name(), theme::cardEdgeOpen.name(), theme::quiet.name(), theme::field.name(), theme::text.name(), theme::selected.name(),
                           QColor(61, 51, 32).name(), theme::error.name()));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(5);
    caption_ = new QLabel(this);
    caption_->setObjectName("lengthCaption");
    caption_->setAlignment(Qt::AlignHCenter);
    column->addWidget(caption_);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(6);
    auto* number = new QFrame(this);
    number->setObjectName("lengthNumber");
    auto* inside = new QHBoxLayout(number);
    inside->setContentsMargins(0, 0, 0, 0);
    inside->setSpacing(0);
    field_ = new ValueField(Unit::Length, number);
    field_->setObjectName("lengthEditor");
    field_->setDragWhileTyping(true);
    field_->setCursor(Qt::SizeHorCursor);
    field_->setToolTip("Type a length, or press on the number and pull sideways to change it.\nShift: faster · Alt: finer");
    field_->installEventFilter(this);
    // A pull changes the text, and the text is shown at once like anything typed.
    connect(field_, &ValueField::dragged, this, [this](double value, session::DragPhase) {
        field_->setText(QString::fromStdString(formatValue(value, Unit::Length)));
    });
    inside->addWidget(field_, 1);
    number->setMinimumWidth(110);
    bar->addWidget(number, 1);
    scope_ = new QComboBox(this);
    scope_->setObjectName("lengthScope");
    scope_->setFocusPolicy(Qt::ClickFocus);
    scope_->addItems({"Edge only", "Shape's setting"});
    scope_->setToolTip("Edge only: this edge gets a length of its own and the rest of the shape keeps its setting.\n"
                       "The shape's setting: the shape sets this edge, and the edge opposite it with it.");
    bar->addWidget(scope_);
    apply_ = new QToolButton(this);
    apply_->setObjectName("lengthApply");
    apply_->setText("✓");
    apply_->setToolTip("Apply · Enter");
    apply_->setFocusPolicy(Qt::ClickFocus);
    cancel_ = new QToolButton(this);
    cancel_->setObjectName("lengthCancel");
    cancel_->setText("✕");
    cancel_->setToolTip("Cancel · Esc");
    cancel_->setFocusPolicy(Qt::ClickFocus);
    bar->addWidget(apply_);
    bar->addWidget(cancel_);
    column->addLayout(bar);
    problem_ = new QLabel(this);
    problem_->setObjectName("lengthProblem");
    problem_->setAlignment(Qt::AlignHCenter);
    problem_->setWordWrap(true);
    problem_->hide();
    column->addWidget(problem_);

    connect(field_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (what_) emit edited(text);
    });
    connect(field_, &QLineEdit::returnPressed, this, &LengthCard::commit);
    connect(apply_, &QToolButton::clicked, this, &LengthCard::commit);
    connect(cancel_, &QToolButton::clicked, this, &LengthCard::cancel);
    connect(scope_, &QComboBox::activated, this, [this](int chosen) {
        if (what_) emit scopeChosen(chosen == 0);
        field_->setFocus();
    });
    setFixedWidth(392);
    hide();
}

void LengthCard::open(const session::Outcome::LengthField& what, const QString& text) {
    what_ = what;
    caption_->setText(QString::fromStdString(what.caption));
    {
        const QSignalBlocker quiet(scope_);
        scope_->setVisible(what.canSwitch);
        scope_->setItemText(1, what.shapeSetting.empty() ? QString("Shape's setting") : "Shape's " + QString::fromStdString(what.shapeSetting).toLower());
        scope_->setCurrentIndex(what.ownLength ? 0 : 1);
    }
    {
        const QSignalBlocker quiet(field_);
        field_->setText(text);
    }
    showProblem({});
    adjustSize();
    show();
    raise();
    field_->setFocus();
    field_->selectAll();
}

void LengthCard::dismiss() {
    what_.reset();
    hide();
}

void LengthCard::showProblem(const QString& problem) {
    problem_->setText(problem);
    problem_->setVisible(!problem.isEmpty());
    apply_->setEnabled(problem.isEmpty());
    field_->setProperty("invalid", !problem.isEmpty());
    adjustSize();
}

QString LengthCard::problem() const { return problem_->text(); }

void LengthCard::commit() {
    if (!what_ || !problem_->text().isEmpty()) return;  // what is typed does not work yet: it stays
    const QString text = field_->text();
    if (parentWidget()) parentWidget()->setFocus();
    dismiss();
    emit committed(text);
}

void LengthCard::cancel() {
    if (!what_) return;
    if (parentWidget()) parentWidget()->setFocus();
    dismiss();
    emit cancelled();
}

bool LengthCard::eventFilter(QObject* watched, QEvent* event) {
    // Enter and Escape are the bar's, not the field's: the field would close itself.
    if (watched == field_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Escape) {
            cancel();
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            commit();
            return true;
        }
    }
    // Leaving for another widget cancels. Not while the list's own window is open (it takes the focus
    // for itself, and gives it back), nor when it is not yet clear where the focus went: the bar's own
    // parts, and the table it is on (a click on an anchor is a click on the table), are not elsewhere.
    // Where the focus went is asked once it has settled.
    if (watched == field_ && event->type() == QEvent::FocusOut && what_) {
        QTimer::singleShot(0, this, [this] {
            const QWidget* now = QApplication::focusWidget();
            const bool unsettled = !now || QApplication::activePopupWidget() != nullptr;
            const bool here = now && (now == this || isAncestorOf(now) || now == parentWidget());
            if (what_ && !unsettled && !here) cancel();
        });
    }
    return QFrame::eventFilter(watched, event);
}

} // namespace clothcad::ui
