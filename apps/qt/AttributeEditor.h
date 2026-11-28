#pragma once
// The selected panel, as Maya's Attribute Editor: where it is in space and
// on the table, its seams (a seam the cloth could not close says so on its
// card), and at the bottom its operations as layers (newest
// on top, the base shape last). Every layer is open, showing its fields; a
// click on its name closes it. The eye switches one off, dragging a name
// moves the layer, the bin removes it, Alt+click on a name shows the panel as
// it was at that step (Escape shows all steps again), and a layer's problem
// shows in red under it. Everything typed or clicked is only reported.
//
//   editor->show(doc, shapes, panel, viewedStep);
//   connect(editor, &AttributeEditor::fieldTyped, ...);

#include <QPointer>
#include <QScrollArea>
#include <optional>

#include "twintailor/core/Document.h"
#include "twintailor/core/Evaluate.h"
#include "twintailor/core/Fields.h"
#include "twintailor/session/Drafting.h"
#include "twintailor/session/Offers.h"
#include "twintailor/sim/Cloth.h"

class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

namespace twintailor::ui {

class Section;
class ValueField;

class AttributeEditor : public QScrollArea {
    Q_OBJECT
public:
    explicit AttributeEditor(QWidget* parent = nullptr);
    // `viewed`: the step the panel is shown as of, if not the last.
    void show(const Document& doc, const Result& shapes, std::optional<PanelId> panel, std::optional<OpId> viewed = std::nullopt);
    // How many particles, triangles and stitches the selected panel's cloth
    // has now; none while it has no mesh. The mesh follows the shape a few
    // frames late, so this is shown on its own.
    struct MeshCounts {
        std::size_t particles = 0, triangles = 0, stitches = 0;
        friend bool operator==(const MeshCounts&, const MeshCounts&) = default;
    };
    void showMesh(std::optional<MeshCounts> counts);
    // The seams the cloth could not close, as it hangs now: each one's card
    // says how far apart it is and what to do. Cheap to call every frame.
    void showOpenSeams(const std::vector<sim::OpenSeam>& open);
    QString openShown(SeamId seam) const;  // what its card says of it; empty if nothing

    // The layers as shown, top first; and whether one shows its fields.
    std::vector<OpId> layersShown() const { return layers_; }
    bool isOpen(OpId layer) const;
    void openLayer(OpId layer);              // shows its fields, if closed
    void toggleLayer(OpId layer);            // as a click on its name
    void switchLayer(OpId layer, bool on);   // as a click on its eye
    // As dragging its name: `to` is its place among the panel's operations, oldest first.
    void moveLayer(OpId layer, std::size_t to);
    void removeLayer(OpId layer);            // as a click on its bin
    void viewStep(OpId layer);               // as Alt+click on its name
    void showAllSteps();                     // as Escape while looking back
    QString problemShown(OpId layer) const;  // the red words under a layer; empty if none
    // What "+ Add operation" offers now, and choosing one as from its menu.
    std::vector<session::Offer> offered() const;
    void addOperation(session::Offer offer);
    std::vector<SeamId> seamsShown() const { return seams_; }
    void reverseSeam(SeamId seam, bool reversed);  // as a click on its twist button
    ValueField* field(OpId layer, FieldId field) const;  // of an open layer
    ValueField* spaceField(session::SpaceField field) const;
    ValueField* tableField(session::TableField field) const;
    ValueField* particleDistance() const { return distance_; }
    QString meshShown() const;  // the words under the distance, empty if none

signals:
    void layerSwitched(twintailor::OpId layer, bool on);
    // A layer dragged to a place among the panel's operations (oldest first).
    void layerMoved(twintailor::OpId layer, std::size_t to);
    void layerRemoved(twintailor::OpId layer);
    // Show the panel as of a step; empty: as it is now, with every step.
    void stepViewed(std::optional<twintailor::OpId> layer);
    void fieldTyped(twintailor::OpId layer, twintailor::FieldId field, QString text);
    // A number of the panel's place in space, or on the table.
    void spaceTyped(twintailor::PanelId panel, twintailor::session::SpaceField field, QString text);
    void tableTyped(twintailor::PanelId panel, twintailor::session::TableField field, QString text);
    // The same fields dragged: the number, in the field's unit, and where in one drag it is.
    void fieldDragged(twintailor::OpId layer, twintailor::FieldId field, double value, twintailor::session::DragPhase phase);
    void spaceDragged(twintailor::PanelId panel, twintailor::session::SpaceField field, double value, twintailor::session::DragPhase phase);
    void tableDragged(twintailor::PanelId panel, twintailor::session::TableField field, double value, twintailor::session::DragPhase phase);
    // The panel's particle distance, typed or dragged.
    void particleDistanceTyped(twintailor::PanelId panel, QString text);
    void particleDistanceDragged(twintailor::PanelId panel, double value, twintailor::session::DragPhase phase);
    // A layer was opened or closed: show again.
    void layerOpened();
    // The name of an edge, corner or line a layer uses was clicked: choose another on the table.
    void referenceClicked(twintailor::OpId layer, twintailor::ReferenceId reference);
    // A panel a layer uses (an instance's source) was chosen from the list.
    void panelReferenceChosen(twintailor::OpId layer, twintailor::ReferenceId reference, twintailor::PanelId panel);
    // The open layer's choices: which side a cut keeps, which end a length keeps, a mirror applied last, an instance mirrored.
    void cutSideChosen(twintailor::OpId layer, twintailor::Cut::Side side);
    void lengthKeepingChosen(twintailor::OpId layer, twintailor::SetLength::Keeping keeping);
    void mirrorLastChanged(twintailor::OpId layer, bool last);
    void instanceMirroredChanged(twintailor::OpId layer, bool mirrored);
    void seamReversed(twintailor::SeamId seam, bool reversed);
    // An operation chosen from the "+ Add operation" list.
    void operationOffered(twintailor::PanelId panel, twintailor::session::Offer offer);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;  // dragging a layer by its name

private:
    void rebuildLayers(const Document& doc, const Result& shapes);
    void rebuildSeams(const Document& doc, const Result& shapes);
    // Where a layer dragged with its middle at height `y` (in the layers' coordinates) would go:
    // its place among the operations, and the layer it would sit just above.
    struct Drop {
        std::size_t to;
        OpId above;
    };
    std::optional<Drop> dropAt(OpId layer, int y) const;
    QRect rowOf(OpId layer) const;            // where the layout puts a layer's row
    void makeRoom(QWidget* row, OpId above);  // the other rows slide to make a gap for it
    void endDrag(bool drop);

