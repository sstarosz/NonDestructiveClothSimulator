#pragma once
// The lists (QComboBox) of the docks and the table, as this window wants them.
//
//   auto* list = new QComboBox(card);
//   list->addItems(entries);
//   takeWheelOnlyWithFocus(*list);   // scrolling past it changes nothing
//   fitItsPlace(*list);              // after its entries: no wider than the room it has

class QComboBox;

namespace twintailor::ui {

// The wheel changes the list only once it has the focus. Otherwise it goes on
// to whatever holds the list, so that a dock scrolled with the pointer over a
// list scrolls, and the pattern stays as it is.
void takeWheelOnlyWithFocus(QComboBox& list);

// The list asks for no width of its own for its longest entry: closed, an entry
// too long for it is cut off; opened, the entries are shown whole.
void fitItsPlace(QComboBox& list);

} // namespace twintailor::ui
