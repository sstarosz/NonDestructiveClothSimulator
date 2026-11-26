#pragma once
// The scene as a tree, as Maya's Outliner: the body, the panels, the seams.
// A panel's icon says what it is: mirrored (pink), an instance (violet).
// The body's row has two buttons, also in its menu: an eye, to hide the body
// (it is then also out of the simulation) or show it again, and one to make it
// see-through, so that cloth inside it shows.
// Choosing, renaming (double click or F2), copying, making it base only and
// deleting a panel (its context menu, Delete) are only reported, as is
// deleting a seam (its context menu, Delete). Choosing a seam's row is
// reported too: the views light that seam up.
// A panel's row has two buttons, also in its menu: an eye, to hide it in 3D,
// and one to deactivate it (take it out of the simulation). A base only panel
// has neither. A seam's row has an eye that hides its lines in 3D, and the eye
// on "Seams" does so for all of them. A click on a button leaves the selection
// as it was.
//
//   outliner->show(doc, shapes, selected, {.name = "avatarV1.obj"});
//   connect(outliner, &Outliner::panelChosen, ...);

#include <QTreeWidget>
#include <optional>
#include <string>
#include <vector>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"

namespace clothcad::ui {

class Outliner : public QTreeWidget {
    Q_OBJECT
public:
    explicit Outliner(QWidget* parent = nullptr);
    // The body as the row shows it: its name (empty: none), whether it is
    // drawn and simulated, whether it is ready to collide with, and whether it
    // is drawn see-through.
    struct Body {
        QString name;
        bool shown = true;
        bool ready = true;
        bool seeThrough = false;
        friend bool operator==(const Body&, const Body&) = default;
    };
    void show(const Document& doc, const Result& shapes, std::optional<PanelId> selected, const Body& body, std::optional<SeamId> seam = {});
    void showBody(const Body& body);  // the body's row alone
    // Which panels are hidden in 3D and which are deactivated: their rows say so.
    void showPanelStates(const std::vector<PanelId>& hidden, const std::vector<PanelId>& inactive);
    void showSeamStates(const std::vector<SeamId>& hidden);  // which seams have no lines in 3D
    // The columns of the buttons: the eye (the body's and the panels') and activation,
    // which on the body's row is where it is made see-through.
    static constexpr int eyeColumn = 2, activeColumn = 3, seeThroughColumn = activeColumn;
    QString bodyShown() const;        // the words on its row

    // For tests and the window: the panels in the order shown.
    std::vector<PanelId> panelsShown() const;
    void choose(PanelId panel);                   // as a click on its row
    void rename(PanelId panel, const QString& name);  // as typing a name into its row
    void copy(PanelId panel) { emit copyRequested(panel); }      // as "Copy" in its menu
    void remove(PanelId panel) { emit deleteRequested(panel); }  // as "Delete" in its menu
    void setBaseOnly(PanelId panel, bool on);                    // as ticking "Base only" in its menu
    void setBodyShown(bool shown) { emit bodyShownChanged(shown); }  // as a click on the body's eye
    QString bodySeeThroughHint() const;                              // the words on the row's other button
    std::vector<SeamId> seamsShown() const;
    void removeSeam(SeamId seam) { emit seamDeleteRequested(seam); }  // as "Delete" in a seam's menu

signals:
    void panelChosen(clothcad::PanelId panel);
    void seamChosen(clothcad::SeamId seam);
    void renamed(clothcad::PanelId panel, QString name);
    void copyRequested(clothcad::PanelId panel);
    void deleteRequested(clothcad::PanelId panel);
    // Base only: a panel kept as a base for instances, not cut from cloth.
    void baseOnlyChanged(clothcad::PanelId panel, bool baseOnly);
    void seamDeleteRequested(clothcad::SeamId seam);
    // The body's eye was clicked: shown or hidden.
    void bodyShownChanged(bool shown);
    void bodySeeThroughChanged(bool seeThrough);
    void panelShownChanged(clothcad::PanelId panel, bool shown);
    void panelActiveChanged(clothcad::PanelId panel, bool active);
    void seamShownChanged(clothcad::SeamId seam, bool shown);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::optional<PanelId> panelOf(const QTreeWidgetItem* item) const;
    std::optional<SeamId> seamOf(const QTreeWidgetItem* item) const;
    // A press on one of a row's buttons: does what it says. False: not on one.
    bool pressButton(const QMouseEvent* event);
    void showState(QTreeWidgetItem* item);  // a panel row's buttons and colour
    void showSeamRows();                    // the seams' eyes, and the one on "Seams"
    // The eye on "Seams": hides every seam, or shows them all when all are hidden.
    void toggleAllSeams();

    std::vector<PanelId> hidden_, inactive_;
    std::vector<SeamId> hiddenSeams_;
    bool onButton_ = false;  // the button held went down on a row's button: the tree hears nothing of it

    Body bodyNow_{.shown = false, .ready = false};  // unlike any body, so that the first one is shown
    QTreeWidgetItem* body_;
    QTreeWidgetItem* panels_;
    QTreeWidgetItem* seams_;
    bool showing_ = false;  // set while the tree is filled, so that it reports nothing
    std::string shown_;     // what the rows were made from: they are made again only when it changes
};

} // namespace clothcad::ui
