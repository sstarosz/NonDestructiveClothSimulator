#include "Outliner.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>

#include <algorithm>

#include "Icons.h"
#include "clothcad/core/Fields.h"

namespace clothcad::ui {

namespace {

constexpr int panelRole = Qt::UserRole + 1;
constexpr int baseOnlyRole = Qt::UserRole + 2;  // a panel kept as a base for instances, not cut from cloth
constexpr int seamRole = Qt::UserRole + 3;
constexpr int failedRole = Qt::UserRole + 4;  // a panel with a problem in one of its steps

template <class Id>
bool listed(const std::vector<Id>& list, Id id) { return std::find(list.begin(), list.end(), id) != list.end(); }

// What a panel is, for its icon: an instance follows another; a mirror makes two halves.
Icon iconOf(const Document& doc, PanelId panel, QColor& colour) {
    colour = theme::text;
    Icon picture = Icon::Rectangle;
    for (OpId op : doc.operations(panel)) {
        if (std::holds_alternative<Instance>(doc.get(op))) {
            colour = theme::instance;
            return Icon::Instance;
        }
        if (std::holds_alternative<Mirror>(doc.get(op))) {
            colour = theme::symmetry;
            picture = Icon::Mirror;
        }
    }
    return picture;
}

} // namespace

Outliner::Outliner(QWidget* parent) : QTreeWidget(parent) {
    setObjectName("outliner");
    setColumnCount(4);  // the name, how many steps, and the two buttons
    header()->hide();
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < columnCount(); ++column) header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    setRootIsDecorated(true);
    setIndentation(14);
    setFrameShape(QFrame::NoFrame);
    body_ = new QTreeWidgetItem(this, {"No body"});
    showBody({});
    panels_ = new QTreeWidgetItem(this, {"Panels"});
    panels_->setIcon(0, icon(Icon::Folder, theme::quiet));
    seams_ = new QTreeWidgetItem(this, {"Seams"});
    seams_->setIcon(0, icon(Icon::Sew, theme::seam));
    seams_->setForeground(0, theme::seam);
    panels_->setExpanded(true);
    connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (showing_) return;
        // The row may be made again while it is reported: nothing of it is used afterwards.
        if (const std::optional<PanelId> panel = panelOf(item)) emit panelChosen(*panel);
        else if (const std::optional<SeamId> seam = seamOf(item)) emit seamChosen(*seam);
    });
    // Only the name column of a panel is edited, and only by double click or F2.
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (column == 0 && panelOf(item)) editItem(item, 0);
    });
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (showing_ || column != 0) return;
        if (const std::optional<PanelId> panel = panelOf(item)) emit renamed(*panel, item->text(0));
    });
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& where) {
        QTreeWidgetItem* item = itemAt(where);
        if (item == body_ && !bodyNow_.name.isEmpty()) {
            QMenu menu(this);
            QAction* shown = menu.addAction("Shown", [this](bool on) { emit bodyShownChanged(on); });
            shown->setToolTip("Hidden, the body is also taken out of the simulation.");
            shown->setCheckable(true);
            shown->setChecked(bodyNow_.shown);
            menu.exec(viewport()->mapToGlobal(where));
            return;
        }
        if (const std::optional<SeamId> seam = seamOf(item)) {
            QMenu menu(this);
            QAction* shown = menu.addAction("Shown in 3D", [this, seam](bool on) { emit seamShownChanged(*seam, on); });
            shown->setCheckable(true);
            shown->setChecked(!listed(hiddenSeams_, *seam));
            menu.addSeparator();
            menu.addAction(icon(Icon::Delete, theme::error), "Delete", [this, seam] { emit seamDeleteRequested(*seam); });
            menu.exec(viewport()->mapToGlobal(where));
            return;
        }
        const std::optional<PanelId> panel = panelOf(item);
        if (!panel) return;
        QMenu menu(this);
        if (!item->data(0, baseOnlyRole).toBool()) {
            QAction* shown = menu.addAction("Shown in 3D", [this, panel](bool on) { emit panelShownChanged(*panel, on); });
            shown->setCheckable(true);
            shown->setChecked(!listed(hidden_, *panel));
            QAction* active = menu.addAction("Active in the simulation", [this, panel](bool on) { emit panelActiveChanged(*panel, on); });
            active->setCheckable(true);
            active->setChecked(!listed(inactive_, *panel));
            menu.addSeparator();
        }
        menu.addAction(icon(Icon::Copy, theme::text), "Copy", [this, panel] { emit copyRequested(*panel); });
        menu.addAction("Rename", [this, item] { editItem(item, 0); });
        QAction* baseOnly = menu.addAction("Base only: not cut from cloth", [this, panel](bool on) { emit baseOnlyChanged(*panel, on); });
        baseOnly->setToolTip("A base for instances to follow. It is not meshed or simulated.");
        baseOnly->setCheckable(true);
        baseOnly->setChecked(item->data(0, baseOnlyRole).toBool());
        menu.addAction(icon(Icon::Delete, theme::error), "Delete", [this, panel] { emit deleteRequested(*panel); });
        menu.exec(viewport()->mapToGlobal(where));
    });
}

