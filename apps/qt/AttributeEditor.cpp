#include "AttributeEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "Section.h"
#include "ValueField.h"
#include "clothcad/core/Text.h"

namespace clothcad::ui {

namespace {

Icon iconOf(const OpData& op) {
    return std::visit([](const auto& typed) {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, Rect>) return Icon::Rectangle;
        else if constexpr (std::is_same_v<T, Circle>) return Icon::Circle;
        else if constexpr (std::is_same_v<T, RingSector>) return Icon::Ring;
        else if constexpr (std::is_same_v<T, Instance>) return Icon::Instance;
        else if constexpr (std::is_same_v<T, Offcut>) return Icon::Split;
        else if constexpr (std::is_same_v<T, Curve> || std::is_same_v<T, Bezier>) return Icon::Curve;
        else if constexpr (std::is_same_v<T, Line> || std::is_same_v<T, LineBetween>) return Icon::Line;
        else if constexpr (std::is_same_v<T, Cut>) return Icon::Cut;
        else if constexpr (std::is_same_v<T, Mirror>) return Icon::Mirror;
        else if constexpr (std::is_same_v<T, AddPoint>) return Icon::Point;
        else return Icon::Select;  // AdjustPoint: a corner moved
    }, op);
}

bool appliedLast(const OpData& op) {
    const Mirror* mirror = std::get_if<Mirror>(&op);
    return mirror && mirror->appliedLast;
}

// A layer's value at a glance: its first field with its unit.
QString summaryOf(const OpData& op, const Document& doc, const Result& shapes) {
    if (appliedLast(op)) return "on top";
    const std::vector<Field> all = fields(op);
    if (all.empty()) return {};
    const std::optional<double> value = valueOf(all[0].value, shapes);
    if (!value) return "?";
    QString text = QString::fromStdString(formatValue(*value, all[0].unit));
    if (all.size() == 2 && all[0].unit == Unit::Length && all[1].unit == Unit::Length && std::string_view(label(op)) == "Rectangle")
        if (const std::optional<double> second = valueOf(all[1].value, shapes))
            text = QString::fromStdString(formatValue(*value, Unit::None) + " × " + formatValue(*second, Unit::Length));
    (void)doc;
    return text;
}

// What a layer uses and chooses, in words: when it changes (undo, a choice),
// the rows are made again so that they show it.
std::string choicesOf(const OpData& op, const Document& doc) {
    std::string words;
    for (const ReferenceField& reference : references(op)) words += "|" + nameOf(reference.value, doc);
    if (const Cut* cut = std::get_if<Cut>(&op)) words += cut->keep == Cut::Side::Left ? "|L" : "|R";
    if (const Mirror* mirror = std::get_if<Mirror>(&op)) words += mirror->appliedLast ? "|last" : "|inplace";
    if (const Instance* instance = std::get_if<Instance>(&op)) words += instance->mirrored ? "|mirrored" : "|plain";
    return words;
}

QToolButton* smallButton(Icon picture, QColor colour, const QString& tip, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setIcon(icon(picture, colour));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    return button;
}

} // namespace

