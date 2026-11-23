#pragma once
// The window's icons, drawn in code on a 24-unit grid (no image files), in
// the colour asked for. The shapes are the mockup's.
//
//   QAction* point = shelf->addAction(icon(Icon::Point), "Point");

#include <QColor>
#include <QIcon>

#include "Theme.h"

namespace clothcad::ui {

enum class Icon {
    Select, Move, Rotate, Scale, Rectangle, Circle, Ring, Point, Curve, Line, Mirror, Cut, Split, Sew, Pull,
    Undo, Redo, Play, Pause, Reset, Body, Avatar, Mesh, Grid, Lengths, Fit,
    OneView, SideBySide, Stacked, Eye, EyeShut, Lock, Plus, Copy, Instance, Delete, Folder, Twist,
};

QIcon icon(Icon which, QColor colour = theme::text);

} // namespace clothcad::ui
