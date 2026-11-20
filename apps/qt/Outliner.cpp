#include "Outliner.h"

#include <QHeaderView>

#include "Icons.h"
#include "clothcad/core/Fields.h"

namespace clothcad::ui {

namespace {

constexpr int panelRole = Qt::UserRole + 1;

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
    body_ = new QTreeWidgetItem(this, {"Mannequin"});
    body_->setIcon(0, icon(Icon::Body, theme::quiet));
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
}

void Outliner::show(const Document& doc, const Result& shapes, std::optional<PanelId> selected, const QString& body) {
    showing_ = true;
    body_->setText(0, body);
    qDeleteAll(panels_->takeChildren());
    qDeleteAll(seams_->takeChildren());
    for (PanelId panel : doc.panels()) {
        QColor colour;
        const Icon picture = iconOf(doc, panel, colour);
        auto* item = new QTreeWidgetItem(panels_, {QString::fromStdString(doc.get(panel).name), QString::number(doc.operations(panel).size())});
        item->setIcon(0, icon(picture, colour));
        item->setData(0, panelRole, panel.value);
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
        item->setToolTip(0, QString::fromStdString(nameOf(Reference(seam.a.edge), doc) + "\n" + nameOf(Reference(seam.b.edge), doc)));
    }
    seams_->setText(1, QString::number(doc.seams().size()));
    seams_->setForeground(1, theme::quiet);
    if (!selected) setCurrentItem(nullptr);
    showing_ = false;
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
