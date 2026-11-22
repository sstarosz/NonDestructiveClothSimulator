#include "Section.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace clothcad::ui {

Section::Section(const QString& title, QWidget* parent) : QWidget(parent), title_(title) {
    headerRow_ = new QWidget(this);
    auto* row = new QHBoxLayout(headerRow_);
    row->setContentsMargins(0, 0, 4, 0);
    row->setSpacing(2);
    header_ = new QPushButton(title, headerRow_);
    header_->setObjectName("sectionHeader");
    header_->setCheckable(true);
    header_->setChecked(true);
    header_->setFlat(true);
    row->addWidget(header_, 1);
    content_ = new QWidget(this);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(headerRow_);
    column->addWidget(content_, 1);  // a content that is given more room, uses it
    connect(header_, &QPushButton::toggled, this, &Section::setOpen);
    setOpen(true);
}

void Section::addToHeader(QWidget* widget) { static_cast<QHBoxLayout*>(headerRow_->layout())->addWidget(widget); }

void Section::setOpen(bool open) {
    header_->setChecked(open);
    content_->setVisible(open);
    header_->setText(QString(open ? QChar(0x25BE) : QChar(0x25B8)) + "  " + title_);  // the arrow says which way it folds
}

bool Section::isOpen() const { return content_->isVisible(); }

} // namespace clothcad::ui