AttributeEditor::AttributeEditor(QWidget* parent) : QScrollArea(parent) {
    setObjectName("attributeEditor");
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    page_ = new QWidget(this);
    auto* column = new QVBoxLayout(page_);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // The name, and what can be done to the whole panel.
    auto* top = new QWidget(page_);
    auto* topRow = new QHBoxLayout(top);
    topRow->setContentsMargins(8, 6, 6, 6);
    name_ = new QLineEdit(top);
    name_->setObjectName("panelName");
    topRow->addWidget(name_, 1);
    QToolButton* copy = smallButton(Icon::Copy, theme::text, "Copy", top);
    QToolButton* instance = smallButton(Icon::Instance, theme::instance, "Instance: a panel that follows this one entirely", top);
    QToolButton* mirrored = smallButton(Icon::Mirror, theme::instance, "Mirrored instance: follows this one, flipped left to right", top);
    QToolButton* bake = smallButton(Icon::Lock, theme::formula,
                                    "Bake: replace every formula of this panel with the number it comes to now,\nso that it follows no parameter. Useful on a copy.", top);
    QToolButton* remove = smallButton(Icon::Delete, theme::error, "Delete", top);
    copy->setObjectName("copyPanel");
    instance->setObjectName("instancePanel");
    mirrored->setObjectName("mirroredInstancePanel");
    bake->setObjectName("bakePanel");
    remove->setObjectName("deletePanel");
    topRow->addWidget(copy);
    topRow->addWidget(instance);
    topRow->addWidget(mirrored);
    topRow->addWidget(bake);
    topRow->addWidget(remove);
    column->addWidget(top);
    baseOnly_ = new QCheckBox("Base only: not cut from cloth", page_);
    baseOnly_->setObjectName("baseOnly");
    baseOnly_->setToolTip("A base for instances to follow. It is not meshed or simulated.");
    baseOnly_->setContentsMargins(8, 0, 8, 4);
    column->addWidget(baseOnly_);
    connect(baseOnly_, &QCheckBox::clicked, this, [this](bool on) { if (panel_) emit baseOnlyChanged(*panel_, on); });
    connect(mirrored, &QToolButton::clicked, this, [this] { if (panel_) emit mirroredInstanceRequested(*panel_); });
    connect(bake, &QToolButton::clicked, this, [this] { if (panel_) emit bakeRequested(*panel_); });
    connect(name_, &QLineEdit::editingFinished, this, [this] {
        if (panel_ && name_->isModified()) emit renamed(*panel_, name_->text());
    });
    connect(copy, &QToolButton::clicked, this, [this] { if (panel_) emit copyRequested(*panel_); });
    connect(instance, &QToolButton::clicked, this, [this] { if (panel_) emit instanceRequested(*panel_); });
    connect(remove, &QToolButton::clicked, this, [this] { if (panel_) emit deleteRequested(*panel_); });

    layersSection_ = new Section("Layers", page_);
    auto* layersColumn = new QVBoxLayout(layersSection_->content());
    layersColumn->setContentsMargins(0, 0, 0, 0);
    layersColumn->setSpacing(0);
    layerRows_ = new QVBoxLayout();
    layerRows_->setContentsMargins(4, 4, 4, 4);
    layerRows_->setSpacing(1);
    layersColumn->addLayout(layerRows_);
    // Operations that have no tool, or that are easier to add from a list: a
    // shape for an empty panel, then changes to it.
    auto* add = new QToolButton(layersSection_->content());
    add->setObjectName("addOperation");
    add->setText("Add operation");
    add->setIcon(icon(Icon::Plus));
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add->setAutoRaise(true);
    add->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(add);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        for (const session::Offered& offer : offers_) {
            QAction* action = menu->addAction(QString(offer.name) + "   —   " + offer.hint);
            const session::Offer which = offer.offer;
            connect(action, &QAction::triggered, this, [this, which] { addOperation(which); });
        }
    });
    add->setMenu(menu);
    layersColumn->addWidget(add, 0, Qt::AlignLeft);
    column->addWidget(layersSection_);

    // Where it is in space: lengths in cm, turns in degrees.
    spaceSection_ = new Section("In space", page_);
    auto* space = new QGridLayout(spaceSection_->content());
    space->setContentsMargins(8, 6, 8, 6);
    space->setSpacing(4);
    const char* spaceLabels[] = {"x", "y", "z", "Turn", "Tilt", "Roll", "Wrap"};
    const QColor spaceColours[] = {theme::axisX, theme::axisY, theme::axisZ, theme::quiet, theme::quiet, theme::quiet, theme::quiet};
    for (int i = 0; i < 7; ++i) {
        auto* label = new QLabel(spaceLabels[i], spaceSection_->content());
        label->setStyleSheet(QString("color: %1").arg(spaceColours[i].name()));
        auto* field = new ValueField(i < 3 ? Unit::Length : Unit::Angle, spaceSection_->content());
        // x y z on the first two rows; turn, tilt, roll on the next two; wrap below.
        const int labelRow = i < 3 ? 0 : i < 6 ? 2 : 4, place = i < 3 ? i : i < 6 ? i - 3 : 0;
        space->addWidget(label, labelRow, place);
        space->addWidget(field, labelRow + 1, place);
        space_.push_back(field);
        field->setDragStep(i < 3 ? 0.5 : 1.0);  // half a centimetre or a degree a pixel
        connect(field, &ValueField::typed, this, [this, i](QString text) {
            if (panel_) emit spaceTyped(*panel_, i, text);
        });
        connect(field, &ValueField::dragged, this, [this, i](double value, session::DragPhase phase) {
            if (panel_) emit spaceDragged(*panel_, i, value, phase);
        });
    }
    // Back to where the table has it, as when the panel was new.
    QToolButton* fromTable = smallButton(Icon::Reset, theme::quiet, "Place it where the table has it", spaceSection_);
    fromTable->setObjectName("placeFromTable");
    spaceSection_->addToHeader(fromTable);
    connect(fromTable, &QToolButton::clicked, this, [this] {
        if (panel_) emit placeFromTableRequested(*panel_);
    });
    column->addWidget(spaceSection_);

    tableSection_ = new Section("On the table", page_);
    auto* table = new QGridLayout(tableSection_->content());
    table->setContentsMargins(8, 6, 8, 6);
    table->setSpacing(4);
    const char* tableLabels[] = {"x", "y", "Angle"};
    for (int i = 0; i < 3; ++i) {
        table->addWidget(new QLabel(tableLabels[i], tableSection_->content()), 0, i);
        auto* field = new ValueField(i < 2 ? Unit::Length : Unit::Angle, tableSection_->content());
        table->addWidget(field, 1, i);
        table_.push_back(field);
        field->setDragStep(i < 2 ? 0.5 : 1.0);
        connect(field, &ValueField::typed, this, [this, i](QString text) {
            if (panel_) emit tableTyped(*panel_, i, text);
        });
        connect(field, &ValueField::dragged, this, [this, i](double value, session::DragPhase phase) {
            if (panel_) emit tableDragged(*panel_, i, value, phase);
        });
    }
    tableSection_->setOpen(false);
    column->addWidget(tableSection_);

    seamsSection_ = new Section("Seams", page_);
    seamRows_ = new QVBoxLayout(seamsSection_->content());
    seamRows_->setContentsMargins(8, 6, 8, 6);
    seamRows_->setSpacing(6);
    column->addWidget(seamsSection_);
    column->addStretch(1);
    setWidget(page_);
}