std::optional<PanelId> Outliner::panelOf(const QTreeWidgetItem* item) const {
    if (!item || !item->data(0, panelRole).isValid()) return std::nullopt;
    return PanelId{item->data(0, panelRole).toUInt()};
}

std::optional<SeamId> Outliner::seamOf(const QTreeWidgetItem* item) const {
    if (!item || !item->data(0, seamRole).isValid()) return std::nullopt;
    return SeamId{item->data(0, seamRole).toUInt()};
}

void Outliner::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete && state() != EditingState) {
        if (const std::optional<PanelId> panel = panelOf(currentItem())) return emit deleteRequested(*panel);
        if (const std::optional<SeamId> seam = seamOf(currentItem())) return emit seamDeleteRequested(*seam);
    }
    QTreeWidget::keyPressEvent(event);
}

bool Outliner::pressButton(const QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return false;
    const QPoint at = event->position().toPoint();
    const QTreeWidgetItem* item = itemAt(at);
    const int column = columnAt(at.x());
    if (item == body_ && column == eyeColumn && !bodyNow_.name.isEmpty()) {
        emit bodyShownChanged(!bodyNow_.shown);
        return true;
    }
    if (column == eyeColumn) {
        if (item == seams_ && seams_->childCount() > 0) {
            toggleAllSeams();
            return true;
        }
        if (const std::optional<SeamId> seam = seamOf(item)) {
            emit seamShownChanged(*seam, listed(hiddenSeams_, *seam));
            return true;
        }
    }
    const std::optional<PanelId> panel = panelOf(item);
    if (!panel || item->data(0, baseOnlyRole).toBool()) return false;
    // What is reported may make the rows again: nothing of this one is used afterwards.
    if (column == eyeColumn) emit panelShownChanged(*panel, listed(hidden_, *panel));
    else if (column == activeColumn) emit panelActiveChanged(*panel, listed(inactive_, *panel));
    else return false;
    return true;
}

void Outliner::mousePressEvent(QMouseEvent* event) {
    onButton_ = pressButton(event);
    if (!onButton_) QTreeWidget::mousePressEvent(event);
}

// The second of two quick clicks on a button is a click like the first.
void Outliner::mouseDoubleClickEvent(QMouseEvent* event) {
    onButton_ = pressButton(event);
    if (!onButton_) QTreeWidget::mouseDoubleClickEvent(event);
}

void Outliner::mouseMoveEvent(QMouseEvent* event) {
    if (!onButton_ || !(event->buttons() & Qt::LeftButton)) QTreeWidget::mouseMoveEvent(event);
}

void Outliner::mouseReleaseEvent(QMouseEvent* event) {
    if (!std::exchange(onButton_, false)) QTreeWidget::mouseReleaseEvent(event);
}

void Outliner::showState(QTreeWidgetItem* item) {
    const std::optional<PanelId> panel = panelOf(item);
    if (!panel) return;
    const bool cut = !item->data(0, baseOnlyRole).toBool();  // a base only panel is not in 3D or in the cloth
    const bool shown = !listed(hidden_, *panel), active = !listed(inactive_, *panel);
    item->setIcon(eyeColumn, cut ? icon(shown ? Icon::Eye : Icon::EyeShut, theme::quiet) : QIcon());
    item->setToolTip(eyeColumn, !cut ? QString() : shown ? "Shown in 3D. Click to hide it; it stays in the simulation." : "Hidden in 3D. Click to show it.");
    item->setIcon(activeColumn, cut ? icon(active ? Icon::Active : Icon::Inactive, theme::quiet) : QIcon());
    item->setToolTip(activeColumn, !cut     ? QString()
                                   : active ? "Simulated. Click to deactivate it: it stays where it is and nothing touches it."
                                            : "Deactivated. Click to simulate it again.");
    if (item->data(0, failedRole).toBool()) item->setForeground(0, theme::error);
    else if (cut && !active) item->setForeground(0, theme::quiet);
    else item->setData(0, Qt::ForegroundRole, QVariant());
}

