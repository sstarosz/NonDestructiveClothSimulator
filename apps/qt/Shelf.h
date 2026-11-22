#pragma once
// The shelf, as in Maya: tabs of tool rows across the top of the window.
// A tool is chosen in one place, whichever tab shows it. It only reports:
// the main window decides what a tool or a button does.
//
//   connect(shelf, &Shelf::toolChosen, [&](Tool tool) { ... });
//   shelf->action(Tool::Point)->trigger();

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

// How the two views share the middle of the window.
enum class ViewLayout { One, SideBySide, Stacked };

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

    // What can be done to the selected panel; the window enables them while one is selected.
    QAction* instance() const { return instance_; }
    QAction* mirroredInstance() const { return mirroredInstance_; }
    QAction* bake() const { return bake_; }

signals:
    void instanceRequested();
    void mirroredInstanceRequested();
    // Every formula of the panel replaced by the number it comes to now.
    void bakeRequested();
    void toolChosen(clothcad::ui::Tool tool);
    void layoutChosen(clothcad::ui::ViewLayout layout);

private:
    QToolBar* row(const QString& tab);

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
};

} // namespace clothcad::ui
