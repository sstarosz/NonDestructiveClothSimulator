#pragma once
// The selected panel, as Maya's Attribute Editor: its name, its operations
// as layers (newest on top, the base shape at the bottom; the eye switches
// one off; the chosen one opens its fields; the arrows move one up or down,
// the bin removes it, the clock shows the panel as it was at that step, and
// a layer's problem shows in red under it), where it is in space and on
// the table, and its seams. Everything typed or clicked is only reported.
//
//   editor->show(doc, shapes, panel, viewedStep);
//   connect(editor, &AttributeEditor::fieldTyped, ...);

#include <QScrollArea>
#include <optional>

#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/core/Fields.h"
#include "clothcad/session/Drafting.h"
#include "clothcad/session/Offers.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace clothcad::ui {

class Section;
class ValueField;

class AttributeEditor : public QScrollArea {
    Q_OBJECT
public:
    explicit AttributeEditor(QWidget* parent = nullptr);
    // `viewed`: the step the panel is shown as of, if not the last.
    void show(const Document& doc, const Result& shapes, std::optional<PanelId> panel, std::optional<OpId> viewed = std::nullopt);

    // The layers as shown, top first; and which one is open.
    std::vector<OpId> layersShown() const { return layers_; }
    std::optional<OpId> openLayer() const { return open_; }
    void openLayer(OpId layer);              // as a click on its name
    void switchLayer(OpId layer, bool on);   // as a click on its eye
    void moveLayer(OpId layer, bool up);     // as a click on its arrow
    void removeLayer(OpId layer);            // as a click on its bin
    void viewStep(OpId layer);               // as a click on its clock
    QString problemShown(OpId layer) const;  // the red words under a layer; empty if none
    // What "+ Add operation" offers now, and choosing one as from its menu.
    std::vector<session::Offer> offered() const;
    void addOperation(session::Offer offer);
    std::vector<SeamId> seamsShown() const { return seams_; }
    void reverseSeam(SeamId seam, bool reversed);  // as a click on its tick
    void removeSeam(SeamId seam);                  // as a click on its Remove
    ValueField* field(FieldId field) const;  // of the open layer
    ValueField* spaceField(int which) const;
    ValueField* tableField(int which) const;

signals:
    void renamed(clothcad::PanelId panel, QString name);
    void layerSwitched(clothcad::OpId layer, bool on);
    // Move a layer one step: `later` up the list (applied after the next one), else down.
    void layerMoved(clothcad::OpId layer, bool later);
    void layerRemoved(clothcad::OpId layer);
    // Show the panel as of a step; empty: as it is now, with every step.
    void stepViewed(std::optional<clothcad::OpId> layer);
    void fieldTyped(clothcad::OpId layer, clothcad::FieldId field, QString text);
    // In space: 0 x, 1 y, 2 z, 3 turn, 4 tilt, 5 roll, 6 wrap. On the table: 0 x, 1 y, 2 angle.
    void spaceTyped(clothcad::PanelId panel, int which, QString text);
    // Placed in space again where the table has it, as a new panel is.
    void placeFromTableRequested(clothcad::PanelId panel);
    void tableTyped(clothcad::PanelId panel, int which, QString text);
    // The same fields dragged: the number, in the field's unit, and where in one drag it is.
    void fieldDragged(clothcad::OpId layer, clothcad::FieldId field, double value, clothcad::session::DragPhase phase);
    void spaceDragged(clothcad::PanelId panel, int which, double value, clothcad::session::DragPhase phase);
    void tableDragged(clothcad::PanelId panel, int which, double value, clothcad::session::DragPhase phase);
    // A layer was opened or closed: show again.
    void layerOpened();
    void copyRequested(clothcad::PanelId panel);
    void instanceRequested(clothcad::PanelId panel);
    void mirroredInstanceRequested(clothcad::PanelId panel);
    // Every formula of the panel replaced by the number it comes to now.
    void bakeRequested(clothcad::PanelId panel);
    void deleteRequested(clothcad::PanelId panel);
    // Base only: a panel kept as a base for instances, not cut from cloth.
    void baseOnlyChanged(clothcad::PanelId panel, bool baseOnly);
    // The name of an edge, corner or line a layer uses was clicked: choose another on the table.
    void referenceClicked(clothcad::OpId layer, clothcad::ReferenceId reference);
    // A panel a layer uses (an instance's source) was chosen from the list.
    void panelReferenceChosen(clothcad::OpId layer, clothcad::ReferenceId reference, clothcad::PanelId panel);
    // The open layer's choices: which side a cut keeps, a mirror applied last, an instance mirrored.
    void cutSideChosen(clothcad::OpId layer, clothcad::Cut::Side side);
    void mirrorLastChanged(clothcad::OpId layer, bool last);
    void instanceMirroredChanged(clothcad::OpId layer, bool mirrored);
    void seamReversed(clothcad::SeamId seam, bool reversed);
    void seamRemoved(clothcad::SeamId seam);
    // An operation chosen from the "+ Add operation" list.
    void operationOffered(clothcad::PanelId panel, clothcad::session::Offer offer);

private:
    void rebuildLayers(const Document& doc, const Result& shapes);
    void rebuildSeams(const Document& doc, const Result& shapes);

    QWidget* page_;
    QLineEdit* name_;
    QCheckBox* baseOnly_;
    Section* layersSection_;
    QVBoxLayout* layerRows_;
    Section* spaceSection_;
    Section* tableSection_;
    Section* seamsSection_;
    QVBoxLayout* seamRows_;
    std::vector<SeamId> seams_;
    std::string shownSeams_;  // what the seam rows were made from
    std::vector<ValueField*> space_;  // x y z turn tilt roll wrap
    std::vector<ValueField*> table_;  // x y angle
    std::vector<std::pair<FieldId, ValueField*>> openFields_;

    std::optional<PanelId> panel_;
    std::vector<OpId> layers_;
    std::optional<OpId> open_;
    std::optional<OpId> viewed_;
    std::vector<std::pair<OpId, QString>> problems_;  // as shown under the layers
    std::vector<session::Offered> offers_;             // what "+ Add operation" offers
    std::string shownLayers_;  // what the layer rows were made from, to make them again only when it changes
};

} // namespace clothcad::ui
