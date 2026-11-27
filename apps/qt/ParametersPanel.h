#pragma once
// The document's parameters: a name and a value with its unit each, a
// formula in teal, a bin to remove one, and a parameter's problem in red
// under it. Typing and clicking are only reported.
//
//   parameters->show(doc, shapes);
//   connect(parameters, &ParametersPanel::valueTyped, ...);

#include <QWidget>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/session/Drafting.h"

class QGridLayout;
class QLabel;
class QLineEdit;

namespace clothcad::ui {

class NameField;
class ValueField;

class ParametersPanel : public QWidget {
    Q_OBJECT
public:
    explicit ParametersPanel(QWidget* parent = nullptr);
    void show(const Document& doc, const Result& shapes);
    ValueField* field(Param parameter) const;  // for tests and for refusals
    QLineEdit* nameField(Param parameter) const;
    QString problemShown(Param parameter) const;  // empty if none
    void remove(Param parameter);  // as a click on its bin

signals:
    void valueTyped(clothcad::Param parameter, QString text);
    void valueDragged(clothcad::Param parameter, double value, clothcad::session::DragPhase phase);
    void renamed(clothcad::Param parameter, QString name);
    void removeRequested(clothcad::Param parameter);
    void addRequested();

private:
    QGridLayout* grid_;
    struct Row {
        Param parameter;
        NameField* name;
        ValueField* field;
        QLabel* problem;
    };
    std::vector<Row> rows_;
};

} // namespace clothcad::ui
