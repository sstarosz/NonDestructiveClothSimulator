#pragma once
// The window's look: a dark Fusion palette, and the colours that mean
// something. The meanings are the same everywhere in the window:
// teal = driven by a formula, violet = an instance, pink = symmetry,
// blue = a seam, amber = selected.
//
//   theme::apply(app);
//   painter.setPen(theme::seam);

#include <QColor>

class QApplication;

namespace clothcad::ui::theme {

inline const QColor formula{110, 219, 199};
inline const QColor instance{191, 158, 242};
inline const QColor symmetry{242, 128, 199};
inline const QColor seam{115, 173, 255};
inline const QColor selected{255, 191, 69};
inline const QColor error{255, 107, 97};
inline const QColor axisX{230, 99, 99};
inline const QColor axisY{120, 209, 120};
inline const QColor axisZ{110, 150, 240};

inline const QColor text{230, 231, 234};
inline const QColor quiet{154, 158, 167};  // labels and units: still 4.5:1 on the panels
inline const QColor panel{32, 34, 38};
inline const QColor header{43, 45, 50};
inline const QColor field{20, 21, 23};
inline const QColor table{23, 24, 27};

// A layer's card in the Attribute Editor: its outline (shut, open, and while
// the panel is shown as of that step) and the band its name sits on.
inline const QColor cardEdge{47, 49, 55};
inline const QColor cardEdgeOpen{59, 62, 69};
inline const QColor cardEdgeViewed{138, 143, 156};
inline const QColor cardBand{26, 27, 30};
inline const QColor cardBandOpen{35, 37, 41};

void apply(QApplication& app);

} // namespace clothcad::ui::theme
