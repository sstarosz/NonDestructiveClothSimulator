#include "ParametersPanel.h"

#include <QFocusEvent>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "Theme.h"
#include "ValueField.h"

namespace clothcad::ui {

namespace {

// A parameter's name: one too long for the box shows its start, not its end.
class NameField : public QLineEdit {
public:
    using QLineEdit::QLineEdit;

protected:
    void focusOutEvent(QFocusEvent* event) override {
        QLineEdit::focusOutEvent(event);
        if (event->reason() != Qt::PopupFocusReason) setCursorPosition(0);  // its menu works on what is selected
    }
};

} // namespace

ParametersPanel::ParametersPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("parameters");
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(6, 4, 6, 6);
    auto* add = new QToolButton(this);
    add->setIcon(icon(Icon::Plus));
    add->setToolTip("Add a parameter");
    add->setAutoRaise(true);
    connect(add, &QToolButton::clicked, this, &ParametersPanel::addRequested);
    auto* top = new QHBoxLayout();
    top->addStretch(1);
    top->addWidget(add);
    column->addLayout(top);
    grid_ = new QGridLayout();
    grid_->setHorizontalSpacing(6);
    grid_->setVerticalSpacing(3);
    grid_->setColumnStretch(0, 1);
    column->addLayout(grid_);
    column->addStretch(1);
}

void ParametersPanel::show(const Document& doc, const Result& shapes) {
    const std::vector<Param> parameters = doc.parameters();
    // The rows are made again only when the list itself changed.
    bool same = parameters.size() == rows_.size();
    for (std::size_t i = 0; same && i < parameters.size(); ++i)
        same = rows_[i].parameter == parameters[i] && rows_[i].field->unit() == doc.get(parameters[i]).unit;
    if (!same) {
        while (QLayoutItem* item = grid_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        rows_.clear();
        int row = 0;
        for (Param parameter : parameters) {
            auto* name = new NameField(this);
            name->setObjectName("parameterName");
            name->setFrame(false);
            connect(name, &QLineEdit::editingFinished, this, [this, parameter, name] {
                if (name->isModified()) emit renamed(parameter, name->text().trimmed());
                name->setModified(false);
            });
            auto* field = new ValueField(doc.get(parameter).unit, this);
            field->setMinimumWidth(96);
            connect(field, &ValueField::typed, this, [this, parameter](QString text) { emit valueTyped(parameter, text); });
            connect(field, &ValueField::dragged, this, [this, parameter](double value, session::DragPhase phase) { emit valueDragged(parameter, value, phase); });
            auto* bin = new QToolButton(this);
            bin->setObjectName("removeParameter");
            bin->setIcon(icon(Icon::Delete, theme::quiet));
            bin->setToolTip("Remove this parameter. Formulas that use it show a problem until they are changed.");
            bin->setAutoRaise(true);
            connect(bin, &QToolButton::clicked, this, [this, parameter] { remove(parameter); });
            auto* problem = new QLabel(this);
            problem->setWordWrap(true);
            problem->setStyleSheet(QString("color: %1").arg(theme::error.name()));
            problem->hide();
            grid_->addWidget(name, row, 0);
            grid_->addWidget(field, row, 1);
            grid_->addWidget(bin, row, 2);
            grid_->addWidget(problem, row + 1, 0, 1, 3);
            rows_.push_back({parameter, name, field, problem});
            row += 2;
        }
    }
    for (const Row& row : rows_) {
        const QString name = QString::fromStdString(doc.get(row.parameter).name);
        if (!row.name->hasFocus()) {
            row.name->setText(name);
            row.name->setCursorPosition(0);  // setText leaves it at the end, scrolled there
        }
        row.name->setToolTip(name + "\nThe name formulas use: letters, digits and underscores, starting with a letter.");
        row.field->show(doc.get(row.parameter).value, doc, shapes);
        QString words;
        for (const Error& error : shapes.errors)
            if (const Param* where = std::get_if<Param>(&error.where); where && *where == row.parameter) words = QString::fromStdString(error.message);
        row.problem->setText(words);
        row.problem->setVisible(!words.isEmpty());
    }
}

ValueField* ParametersPanel::field(Param parameter) const {
    for (const Row& row : rows_)
        if (row.parameter == parameter) return row.field;
    return nullptr;
}

QLineEdit* ParametersPanel::nameField(Param parameter) const {
    for (const Row& row : rows_)
        if (row.parameter == parameter) return row.name;
    return nullptr;
}

QString ParametersPanel::problemShown(Param parameter) const {
    for (const Row& row : rows_)
        if (row.parameter == parameter) return row.problem->text();
    return {};
}

void ParametersPanel::remove(Param parameter) { emit removeRequested(parameter); }

} // namespace clothcad::ui