    QWidget* page_;
    Section* layersSection_;
    QScrollArea* layerScroll_;  // the layers scroll on their own
    QWidget* layerPage_;        // what scrolls: the rows are its children
    QVBoxLayout* layerRows_;
    Section* spaceSection_;
    Section* tableSection_;
    Section* particlesSection_;
    ValueField* distance_;
    QLabel* meshCounts_;
    Section* seamsSection_;
    QVBoxLayout* seamRows_;
    std::vector<SeamId> seams_;
    std::string shownSeams_;  // what the seam rows were made from
    // The words on a seam's card that change with a number alone: these are
    // set again in place, so that the card is not made again for each step of a drag.
    struct SeamCard {
        QLabel* lengths;
        QLabel* gathered;
        QLabel* open;  // hidden while there is nothing to say; none for a broken seam
    };
    std::vector<SeamCard> seamCards_;  // in the order of seams_
    std::vector<sim::OpenSeam> openSeams_;
    void sayOpenSeams();
    // A layer's summary (its first value, as the row shows it), for the same reason.
    struct LayerSummary {
        OpId layer;
        QLabel* label;
    };
    std::vector<LayerSummary> summaries_;
    std::vector<ValueField*> space_;  // in the order of session::SpaceField
    std::vector<ValueField*> table_;  // in the order of session::TableField
    struct OpenField {
        OpId layer;
        FieldId id;
        ValueField* value;
    };
    std::vector<OpenField> openFields_;

    std::optional<PanelId> panel_;
    std::vector<OpId> layers_;
    std::vector<OpId> made_;   // the same, oldest first: the order they are applied in
    std::vector<OpId> onTop_;  // the mirrors applied last: they stay on top
    std::vector<OpId> closed_;  // layers the user closed; every other one shows its fields
    // A layer being dragged by its name: its row stays as a dim gap while a picture of it follows the mouse.
    struct Dragging {
        OpId layer;
        QPoint from;
        bool started = false;
        QPointer<QWidget> row;
        QPointer<QLabel> picture;
        int grabbed = 0;  // how far below the top of the row the mouse took hold
        std::optional<Drop> drop;
    };
    std::optional<Dragging> dragging_;
    std::vector<QPointer<QPropertyAnimation>> slides_;
    std::optional<OpId> viewed_;
    std::vector<std::pair<OpId, QString>> problems_;  // as shown under the layers
    std::vector<session::Offered> offers_;             // what "+ Add operation" offers
    std::string shownLayers_;  // what the layer rows were made from, to make them again only when it changes
};

} // namespace twintailor::ui
