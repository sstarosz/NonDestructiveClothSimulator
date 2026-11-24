#include "Outliner.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>

#include "Icons.h"
#include "clothcad/core/Fields.h"

namespace clothcad::ui {

namespace {

constexpr int panelRole = Qt::UserRole + 1;
constexpr int baseOnlyRole = Qt::UserRole + 2;  // a panel kept as a base for instances, not cut from cloth
constexpr int seamRole = Qt::UserRole + 3;

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
    setColumnCount(2);
    header()->hide();
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
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
        if (showing_ || !item || !item->data(0, panelRole).isValid()) return;
        emit panelChosen(PanelId{item->data(0, panelRole).toUInt()});
    });
    connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (item == body_ && column == 1 && !bodyNow_.name.isEmpty()) emit bodyShownChanged(!bodyNow_.shown);
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
            menu.addAction(icon(Icon::Delete, theme::error), "Delete", [this, seam] { emit seamDeleteRequested(*seam); });
            menu.exec(viewport()->mapToGlobal(where));
            return;
        }
        const std::optional<PanelId> panel = panelOf(item);
        if (!panel) return;
        QMenu menu(this);
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
    body_->setIcon(1, none ? QIcon() : icon(body.shown ? Icon::Eye : Icon::EyeShut, theme::quiet));
    body_->setToolTip(1, none ? QString() : body.shown ? "Shown. Click to hide it; hidden, it is also out of the simulation." : "Hidden. Click to show it.");
}

QString Outliner::bodyShown() const { return body_->text(0); }

void Outliner::show(const Document& doc, const Result& shapes, std::optional<PanelId> selected, const Body& body) {
    showBody(body);
    // Everything the rows say. The window shows the document again after every
    // change, each step of a drag included; rows that would come out the same
    // are left alone, so that nothing is made or painted again.
    const std::vector<PanelId> panels = doc.panels();
    std::string signature = selected ? std::to_string(selected->value) : "-";
    for (PanelId panel : panels) {
        QColor colour;
        const Icon picture = iconOf(doc, panel, colour);
        bool failed = false;
        for (OpId op : doc.operations(panel)) failed = failed || shapes.failed(op);
        signature += "|p" + std::to_string(panel.value) + doc.get(panel).name + "," + std::to_string(doc.operations(panel).size()) + "," + std::to_string(static_cast<int>(picture)) +
                     colour.name().toStdString() + (doc.get(panel).construction ? "c" : "") + (failed ? "f" : "");
    }
    for (SeamId id : doc.seams()) {
        const Seam& seam = doc.get(id);
        signature += "|s" + std::to_string(id.value) + nameOf(Reference(seam.a.edge), doc) + nameOf(Reference(seam.b.edge), doc);
        for (const SeamSide& side : {seam.a, seam.b}) signature += doc.contains(side.panel) ? doc.get(side.panel).name : "?";
    }
    // Rows the user has been typing in, or has selected, are not as the document has them.
    bool asShown = signature == shown_ && panels_->childCount() == static_cast<int>(panels.size()) && (selected ? panelOf(currentItem()) == selected : currentItem() == nullptr);
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
            if (shapes.failed(op)) item->setForeground(0, theme::error);
        if (selected && *selected == panel) setCurrentItem(item);
    }
    for (SeamId id : doc.seams()) {
        const Seam& seam = doc.get(id);
        const auto side = [&](const SeamSide& s) {
            return doc.contains(s.panel) ? doc.get(s.panel).name : std::string("?");
        };
        auto* item = new QTreeWidgetItem(seams_, {QString::fromStdString(side(seam.a) + "  ↔  " + side(seam.b))});
        item->setForeground(0, theme::seam);
        item->setData(0, seamRole, id.value);
        item->setToolTip(0, QString::fromStdString(nameOf(Reference(seam.a.edge), doc) + "\n" + nameOf(Reference(seam.b.edge), doc)));
    }
    seams_->setText(1, QString::number(doc.seams().size()));
    seams_->setForeground(1, theme::quiet);
    if (!selected) setCurrentItem(nullptr);
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