void Outliner::showPanelStates(const std::vector<PanelId>& hidden, const std::vector<PanelId>& inactive) {
    if (hidden == hidden_ && inactive == inactive_) return;
    hidden_ = hidden;
    inactive_ = inactive;
    const QSignalBlocker quiet(this);
    for (int i = 0; i < panels_->childCount(); ++i) showState(panels_->child(i));
}

void Outliner::toggleAllSeams() {
    // Read first: what is reported changes the rows.
    const std::vector<SeamId> seams = seamsShown();
    const std::vector<SeamId> hidden = hiddenSeams_;
    const bool show = std::all_of(seams.begin(), seams.end(), [&](SeamId seam) { return listed(hidden, seam); });
    for (const SeamId seam : seams)
        if (listed(hidden, seam) == show) emit seamShownChanged(seam, show);
}

void Outliner::showSeamRows() {
    bool any = false;
    for (int i = 0; i < seams_->childCount(); ++i) {
        QTreeWidgetItem* item = seams_->child(i);
        const std::optional<SeamId> seam = seamOf(item);
        if (!seam) continue;
        const bool shown = !listed(hiddenSeams_, *seam);
        any = any || shown;
        item->setIcon(eyeColumn, icon(shown ? Icon::Eye : Icon::EyeShut, theme::quiet));
        item->setToolTip(eyeColumn, shown ? "Its lines are shown in 3D while it is open or selected. Click to hide them." : "Hidden in 3D. Click to show it.");
        item->setForeground(0, shown ? theme::seam : theme::quiet);
    }
    const bool none = seams_->childCount() == 0;
    seams_->setIcon(eyeColumn, none ? QIcon() : icon(any ? Icon::Eye : Icon::EyeShut, theme::quiet));
    seams_->setToolTip(eyeColumn, none ? QString() : any ? "Click to hide the lines of every seam in 3D." : "Every seam is hidden in 3D. Click to show them.");
}

void Outliner::showSeamStates(const std::vector<SeamId>& hidden) {
    if (hidden == hiddenSeams_) return;
    hiddenSeams_ = hidden;
    const QSignalBlocker quiet(this);
    showSeamRows();
}

void Outliner::setBaseOnly(PanelId panel, bool on) { emit baseOnlyChanged(panel, on); }

void Outliner::rename(PanelId panel, const QString& name) {
    for (int i = 0; i < panels_->childCount(); ++i)
        if (panelOf(panels_->child(i)) == panel) panels_->child(i)->setText(0, name);
}

void Outliner::showBody(const Body& body) {
    if (body == bodyNow_) return;
    bodyNow_ = body;
    const bool none = body.name.isEmpty();
    const QSignalBlocker quiet(this);
    // Working out how to collide with a new avatar takes a moment; the cloth waits for it.
    body_->setText(0, none ? QString("No body") : body.ready ? body.name : body.name + "  ·  preparing…");
    body_->setIcon(0, icon(Icon::Body, theme::quiet));
    body_->setForeground(0, none || !body.shown ? theme::quiet : theme::text);
    body_->setIcon(eyeColumn, none ? QIcon() : icon(body.shown ? Icon::Eye : Icon::EyeShut, theme::quiet));
    body_->setToolTip(eyeColumn, none ? QString() : body.shown ? "Shown. Click to hide it; hidden, it is also out of the simulation." : "Hidden. Click to show it.");
}

QString Outliner::bodyShown() const { return body_->text(0); }