void AttributeEditor::show(const Document& doc, const Result& shapes, std::optional<PanelId> panel, std::optional<OpId> viewed) {
    if (panel && !doc.contains(*panel)) panel.reset();
    if (panel != panel_) open_.reset();
    panel_ = panel;
    viewed_ = viewed;
    offers_ = panel ? session::offersFor(doc, *panel) : std::vector<session::Offered>{};
    page_->setVisible(panel.has_value());
    if (!panel) return;
    const clothcad::Panel& p = doc.get(*panel);  // QFrame has a Panel of its own
    if (!name_->hasFocus()) name_->setText(QString::fromStdString(p.name));
    baseOnly_->setChecked(p.construction);
    rebuildLayers(doc, shapes);

    const Arrangement& at = p.arrangement;
    const double space[] = {at.x, at.y, at.z, at.turn, at.tilt, at.roll, at.wrap};
    for (std::size_t i = 0; i < space_.size(); ++i) space_[i]->show(Expr(space[i]), doc, shapes);
    const double table[] = {p.x, p.y, p.angle};
    for (std::size_t i = 0; i < table_.size(); ++i) table_[i]->show(Expr(table[i]), doc, shapes);

    rebuildSeams(doc, shapes);
}

// The seams that touch the panel: what is sewn to what, both lengths, and
// which way round; each can be reversed or removed.
void AttributeEditor::rebuildSeams(const Document& doc, const Result& shapes) {
    std::vector<SeamId> mine;
    for (SeamId id : doc.seams())
        if (doc.get(id).a.panel == *panel_ || doc.get(id).b.panel == *panel_) mine.push_back(id);
    const auto sewnOf = [&](SeamId id) -> const SeamResult* {
        for (const SeamResult& r : shapes.seams)
            if (r.seam == id) return &r;
        return nullptr;
    };
    const auto sideName = [&](const SeamSide& side) {
        std::string name = nameOf(Reference(side.edge), doc);
        if (!(side.panel == *panel_)) name += " on " + (doc.contains(side.panel) ? doc.get(side.panel).name : std::string("a removed panel"));
        return QString::fromStdString(name);
    };
    // The words each row shows; the rows are made again only when they change.
    struct Shown {
        SeamId id;
        QString here, there, lengths;
        bool broken, reversed;
    };
    std::vector<Shown> rows;
    std::string signature = std::to_string(panel_->value);
    for (SeamId id : mine) {
        const Seam& seam = doc.get(id);
        const bool swap = !(seam.a.panel == *panel_);  // this panel's edge first
        const SeamResult* sewn = sewnOf(id);
        Shown row{id, sideName(swap ? seam.b : seam.a), "sewn to " + sideName(swap ? seam.a : seam.b), {}, sewn && !sewn->sewn(), seam.reversed};
        if (sewn && !sewn->sewn()) {
            row.lengths = QString::fromStdString("Broken: " + sewn->problem + ". It comes back if the edge does.");
        } else if (sewn) {
            const double here = swap ? sewn->lengthB : sewn->lengthA, there = swap ? sewn->lengthA : sewn->lengthB;
            row.lengths = std::abs(here - there) < 0.05
                              ? QString::fromStdString(formatValue(here, Unit::Length) + ", both sides")
                              : QString::fromStdString(formatValue(here, Unit::Length) + " to " + formatValue(there, Unit::Length) + ": the longer side is gathered");
        }
        signature += "," + std::to_string(id.value) + row.here.toStdString() + row.there.toStdString() + row.lengths.toStdString() + (row.reversed ? "r" : "");
        rows.push_back(row);
    }
    seamsSection_->setVisible(!rows.empty());
    if (signature == shownSeams_) return;
    shownSeams_ = signature;
    seams_ = mine;
    while (QLayoutItem* item = seamRows_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const Shown& row : rows) {
        auto* box = new QWidget(seamsSection_->content());
        box->setObjectName(QString("seam%1").arg(row.id.value));
        auto* column = new QVBoxLayout(box);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(1);
        auto* here = new QLabel(row.here, box);
        here->setWordWrap(true);
        here->setStyleSheet(QString("color: %1").arg(theme::seam.name()));
        auto* there = new QLabel(row.there, box);
        there->setWordWrap(true);
        auto* lengths = new QLabel(row.lengths, box);
        lengths->setWordWrap(true);
        lengths->setStyleSheet(QString("color: %1").arg((row.broken ? theme::error : theme::quiet).name()));
        auto* buttons = new QHBoxLayout();
        auto* reversed = new QCheckBox("Reversed", box);
        reversed->setObjectName("reversed");
        reversed->setChecked(row.reversed);
        reversed->setToolTip("Sews start to start instead of start to end, which twists the pieces.\nThe dotted links on the table show which ends meet.");
        const SeamId id = row.id;
        connect(reversed, &QCheckBox::clicked, this, [this, id](bool on) { reverseSeam(id, on); });
        auto* remove = new QPushButton("Remove", box);
        remove->setObjectName("removeSeam");
        connect(remove, &QPushButton::clicked, this, [this, id] { removeSeam(id); });
        buttons->addWidget(reversed, 1);
        buttons->addWidget(remove);
        column->addWidget(here);
        column->addWidget(there);
        column->addWidget(lengths);
        column->addLayout(buttons);
        seamRows_->addWidget(box);
    }
}

