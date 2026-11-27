#pragma once
// The bar that opens on the table to type an edge's length: a caption naming
// the edge, the number with its unit, where the shape also has a setting for the
// edge (a rectangle's width) a list that says which of the two is typed ("Edge
// only", "Shape's width"), and apply and cancel. A problem with what is typed is
// said on a line under it, and Enter waits until there is none.
// The number is a ValueField, as in the Attribute Editor: type it, or press on it and pull sideways to
// make it more or less (Shift: faster, Alt: finer).
// Which end stays put is not here: it is chosen on the edge itself (TableView).
// It only reports; whoever owns the document decides. While it is open every
// edit is reported at once, so that the table can show the result as it is typed.
//
//   auto* card = new LengthCard(table);
//   connect(card, &LengthCard::edited, [&](QString text) { ...; card->showProblem(why); });
//   connect(card, &LengthCard::committed, [&](QString text) { ... });
//   connect(card, &LengthCard::cancelled, [&] { ... });
//   connect(card, &LengthCard::scopeChosen, [&](bool thisEdgeOnly) { ... });
//   card->open(what, "30 cm");

#include <QFrame>
#include <optional>

#include "ValueField.h"
#include "twintailor/session/Drafting.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace twintailor::ui {


class LengthCard : public QFrame {
    Q_OBJECT
public:
    explicit LengthCard(QWidget* parent = nullptr);

    // Shows the bar for this length with `text` in it, selected for typing. Opened again for
    // another field (the other of the two settings), it keeps its place and says nothing of it.
    void open(const session::Outcome::LengthField& what, const QString& text);
    // Closes it without a word to anyone: the window did it.
    void dismiss();
    // As Escape: closes it and says cancelled.
    void cancel();
    // Says what is wrong with what is typed, under the field; empty: nothing is. Apply waits for that.
    void showProblem(const QString& problem);
    QString problem() const;
    // The length being typed, while the bar is open.
    const std::optional<session::Outcome::LengthField>& what() const { return what_; }
    QLineEdit* field() const { return field_; }

signals:
    void edited(const QString& text);     // every change of the text
    void committed(const QString& text);  // Enter or apply, with no problem showing
    void cancelled();                     // Escape, cancel, or a click elsewhere
    void scopeChosen(bool thisEdgeOnly);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    // The wheel stays on the bar, as clicks do: the table zoomed under it would move what is being typed about.
    void wheelEvent(QWheelEvent* event) override;

private:
    void commit();

    std::optional<session::Outcome::LengthField> what_;
    QLabel* caption_ = nullptr;
    ValueField* field_ = nullptr;
    QComboBox* scope_ = nullptr;
    QToolButton* apply_ = nullptr;
    QToolButton* cancel_ = nullptr;
    QLabel* problem_ = nullptr;
};

} // namespace twintailor::ui