void Outliner::show(const Document& doc, const Result& shapes, std::optional<PanelId> selected, const Body& body, std::optional<SeamId> seam) {
    showBody(body);
    // Everything the rows say. The window shows the document again after every
    // change, each step of a drag included; rows that would come out the same
    // are left alone, so that nothing is made or painted again.
    const std::vector<PanelId> panels = doc.panels();
    std::string signature = selected ? std::to_string(selected->value) : "-";
    signature += seam ? "," + std::to_string(seam->value) : ",-";
    for (PanelId panel : panels) {
        QColor colour;
        const Icon picture = iconOf(doc, panel, colour);
        bool failed = false;
        for (OpId op : doc.operations(panel)) failed = failed || shapes.failed(op);
        signature += "|p" + std::to_string(panel.value) + doc.get(panel).name + "," + std::to_string(doc.operations(panel).size()) + "," + std::to_string(static_cast<int>(picture)) +
                     colour.name().toStdString() + (doc.get(panel).construction ? "c" : "") + (failed ? "f" : "");
    }
    for (SeamId id : doc.seams()) {
        const Seam& sewn = doc.get(id);
        signature += "|s" + std::to_string(id.value) + nameOf(Reference(sewn.a.edge), doc) + nameOf(Reference(sewn.b.edge), doc);
        for (const SeamSide& side : {sewn.a, sewn.b}) signature += doc.contains(side.panel) ? doc.get(side.panel).name : "?";
    }
    // Rows the user has been typing in, or has selected, are not as the document has them.
    const bool rowChosen = selected ? panelOf(currentItem()) == selected : seam ? seamOf(currentItem()) == seam : currentItem() == nullptr;
    bool asShown = signature == shown_ && panels_->childCount() == static_cast<int>(panels.size()) && rowChosen;
    for (int i = 0; asShown && i < panels_->childCount(); ++i) asShown = panels_->child(i)->text(0) == QString::fromStdString(doc.get(panels[static_cast<std::size_t>(i)]).name);
    if (asShown) return;
    shown_ = signature;
    showing_ = true;
    qDeleteAll(panels_->takeChildren());
    qDeleteAll(seams_->takeChildren());
    for (PanelId panel : doc.panels()) {
        QColor colour;
        const Icon picture = iconOf(doc, panel, colour);
        auto* item = new QTreeWidgetItem(panels_, {QString::fromStdString(doc.get(panel).name), QString::number(doc.operations(panel).size())});
        item->setIcon(0, icon(picture, colour));
        item->setData(0, panelRole, panel.value);
        item->setData(0, baseOnlyRole, doc.get(panel).construction);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setForeground(1, theme::quiet);
        // A panel with a problem in one of its steps says so in its colour.
        for (OpId op : doc.operations(panel))
            if (shapes.failed(op)) item->setData(0, failedRole, true);
        showState(item);
        if (selected && *selected == panel) setCurrentItem(item);
    }
    for (SeamId id : doc.seams()) {
        const Seam& sewn = doc.get(id);
        const auto side = [&](const SeamSide& s) {
            return doc.contains(s.panel) ? doc.get(s.panel).name : std::string("?");
        };
        auto* item = new QTreeWidgetItem(seams_, {QString::fromStdString(side(sewn.a) + "  ↔  " + side(sewn.b))});
        item->setData(0, seamRole, id.value);
        item->setToolTip(0, QString::fromStdString(nameOf(Reference(sewn.a.edge), doc) + "\n" + nameOf(Reference(sewn.b.edge), doc)));
        if (seam && *seam == id) setCurrentItem(item);
    }
    seams_->setText(1, QString::number(doc.seams().size()));
    seams_->setForeground(1, theme::quiet);
    showSeamRows();
    if (!selected && !seam) setCurrentItem(nullptr);
    showing_ = false;
}

std::vector<SeamId> Outliner::seamsShown() const {
    std::vector<SeamId> shown;
    for (int i = 0; i < seams_->childCount(); ++i) shown.push_back(SeamId{seams_->child(i)->data(0, seamRole).toUInt()});
    return shown;
}

std::vector<PanelId> Outliner::panelsShown() const {
    std::vector<PanelId> shown;
    for (int i = 0; i < panels_->childCount(); ++i) shown.push_back(PanelId{panels_->child(i)->data(0, panelRole).toUInt()});
    return shown;
}

void Outliner::choose(PanelId panel) {
    for (int i = 0; i < panels_->childCount(); ++i)
        if (panels_->child(i)->data(0, panelRole).toUInt() == panel.value) setCurrentItem(panels_->child(i));
}

} // namespace clothcad::ui