void AttributeEditor::reverseSeam(SeamId seam, bool reversed) { emit seamReversed(seam, reversed); }

void AttributeEditor::removeSeam(SeamId seam) { emit seamRemoved(seam); }

void AttributeEditor::rebuildLayers(const Document& doc, const Result& shapes) {
    // Newest on top; a mirror applied last is on top of everything.
    std::vector<OpId> order = doc.operations(*panel_);
    std::reverse(order.begin(), order.end());
    std::stable_partition(order.begin(), order.end(), [&](OpId op) { return appliedLast(doc.get(op)); });
    const std::vector<OpId> made = doc.operations(*panel_);  // oldest first: the order they are applied in
    // What each layer's problem is now, if it has one.
    std::vector<std::pair<OpId, QString>> problems;
    for (const Error& error : shapes.errors)
        if (const OpId* where = std::get_if<OpId>(&error.where); where && doc.contains(*where) && doc.panelOf(*where) == *panel_)
            problems.push_back({*where, QString::fromStdString(error.message)});
    const auto problemOf = [&](OpId op) {
        for (const auto& [which, words] : problems)
            if (which == op) return words;
        return QString();
    };
    std::string signature = std::to_string(panel_->value) + ":" + (open_ ? std::to_string(open_->value) : "-") + "@" + (viewed_ ? std::to_string(viewed_->value) : "-");
    for (OpId op : order) signature += "," + std::to_string(op.value) + (doc.enabled(op) ? "+" : "-") + problemOf(op).toStdString() + choicesOf(doc.get(op), doc);
    if (signature != shownLayers_) {
        shownLayers_ = signature;
        layers_ = order;
        problems_ = problems;
        while (QLayoutItem* item = layerRows_->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        openFields_.clear();
        const OpId base = made.empty() ? OpId{} : made.front();
        const auto indexOf = [&](OpId op) { return static_cast<int>(std::find(made.begin(), made.end(), op) - made.begin()); };
        const int viewedIndex = viewed_ ? indexOf(*viewed_) : static_cast<int>(made.size());
        if (viewed_) {
            // Looking back: say so, and how to come back.
            auto* back = new QWidget(layersSection_->content());
            auto* backRow = new QHBoxLayout(back);
            backRow->setContentsMargins(6, 2, 2, 4);
            auto* words = new QLabel("Showing an earlier step", back);
            words->setStyleSheet(QString("color: %1").arg(theme::formula.name()));
            auto* all = new QPushButton("Show all steps", back);
            all->setObjectName("showAllSteps");
            connect(all, &QPushButton::clicked, this, [this] { emit stepViewed(std::nullopt); });
            backRow->addWidget(words, 1);
            backRow->addWidget(all);
            layerRows_->addWidget(back);
        }
        for (OpId op : order) {
            const OpData& operation = doc.get(op);
            const bool on = doc.enabled(op);
            const bool isOpen = open_ && *open_ == op;
            const int index = indexOf(op);
            const bool afterViewed = index > viewedIndex;  // left out of what the table shows now
            const QString problem = problemOf(op);
            auto* box = new QWidget(layersSection_->content());
            box->setObjectName(QString("layer%1").arg(op.value));
            if (isOpen) box->setStyleSheet("#" + box->objectName() + " { background: #2e2a20; border: 1px solid #6b5426; border-radius: 5px; }");
            auto* boxColumn = new QVBoxLayout(box);
            boxColumn->setContentsMargins(2, 0, 2, isOpen ? 6 : 0);
            boxColumn->setSpacing(2);
            auto* row = new QHBoxLayout();
            row->setSpacing(4);
            QToolButton* eye = op == base ? smallButton(Icon::Lock, theme::quiet, "The base shape", box)
                                          : smallButton(on ? Icon::Eye : Icon::EyeShut, on ? theme::text : theme::quiet, on ? "Switch off" : "Switch on", box);
            eye->setObjectName("eye");
            eye->setEnabled(op != base);
            connect(eye, &QToolButton::clicked, this, [this, op, on] { emit layerSwitched(op, !on); });
            QColor colour = !problem.isEmpty() ? theme::error : appliedLast(operation) ? theme::symmetry : isOpen ? theme::selected : on && !afterViewed ? theme::text : theme::quiet;
            auto* name = new QPushButton(icon(iconOf(operation), colour), QString(label(operation).data()), box);
            name->setObjectName("layerName");
            name->setFlat(true);
            name->setStyleSheet(QString("text-align: left; color: %1; %2").arg(colour.name(), on ? "" : "text-decoration: line-through;"));
            connect(name, &QPushButton::clicked, this, [this, op] { openLayer(op); });
            auto* summary = new QLabel(summaryOf(operation, doc, shapes), box);
            const bool driven = !fields(operation).empty() && !asNumber(fields(operation)[0].value);
            summary->setStyleSheet(QString("color: %1").arg((driven ? theme::formula : theme::quiet).name()));
            row->addWidget(eye);
            row->addWidget(name, 1);
            row->addWidget(summary);
            // Look back to this step, move it, remove it. The base shape stays first and stays.
            const bool viewedHere = viewed_ && *viewed_ == op;
            QToolButton* clock = smallButton(Icon::Clock, viewedHere ? theme::selected : theme::quiet,
                                             viewedHere ? "Show all steps again" : "Show the panel as it was at this step", box);
            clock->setObjectName("clock");
            connect(clock, &QToolButton::clicked, this, [this, op] { viewStep(op); });
            row->addWidget(clock);
            const bool movable = op != base && !appliedLast(operation);
            QToolButton* up = smallButton(Icon::Up, theme::quiet, "Move up: apply it later", box);
            up->setObjectName("up");
            up->setEnabled(movable && index + 1 < static_cast<int>(made.size()));
            connect(up, &QToolButton::clicked, this, [this, op] { moveLayer(op, true); });
            QToolButton* down = smallButton(Icon::Down, theme::quiet, "Move down: apply it earlier", box);
            down->setObjectName("down");
            down->setEnabled(movable && index > 1);
            connect(down, &QToolButton::clicked, this, [this, op] { moveLayer(op, false); });
            QToolButton* bin = smallButton(Icon::Delete, theme::quiet, "Remove this layer", box);
            bin->setObjectName("remove");
            bin->setEnabled(op != base);
            connect(bin, &QToolButton::clicked, this, [this, op] { removeLayer(op); });
            row->addWidget(up);
            row->addWidget(down);
            row->addWidget(bin);
            boxColumn->addLayout(row);
            if (!problem.isEmpty()) {
                auto* why = new QLabel(problem, box);
                why->setObjectName("problem");
                why->setWordWrap(true);
                why->setContentsMargins(26, 0, 4, 2);
                why->setStyleSheet(QString("color: %1").arg(theme::error.name()));
                boxColumn->addWidget(why);
            }
            if (isOpen) {
                auto* grid = new QGridLayout();
                grid->setContentsMargins(26, 0, 4, 0);
                grid->setSpacing(4);
                int line = 0;
                for (const ReferenceField& reference : references(operation)) {
                    grid->addWidget(new QLabel(QString(reference.label.data()), box), line, 0);
                    const QString used = QString::fromStdString(nameOf(reference.value, doc));
                    if (std::holds_alternative<PanelId>(reference.value)) {
                        // Any other panel can be chosen; one that would follow itself is refused when chosen.
                        auto* list = new QComboBox(box);
                        list->setObjectName("panelReference");
                        const PanelId own = *panel_;
                        int current = -1;
                        for (PanelId other : doc.panels()) {
                            if (other == own) continue;
                            if (Reference(other) == reference.value) current = list->count();
                            list->addItem(QString::fromStdString(doc.get(other).name), QVariant::fromValue(other.value));
                        }
                        if (current < 0) list->insertItem(0, used), current = 0;  // nothing chosen, or a panel removed
                        list->setCurrentIndex(current);
                        const ReferenceId id = reference.id;
                        connect(list, &QComboBox::activated, this, [this, op, id, list](int chosen) {
                            const QVariant value = list->itemData(chosen);
                            if (value.isValid()) emit panelReferenceChosen(op, id, PanelId{value.toUInt()});
                        });
                        grid->addWidget(list, line++, 1);
                        continue;
                    }
                    // An edge, corner or line: clicking it chooses another on the table.
                    auto* what = new QPushButton(used, box);
                    what->setFlat(true);
                    what->setCursor(Qt::PointingHandCursor);
                    what->setToolTip("Click, then click another on the table");
                    what->setStyleSheet(QString("text-align: left; color: %1; text-decoration: underline;").arg(theme::seam.name()));
                    const ReferenceId id = reference.id;
                    connect(what, &QPushButton::clicked, this, [this, op, id] { emit referenceClicked(op, id); });
                    grid->addWidget(what, line++, 1);
                }
                // Settings that are one of a few choices.
                if (const Cut* cut = std::get_if<Cut>(&operation)) {
                    grid->addWidget(new QLabel("Keep", box), line, 0);
                    auto* keep = new QComboBox(box);
                    keep->setObjectName("keep");
                    keep->addItems({"left of the line", "right of the line"});
                    keep->setCurrentIndex(cut->keep == Cut::Side::Left ? 0 : 1);
                    connect(keep, &QComboBox::activated, this, [this, op](int chosen) { emit cutSideChosen(op, chosen == 0 ? Cut::Side::Left : Cut::Side::Right); });
                    grid->addWidget(keep, line++, 1);
                }
                if (const Mirror* mirror = std::get_if<Mirror>(&operation)) {
                    auto* last = new QCheckBox("Applied last", box);
                    last->setObjectName("appliedLast");
                    last->setChecked(mirror->appliedLast);
                    last->setToolTip("Ticked: the mirror waits for every other step, so later edits stay symmetric.\n"
                                     "Unticked: it runs in its place, and later steps work on the whole panel.");
                    connect(last, &QCheckBox::clicked, this, [this, op](bool ticked) { emit mirrorLastChanged(op, ticked); });
                    grid->addWidget(last, line++, 0, 1, 2);
                }
                if (const Instance* instance = std::get_if<Instance>(&operation)) {
                    auto* mirrored = new QCheckBox("Mirrored", box);
                    mirrored->setObjectName("mirrored");
                    mirrored->setChecked(instance->mirrored);
                    mirrored->setToolTip("Follows the source flipped left to right, as the other sleeve does.");
                    connect(mirrored, &QCheckBox::clicked, this, [this, op](bool ticked) { emit instanceMirroredChanged(op, ticked); });
                    grid->addWidget(mirrored, line++, 0, 1, 2);
                }
                for (const Field& f : fields(operation)) {
                    grid->addWidget(new QLabel(QString(f.label.data()), box), line, 0);
                    auto* value = new ValueField(f.unit, box);
                    value->show(f.value, doc, shapes);
                    const FieldId id = f.id;
                    connect(value, &ValueField::typed, this, [this, op, id](QString text) { emit fieldTyped(op, id, text); });
                    connect(value, &ValueField::dragged, this,
                            [this, op, id](double number, session::DragPhase phase) { emit fieldDragged(op, id, number, phase); });
                    grid->addWidget(value, line++, 1);
                    openFields_.push_back({id, value});
                }
                boxColumn->addLayout(grid);
            }
            layerRows_->addWidget(box);
        }
    } else {
        // Same rows: only the values change, and the open fields keep what is being typed.
        if (open_)
            for (const auto& [id, value] : openFields_) value->show(fields(doc.get(*open_))[id.index].value, doc, shapes);
    }
}

void AttributeEditor::openLayer(OpId layer) {
    open_ = open_ && *open_ == layer ? std::optional<OpId>() : std::optional<OpId>(layer);  // a second click closes it
    shownLayers_.clear();
    emit layerOpened();
}

void AttributeEditor::switchLayer(OpId layer, bool on) { emit layerSwitched(layer, on); }

// The layers are shown newest on top: up is later among the operations.
void AttributeEditor::moveLayer(OpId layer, bool up) { emit layerMoved(layer, up); }

void AttributeEditor::removeLayer(OpId layer) { emit layerRemoved(layer); }

std::vector<session::Offer> AttributeEditor::offered() const {
    std::vector<session::Offer> offers;
    for (const session::Offered& offer : offers_) offers.push_back(offer.offer);
    return offers;
}

void AttributeEditor::addOperation(session::Offer offer) {
    if (panel_) emit operationOffered(*panel_, offer);
}

void AttributeEditor::viewStep(OpId layer) { emit stepViewed(viewed_ && *viewed_ == layer ? std::nullopt : std::optional<OpId>(layer)); }

QString AttributeEditor::problemShown(OpId layer) const {
    for (const auto& [which, words] : problems_)
        if (which == layer) return words;
    return {};
}

ValueField* AttributeEditor::field(FieldId id) const {
    for (const auto& [which, value] : openFields_)
        if (which == id) return value;
    return nullptr;
}

ValueField* AttributeEditor::spaceField(int which) const {
    return which >= 0 && which < static_cast<int>(space_.size()) ? space_[static_cast<std::size_t>(which)] : nullptr;
}

ValueField* AttributeEditor::tableField(int which) const {
    return which >= 0 && which < static_cast<int>(table_.size()) ? table_[static_cast<std::size_t>(which)] : nullptr;
}

} // namespace clothcad::ui
