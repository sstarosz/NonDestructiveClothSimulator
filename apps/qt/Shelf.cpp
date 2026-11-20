#include "Shelf.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
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
    {Tool::Select, Icon::Select, theme::text, "Select", "Q", "Click a panel; drag it, or drag a corner."},
    {Tool::Rectangle, Icon::Rectangle, theme::text, "Rectangle", "R", "Drag on the table."},
    {Tool::Circle, Icon::Circle, theme::text, "Circle", "C", "Drag from the centre out."},
    {Tool::Ring, Icon::Ring, theme::text, "Ring", "G", "Click on the table."},
    {Tool::Point, Icon::Point, theme::text, "Point", "P", "Click an edge."},
    {Tool::Curve, Icon::Curve, theme::text, "Curve", "V", "Click an edge, then drag its handles."},
    {Tool::Line, Icon::Line, theme::text, "Line", "L", "Click two points on one outline."},
    {Tool::Mirror, Icon::Mirror, theme::symmetry, "Mirror", "M", "Click a straight edge."},
    {Tool::Cut, Icon::Cut, theme::text, "Cut", "X", "Click the side to cut away."},
    {Tool::Split, Icon::Split, theme::text, "Split", "", "Click an internal line."},
    {Tool::Sew, Icon::Sew, theme::seam, "Sew", "S", "Click two edges."},
    {Tool::Pull, Icon::Pull, theme::text, "Pull", "H", "Drag the cloth."},
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
    auto* line = new QHBoxLayout();
    line->setSpacing(0);
    line->addWidget(rows_, 1);
    column->addLayout(line);

    toolGroup_ = new QActionGroup(this);
    toolGroup_->setExclusive(true);
    for (const ToolInfo& info : toolInfo) {
        QAction* action = new QAction(icon(info.icon, info.colour), info.name, this);
        action->setCheckable(true);
        action->setToolTip(QString(info.name) + (info.key[0] ? QString("  ·  ") + info.key : QString()));
        if (info.key[0]) action->setShortcut(QKeySequence(info.key));
        action->setActionGroup(toolGroup_);
        const Tool tool = info.tool;
        connect(action, &QAction::triggered, this, [this, tool] { setTool(tool); emit toolChosen(tool); });
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
    connect(simulate_, &QAction::toggled, this, [this](bool on) {
        simulate_->setIcon(on ? icon(Icon::Pause) : icon(Icon::Play, theme::formula));
        simulate_->setText(on ? "Pause" : "Simulate");
    });
    reset_ = new QAction(icon(Icon::Reset), "Reset the cloth", this);

    layoutGroup_ = new QActionGroup(this);
    for (const auto& [which, picture, name] : {std::tuple{ViewLayout::One, Icon::OneView, "One view"},
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

    const auto tools = [&](QToolBar* bar, std::initializer_list<Tool> group) {
        for (Tool tool : group) bar->addAction(action(tool));
        bar->addSeparator();
    };
    QToolBar* draft = row("Draft");
    draft->addAction(undo_);
    draft->addAction(redo_);
    draft->addSeparator();
    tools(draft, {Tool::Select});
    tools(draft, {Tool::Rectangle, Tool::Circle, Tool::Ring});
    tools(draft, {Tool::Point, Tool::Curve, Tool::Line, Tool::Mirror});
    tools(draft, {Tool::Cut, Tool::Split});
    tools(draft, {Tool::Sew});
    QToolBar* sew = row("Sew");
    tools(sew, {Tool::Select});
    tools(sew, {Tool::Sew});
    QToolBar* simulate = row("Simulate");
    simulate->addAction(undo_);
    simulate->addAction(redo_);
    simulate->addSeparator();
    tools(simulate, {Tool::Select, Tool::Pull});
    simulate->addAction(simulate_);
    simulate->addAction(reset_);
    connect(tabs_, &QTabBar::currentChanged, rows_, &QStackedWidget::setCurrentIndex);

    // The layouts are the same on every tab: beside the rows, at the right.
    auto* corner = new QToolBar(this);
    corner->setObjectName("shelfRow");
    corner->setIconSize(QSize(20, 20));
    mesh_ = new QAction(icon(Icon::Mesh), "Cloth mesh", this);
    mesh_->setCheckable(true);
    mesh_->setToolTip("Cloth mesh  ·  the triangles the cloth is made of");
    corner->addAction(mesh_);
    corner->addSeparator();
    for (const auto& [which, action] : layouts_) corner->addAction(action);
    line->addWidget(corner);
}

QToolBar* Shelf::row(const QString& tab) {
    auto* bar = new QToolBar(this);
    bar->setObjectName("shelfRow");
    bar->setIconSize(QSize(28, 28));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tabs_->addTab(tab);
    rows_->addWidget(bar);
    return bar;
}

QAction* Shelf::action(Tool tool) const {
    for (const auto& [which, action] : tools_)
        if (which == tool) return action;
    return nullptr;
}

void Shelf::setTool(Tool tool) {
    tool_ = tool;
    action(tool)->setChecked(true);
}

QAction* Shelf::layout(ViewLayout which) const {
    for (const auto& [kind, action] : layouts_)
        if (kind == which) return action;
    return nullptr;
}

int Shelf::tabCount() const { return tabs_->count(); }

} // namespace clothcad::ui
