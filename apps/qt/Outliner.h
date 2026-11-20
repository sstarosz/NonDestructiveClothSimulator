#pragma once
// The scene as a tree, as Maya's Outliner: the body, the panels, the seams.
// A panel's icon says what it is: mirrored (pink), an instance (violet).
// Choosing a panel is only reported.
//
//   outliner->show(doc, shapes, selected, "Mannequin");
//   connect(outliner, &Outliner::panelChosen, ...);

#include <QTreeWidget>
#include <optional>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"

namespace clothcad::ui {

class Outliner : public QTreeWidget {
    Q_OBJECT
public:
    explicit Outliner(QWidget* parent = nullptr);
    void show(const Document& doc, const Result& shapes, std::optional<PanelId> selected, const QString& body);

    // For tests and the window: the panels in the order shown.
    std::vector<PanelId> panelsShown() const;
    void choose(PanelId panel);  // as a click on its row

signals:
    void panelChosen(clothcad::PanelId panel);

private:
    QTreeWidgetItem* body_;
    QTreeWidgetItem* panels_;
    QTreeWidgetItem* seams_;
    bool showing_ = false;  // set while the tree is filled, so that it reports nothing
};

} // namespace clothcad::ui
