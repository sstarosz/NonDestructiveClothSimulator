#pragma once
// The shelf, as in Maya, and the two bars that go with it. It owns every
// action, and shows each one in one place only:
//   - the shelf: tabs of tools that make something (Draft, Sew, Simulate);
//   - the action row, under the menu: undo, redo, play, pause, reset, and
//     the room for more of what is wanted whichever tab is shown;
//   - the toolbox, at the window's left edge: Select, Move, Rotate, Scale,
//     and how the two views share the middle.
// The shelf only reports: the main window decides what a tool or a button does.
// The window places the two bars with QMainWindow::addToolBar.
//
//   connect(shelf, &Shelf::toolChosen, [&](Tool tool) { ... });
//   shelf->action(Tool::Point)->trigger();
//   window.addToolBar(Qt::LeftToolBarArea, shelf->toolbox());

#include <QWidget>
#include <optional>

#include "clothcad/session/Tools.h"

class QAction;
class QActionGroup;
class QStackedWidget;
class QTabBar;
class QToolBar;

namespace clothcad::ui {

// The tools are the session's (session/Tools.h): the shelf only shows them.
using session::Tool;

// How the two views share the middle of the window: one alone, or both,
// side by side or stacked.
enum class ViewLayout { SpaceOnly, TableOnly, SideBySide, Stacked };

// Its name and a few words on what to do with it, for the tooltip and the help line.
const char* nameOf(Tool tool);
const char* hintFor(Tool tool);

class Shelf : public QWidget {
    Q_OBJECT
public:
    explicit Shelf(QWidget* parent = nullptr);

    QAction* action(Tool tool) const;
    // The tool shown as chosen.
    Tool tool() const;
    void setTool(Tool tool);

    QAction* undo() const { return undo_; }
    QAction* redo() const { return redo_; }
    QAction* simulate() const { return simulate_; }  // checkable: running or paused
    QAction* reset() const { return reset_; }
    QAction* layout(ViewLayout which) const;
    QAction* mesh() const { return mesh_; }  // checkable: the cloth mesh drawn over the panels
    int tabCount() const;
    QToolBar* actionRow() const { return actionRow_; }
    QToolBar* toolbox() const { return toolbox_; }

    // What can be done to the selected panel; the window enables them while one is selected.
    QAction* instance() const { return instance_; }
    QAction* mirroredInstance() const { return mirroredInstance_; }
    QAction* bake() const { return bake_; }
    QAction* placeFromTable() const { return placeFromTable_; }

signals:
    // The panel placed in space again where the table has it, as when it was new.
    void placeFromTableRequested();
    void instanceRequested();
    void mirroredInstanceRequested();
    // Every formula of the panel replaced by the number it comes to now.
    void bakeRequested();
    void toolChosen(clothcad::ui::Tool tool);
    void layoutChosen(clothcad::ui::ViewLayout layout);

private:
    QToolBar* row(const QString& tab);
    QToolBar* bar(int iconSize);

    QToolBar* actionRow_;
    QToolBar* toolbox_;
    QTabBar* tabs_;
    QStackedWidget* rows_;
    QActionGroup* toolGroup_;
    QActionGroup* layoutGroup_;
    std::vector<std::pair<Tool, QAction*>> tools_;
    std::vector<std::pair<ViewLayout, QAction*>> layouts_;
    QAction* undo_;
    QAction* redo_;
    QAction* simulate_;
    QAction* reset_;
    QAction* mesh_;
    QAction* instance_;
    QAction* mirroredInstance_;
    QAction* bake_;
    QAction* placeFromTable_;
};

} // namespace clothcad::ui
