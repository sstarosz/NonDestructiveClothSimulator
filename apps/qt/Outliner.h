#pragma once
// The scene as a tree, as Maya's Outliner: the body, the panels, the seams.
// A panel's icon says what it is: mirrored (pink), an instance (violet).
// The body's row has an eye: click it, or use the row's menu, to hide the
// body (it is then also out of the simulation) or show it again.
// Choosing, renaming (double click or F2), copying, making it base only and
// deleting a panel (its context menu, Delete) are only reported, as is
// deleting a seam (its context menu, Delete).
//
//   outliner->show(doc, shapes, selected, {.name = "avatarV1.obj"});
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
    // The body as the row shows it: its name (empty: none), whether it is
    // drawn and simulated, and whether it is ready to collide with.
    struct Body {
        QString name;
        bool shown = true;
        bool ready = true;
        friend bool operator==(const Body&, const Body&) = default;
    };
    void show(const Document& doc, const Result& shapes, std::optional<PanelId> selected, const Body& body);
    void showBody(const Body& body);  // the body's row alone
    QString bodyShown() const;        // the words on its row

    // For tests and the window: the panels in the order shown.
    std::vector<PanelId> panelsShown() const;
    void choose(PanelId panel);                   // as a click on its row
    void rename(PanelId panel, const QString& name);  // as typing a name into its row
    void copy(PanelId panel) { emit copyRequested(panel); }      // as "Copy" in its menu
    void remove(PanelId panel) { emit deleteRequested(panel); }  // as "Delete" in its menu
    void setBaseOnly(PanelId panel, bool on);                    // as ticking "Base only" in its menu
    void setBodyShown(bool shown) { emit bodyShownChanged(shown); }  // as a click on the body's eye
    std::vector<SeamId> seamsShown() const;
    void removeSeam(SeamId seam) { emit seamDeleteRequested(seam); }  // as "Delete" in a seam's menu

signals:
    void panelChosen(clothcad::PanelId panel);
    void renamed(clothcad::PanelId panel, QString name);
    void copyRequested(clothcad::PanelId panel);
    void deleteRequested(clothcad::PanelId panel);
    // Base only: a panel kept as a base for instances, not cut from cloth.
    void baseOnlyChanged(clothcad::PanelId panel, bool baseOnly);
    void seamDeleteRequested(clothcad::SeamId seam);
    // The body's eye was clicked: shown or hidden.
    void bodyShownChanged(bool shown);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    std::optional<PanelId> panelOf(const QTreeWidgetItem* item) const;
    std::optional<SeamId> seamOf(const QTreeWidgetItem* item) const;

    Body bodyNow_{.shown = false, .ready = false};  // unlike any body, so that the first one is shown
    QTreeWidgetItem* body_;
    QTreeWidgetItem* panels_;
    QTreeWidgetItem* seams_;
    bool showing_ = false;  // set while the tree is filled, so that it reports nothing
};

} // namespace clothcad::ui
