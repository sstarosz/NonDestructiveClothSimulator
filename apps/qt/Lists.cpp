#include "Lists.h"

#include <QAbstractItemView>
#include <QComboBox>

namespace clothcad::ui {

void fitItsPlace(QComboBox& list) {
    list.setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    list.setMinimumContentsLength(4);
    list.view()->setMinimumWidth(list.view()->sizeHintForColumn(0));
}

} // namespace clothcad::ui
