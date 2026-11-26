#include "Shelf.h"

#include <QAction>
#include <QActionGroup>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolBar>
#include <QVBoxLayout>

#include "Icons.h"

namespace clothcad::ui {

namespace {

struct ToolInfo {
    Tool tool;
    Icon icon;
    QColor colour;
    const char* name;
    const char* key;
    const char* hint;
};

const ToolInfo toolInfo[] = {
    {Tool::Select, Icon::Select, theme::text, "Select", "Q", "Shift holds a drag to one axis, Ctrl snaps to the grid. Shift-click or a box selects several panels; a box on one panel selects its corners. Delete removes; Ctrl+C, V and D copy."},
    {Tool::Move, Icon::Move, theme::text, "Move", "W", "Drag an arrow to move along it, a square to move in its plane. Ctrl: whole centimetres."},
    {Tool::Rotate, Icon::Rotate, theme::text, "Rotate", "E", "Drag a ring to turn about its axis. Ctrl: steps of 15°."},
    {Tool::Scale, Icon::Scale, theme::text, "Scale", "R", "Drag a handle to stretch along it, the middle to scale evenly. Ctrl: steps of 10 %."},
    {Tool::Rectangle, Icon::Rectangle, theme::text, "Rectangle", "", "Drag on the table."},
    {Tool::Circle, Icon::Circle, theme::text, "Circle", "C", "Drag from the centre out."},
    {Tool::Ring, Icon::Ring, theme::text, "Ring", "G", "Click on the table."},
    {Tool::Point, Icon::Point, theme::text, "Point", "P", "Click an edge."},
    {Tool::Curve, Icon::Curve, theme::text, "Curve", "V", "Click an edge, then drag its handles."},
    {Tool::Line, Icon::Line, theme::text, "Line", "L", "Click two points on one outline."},
    {Tool::Length, Icon::Lengths, theme::text, "Length", "", "Click an edge, then type its length: a waist and an ankle can differ."},
    {Tool::Mirror, Icon::Mirror, theme::symmetry, "Mirror", "M", "Click a straight edge."},
    {Tool::Cut, Icon::Cut, theme::text, "Cut", "X", "Click the side to cut away."},
    {Tool::Split, Icon::Split, theme::text, "Split", "", "Click an internal line."},
    {Tool::Sew, Icon::Sew, theme::seam, "Sew", "S", "Click two edges."},
    {Tool::Pull, Icon::Pull, theme::text, "Pull", "H", "Drag the cloth in 3D, playing or paused."},
};

const ToolInfo& infoOf(Tool tool) {
    for (const ToolInfo& info : toolInfo)
        if (info.tool == tool) return info;
    return toolInfo[0];
}

} // namespace

const char* nameOf(Tool tool) { return infoOf(tool).name; }

const char* hintFor(Tool tool) { return infoOf(tool).hint; }

Shelf::Shelf(QWidget* parent) : QWidget(parent) {
    tabs_ = new QTabBar(this);
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    rows_ = new QStackedWidget(this);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(tabs_);
    column->addWidget(rows_);

    toolGroup_ = new QActionGroup(this);
    toolGroup_->setExclusive(true);
    for (const ToolInfo& info : toolInfo) {
        QAction* action = new QAction(icon(info.icon, info.colour), info.name, this);
        action->setCheckable(true);
        action->setToolTip(QString(info.name) + (info.key[0] ? QString("  ·  ") + info.key : QString()));
        if (info.key[0]) action->setShortcut(QKeySequence(info.key));
        action->setActionGroup(toolGroup_);
        const Tool tool = info.tool;
        connect(action, &QAction::triggered, this, [this, tool] { emit toolChosen(tool); });
        tools_.push_back({tool, action});
    }
    action(Tool::Select)->setChecked(true);

    undo_ = new QAction(icon(Icon::Undo), "Undo", this);
    undo_->setShortcut(QKeySequence::Undo);
    redo_ = new QAction(icon(Icon::Redo), "Redo", this);
    redo_->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Y")});
    simulate_ = new QAction(icon(Icon::Play, theme::formula), "Simulate", this);
    simulate_->setCheckable(true);
    simulate_->setShortcut(QKeySequence("Space"));
    simulate_->setToolTip("Simulate  ·  Space");
    connect(simulate_, &QAction::toggled, this, [this](bool on) {
        simulate_->setIcon(on ? icon(Icon::Pause) : icon(Icon::Play, theme::formula));
        simulate_->setText(on ? "Pause" : "Simulate");
        simulate_->setToolTip(on ? "Pause  ·  Space" : "Simulate  ·  Space");
    });
    reset_ = new QAction(icon(Icon::Reset), "Reset the cloth", this);
    reset_->setToolTip("Reset the cloth  ·  drop it again from where the panels are placed");

    instance_ = new QAction(icon(Icon::Instance, theme::instance), "Instance", this);
    instance_->setToolTip("Instance  ·  a panel that follows the selected one entirely, sewn as it is.\nSeveral selected panels get one each.");
    mirroredInstance_ = new QAction(icon(Icon::Mirror, theme::instance), "Mirrored instance", this);
    mirroredInstance_->setToolTip("Mirrored instance  ·  follows the selected panel, flipped left to right, sewn as it is.\nSeveral selected panels get one each.");
    bake_ = new QAction(icon(Icon::Lock, theme::formula), "Bake", this);
    bake_->setToolTip("Bake  ·  replace every formula of the selected panel with the number it comes to now,\nso that it follows no parameter. Useful on a copy.");
    placeFromTable_ = new QAction(icon(Icon::Reset, theme::quiet), "Place in space from the table", this);
    placeFromTable_->setToolTip("Place in space from the table  ·  put the selected panel in space where the table has it, as when it was new");
    sewAsSource_ = new QAction(icon(Icon::Instance, theme::seam), "Sew as source", this);
    sewAsSource_->setToolTip("Sew as source  ·  sew the selected instances as the panels they follow are sewn,\nfor seams made after the instances were");
    for (QAction* panelAction : {instance_, mirroredInstance_, bake_, placeFromTable_, sewAsSource_}) panelAction->setEnabled(false);
    connect(sewAsSource_, &QAction::triggered, this, &Shelf::sewAsSourceRequested);
    connect(placeFromTable_, &QAction::triggered, this, &Shelf::placeFromTableRequested);
    connect(instance_, &QAction::triggered, this, &Shelf::instanceRequested);
    connect(mirroredInstance_, &QAction::triggered, this, &Shelf::mirroredInstanceRequested);
    connect(bake_, &QAction::triggered, this, &Shelf::bakeRequested);

    layoutGroup_ = new QActionGroup(this);
    for (const auto& [which, picture, name] : {std::tuple{ViewLayout::SpaceOnly, Icon::SpaceOnly, "3D view only"},
                                                std::tuple{ViewLayout::TableOnly, Icon::TableOnly, "Table only"},
                                                std::tuple{ViewLayout::SideBySide, Icon::SideBySide, "Side by side"},
                                                std::tuple{ViewLayout::Stacked, Icon::Stacked, "Stacked"}}) {
        QAction* action = new QAction(icon(picture, theme::quiet), name, this);
        action->setCheckable(true);
        action->setActionGroup(layoutGroup_);
        const ViewLayout chosen = which;
        connect(action, &QAction::triggered, this, [this, chosen] { emit layoutChosen(chosen); });
        layouts_.push_back({which, action});
    }
    layout(ViewLayout::SideBySide)->setChecked(true);

    mesh_ = new QAction(icon(Icon::Mesh), "Cloth mesh", this);
    mesh_->setCheckable(true);
    mesh_->setToolTip("Cloth mesh  ·  the triangles the cloth is made of");

    const auto tools = [&](QToolBar* to, std::initializer_list<Tool> group) {
        for (Tool tool : group) to->addAction(action(tool));
        to->addSeparator();
    };
    // Each action is shown once. What every stage needs is in the two bars
    // around the shelf; the tabs hold what is made in that stage.
    QToolBar* draft = row("Draft");
    tools(draft, {Tool::Rectangle, Tool::Circle, Tool::Ring});
    tools(draft, {Tool::Point, Tool::Curve, Tool::Line, Tool::Length, Tool::Mirror});
    tools(draft, {Tool::Cut, Tool::Split});
    draft->addAction(instance_);
    draft->addAction(mirroredInstance_);
    draft->addAction(bake_);
    draft->addAction(placeFromTable_);
    QToolBar* sew = row("Sew");
    sew->addAction(action(Tool::Sew));
    sew->addAction(sewAsSource_);
    QToolBar* simulate = row("Simulate");
    simulate->addAction(action(Tool::Pull));
    connect(tabs_, &QTabBar::currentChanged, rows_, &QStackedWidget::setCurrentIndex);

    // Under the menu, whichever tab is shown. More can be added at the end:
    // the room that is left is the bar's own.
    actionRow_ = bar(22);
    actionRow_->addAction(undo_);
    actionRow_->addAction(redo_);
    actionRow_->addSeparator();
    actionRow_->addAction(simulate_);
    actionRow_->addAction(reset_);
    actionRow_->addSeparator();
    actionRow_->addAction(mesh_);

    // At the window's left edge: the tools that work on what is there.
    toolbox_ = bar(28);
    toolbox_->setOrientation(Qt::Vertical);
    for (Tool tool : {Tool::Select, Tool::Move, Tool::Rotate, Tool::Scale}) toolbox_->addAction(action(tool));
    // The views' layouts sit at the bottom of the bar, as in Maya.
    auto* room = new QWidget(toolbox_);
    room->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    toolbox_->addWidget(room);
    for (const auto& [which, layoutAction] : layouts_) toolbox_->addAction(layoutAction);

    // Keys work from every tab, as the actions are on the shelf, which is always shown.
    for (const auto& [which, toolAction] : tools_) addAction(toolAction);
    for (QAction* keyed : {undo_, redo_, simulate_, reset_, mesh_}) addAction(keyed);
}

QToolBar* Shelf::bar(int iconSize) {
    auto* made = new QToolBar(this);
    made->setObjectName("shelfRow");
    made->setIconSize(QSize(iconSize, iconSize));
    made->setToolButtonStyle(Qt::ToolButtonIconOnly);
    made->setMovable(false);
    return made;
}

QToolBar* Shelf::row(const QString& tab) {
    QToolBar* made = bar(28);
    tabs_->addTab(tab);
    rows_->addWidget(made);
    return made;
}

QAction* Shelf::action(Tool tool) const {
    for (const auto& [which, action] : tools_)
        if (which == tool) return action;
    return nullptr;
}

void Shelf::setTool(Tool tool) { action(tool)->setChecked(true); }

Tool Shelf::tool() const {
    for (const auto& [which, action] : tools_)
        if (action->isChecked()) return which;
    return Tool::Select;
}

QAction* Shelf::layout(ViewLayout which) const {
    for (const auto& [kind, action] : layouts_)
        if (kind == which) return action;
    return nullptr;
}

int Shelf::tabCount() const { return tabs_->count(); }

} // namespace clothcad::ui
