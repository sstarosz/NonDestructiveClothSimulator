#pragma once
// The lists (QComboBox) of the docks and the table, as this window wants them.
//
//   auto* list = new QComboBox(card);
//   list->addItems(entries);
//   fitItsPlace(*list);              // after its entries: no wider than the room it has

class QComboBox;

namespace clothcad::ui {

// The list asks for no width of its own for its longest entry: closed, an entry
// too long for it is cut off; opened, the entries are shown whole.
void fitItsPlace(QComboBox& list);

} // namespace clothcad::ui
