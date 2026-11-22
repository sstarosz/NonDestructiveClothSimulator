#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace clothcad::ui {

namespace {

// Strokes and fills on the 24-unit grid. Each icon is a few of these.
struct Pen {
    QPainter& p;
    QColor colour;
    void line(QPointF a, QPointF b) { p.drawLine(a, b); }
    void lines(std::initializer_list<QPointF> points) {
        QPainterPath path;
        bool first = true;
        for (QPointF point : points) {
            if (first) path.moveTo(point);
            else path.lineTo(point);
            first = false;
        }
        p.drawPath(path);
    }
    void dashed(QPointF a, QPointF b) {
        QPen pen = p.pen();
        pen.setDashPattern({1.5, 1.5});
        p.save();
        p.setPen(pen);
        p.drawLine(a, b);
        p.restore();
    }
    void dot(QPointF at, double radius) {
        p.save();
        p.setBrush(colour);
        p.drawEllipse(at, radius, radius);
        p.restore();
    }
    void filled(std::initializer_list<QPointF> points) {
        QPolygonF shape(points);
        p.save();
        p.setBrush(colour);
        p.setPen(Qt::NoPen);
        p.drawPolygon(shape);
        p.restore();
    }
};

void draw(Icon which, Pen& d) {
    QPainter& p = d.p;
    switch (which) {
    case Icon::Select: d.lines({{5, 3}, {11, 20}, {13, 13}, {20, 11}, {5, 3}}); break;
    case Icon::Move:  // four arrows out from the middle
        d.line({12, 3}, {12, 21}); d.line({3, 12}, {21, 12});
        d.lines({{9, 6}, {12, 3}, {15, 6}}); d.lines({{9, 18}, {12, 21}, {15, 18}});
        d.lines({{6, 9}, {3, 12}, {6, 15}}); d.lines({{18, 9}, {21, 12}, {18, 15}});
        break;
    case Icon::Rotate: {  // a ring nearly closed, with an arrow head
        QPainterPath around; around.moveTo(19.5, 9); around.arcTo(QRectF(4, 4, 16, 16), 20, 290); p.drawPath(around);
        d.lines({{15.5, 4.5}, {19.5, 9}, {21, 3.5}});
        break;
    }
    case Icon::Scale:  // a small box, a big one, an arrow from the one to the other
        p.drawRect(QRectF(3, 13, 8, 8)); p.drawRect(QRectF(3, 3, 18, 18));
        d.line({10, 14}, {18, 6}); d.lines({{13, 6}, {18, 6}, {18, 11}});
        break;
    case Icon::Rectangle: p.drawRoundedRect(QRectF(4, 6, 16, 12), 1, 1); break;
    case Icon::Circle: p.drawEllipse(QPointF(12, 12), 8, 8); break;
    case Icon::Ring: {
        QPainterPath ring;
        ring.moveTo(3, 9);
        ring.cubicTo(8, 4, 16, 4, 21, 9);
        ring.lineTo(16.5, 13.5);
        ring.cubicTo(14, 11, 10, 11, 7.5, 13.5);
        ring.closeSubpath();
        p.drawPath(ring);
        break;
    }
    case Icon::Point: d.line({3, 16}, {21, 16}); d.dot({12, 16}, 3); d.line({12, 4}, {12, 10}); d.line({9, 7}, {15, 7}); break;
    case Icon::Curve: {
        QPainterPath curve;
        curve.moveTo(4, 18);
        curve.cubicTo(8, 5, 16, 5, 20, 18);
        p.drawPath(curve);
        break;
    }
    case Icon::Line: d.line({5, 19}, {19, 5}); p.drawEllipse(QPointF(5, 19), 2, 2); p.drawEllipse(QPointF(19, 5), 2, 2); break;
    case Icon::Mirror: d.dashed({12, 3}, {12, 21}); d.lines({{9, 6}, {4, 6}, {4, 18}, {9, 18}}); d.lines({{15, 6}, {20, 6}, {20, 18}, {15, 18}}); break;
    case Icon::Cut: p.drawEllipse(QPointF(6, 18), 3, 3); p.drawEllipse(QPointF(18, 18), 3, 3); d.line({8, 16}, {20, 4}); d.line({16, 16}, {4, 4}); break;
    case Icon::Split: p.drawRect(QRectF(3, 5, 7, 14)); p.drawRect(QRectF(14, 5, 7, 14)); break;
    case Icon::Sew: d.line({4, 7}, {20, 7}); d.line({4, 17}, {20, 17}); for (double x : {8.0, 12.0, 16.0}) d.dashed({x, 7}, {x, 17}); break;
    case Icon::Pull: {
        QPainterPath hand;
        hand.moveTo(8, 13); hand.lineTo(8, 5); hand.arcTo(QRectF(8, 3.5, 3, 3), 180, -180); hand.lineTo(11, 11);
        hand.moveTo(11, 10); hand.lineTo(11, 4); hand.arcTo(QRectF(11, 2.5, 3, 3), 180, -180); hand.lineTo(14, 10);
        hand.moveTo(14, 10); hand.lineTo(14, 5); hand.arcTo(QRectF(14, 3.5, 3, 3), 180, -180); hand.lineTo(17, 12);
        hand.moveTo(17, 9); hand.arcTo(QRectF(17, 7.5, 3, 3), 180, -180); hand.lineTo(20, 14);
        hand.cubicTo(20, 18, 17, 21, 13, 21); hand.lineTo(12, 21); hand.cubicTo(9.5, 21, 7.5, 19.5, 6.5, 18.3);
        hand.lineTo(4, 15.5); hand.cubicTo(3.5, 14.5, 5.5, 12.5, 6.4, 13.5); hand.lineTo(8, 15);
        p.drawPath(hand);
        break;
    }
    case Icon::Undo: d.lines({{9, 14}, {4, 9}, {9, 4}}); {
        QPainterPath back; back.moveTo(4, 9); back.lineTo(15, 9); back.arcTo(QRectF(10, 9, 10, 10), 90, -180); back.lineTo(12, 19); p.drawPath(back);
    } break;
    case Icon::Redo: d.lines({{15, 14}, {20, 9}, {15, 4}}); {
        QPainterPath on; on.moveTo(20, 9); on.lineTo(9, 9); on.arcTo(QRectF(4, 9, 10, 10), 90, 180); on.lineTo(12, 19); p.drawPath(on);
    } break;
    case Icon::Play: d.filled({{7, 4}, {7, 20}, {20, 12}}); break;
    case Icon::Pause: p.fillRect(QRectF(6, 4, 4.5, 16), d.colour); p.fillRect(QRectF(13.5, 4, 4.5, 16), d.colour); break;
    case Icon::Reset: {
        QPainterPath around; around.moveTo(7, 5.8); around.arcTo(QRectF(4, 4, 16, 16), 125, 305); p.drawPath(around);
        d.lines({{4, 4}, {4, 9}, {9, 9}});
        break;
    }
    case Icon::Body: p.drawEllipse(QPointF(12, 5), 3, 3); d.lines({{8, 10}, {16, 10}, {15, 21}, {9, 21}, {8, 10}}); break;
    case Icon::Avatar: p.drawEllipse(QPointF(10, 5), 3, 3); d.lines({{6, 10}, {14, 10}, {13, 21}, {7, 21}, {6, 10}}); d.line({19, 12}, {19, 20}); d.line({15, 16}, {23, 16}); break;
    case Icon::Mesh: p.drawRect(QRectF(3, 3, 18, 18)); d.line({3, 3}, {21, 21}); d.line({12, 3}, {12, 21}); d.line({3, 12}, {21, 12}); break;
    case Icon::Grid: for (double v : {9.0, 15.0}) { d.line({3, v}, {21, v}); d.line({v, 3}, {v, 21}); } break;
    case Icon::Lengths: d.line({3, 12}, {21, 12}); d.line({3, 8}, {3, 16}); d.line({21, 8}, {21, 16}); break;
    case Icon::Fit: d.lines({{4, 9}, {4, 4}, {9, 4}}); d.lines({{15, 4}, {20, 4}, {20, 9}}); d.lines({{4, 15}, {4, 20}, {9, 20}}); d.lines({{15, 20}, {20, 20}, {20, 15}}); break;
    case Icon::OneView: p.drawRoundedRect(QRectF(3, 4, 18, 16), 1, 1); break;
    case Icon::SideBySide: p.drawRoundedRect(QRectF(3, 4, 18, 16), 1, 1); d.line({12, 4}, {12, 20}); break;
    case Icon::Stacked: p.drawRoundedRect(QRectF(3, 4, 18, 16), 1, 1); d.line({3, 12}, {21, 12}); break;
    case Icon::Eye: {
        QPainterPath eye; eye.moveTo(2, 12); eye.cubicTo(6, 5, 18, 5, 22, 12); eye.cubicTo(18, 19, 6, 19, 2, 12); p.drawPath(eye);
        p.drawEllipse(QPointF(12, 12), 3, 3);
        break;
    }
    case Icon::EyeShut: {
        QPainterPath lid; lid.moveTo(2, 11); lid.cubicTo(6, 17, 18, 17, 22, 11); p.drawPath(lid);
        d.line({3, 3}, {21, 21});
        break;
    }
    case Icon::Lock: p.drawRoundedRect(QRectF(5, 11, 14, 10), 1, 1); {
        QPainterPath shackle; shackle.moveTo(8, 11); shackle.lineTo(8, 7); shackle.arcTo(QRectF(8, 3, 8, 8), 180, -180); shackle.lineTo(16, 11); p.drawPath(shackle);
    } break;
    case Icon::Plus: d.line({12, 5}, {12, 19}); d.line({5, 12}, {19, 12}); break;
    case Icon::Copy: p.drawRoundedRect(QRectF(8, 8, 12, 12), 1, 1); d.lines({{4, 16}, {4, 4}, {16, 4}}); break;
    case Icon::Instance: p.drawRect(QRectF(4, 4, 10, 10)); p.drawRect(QRectF(10, 10, 10, 10)); break;
    case Icon::Delete: d.line({4, 7}, {20, 7}); d.lines({{9, 7}, {9, 4}, {15, 4}, {15, 7}}); d.lines({{6, 7}, {7, 20}, {17, 20}, {18, 7}}); break;
    case Icon::Folder: d.lines({{3, 7}, {10, 7}, {12, 9}, {21, 9}, {21, 19}, {3, 19}, {3, 7}}); break;
    }
}

} // namespace

QIcon icon(Icon which, QColor colour) {
    QIcon made;
    // Drawn at the sizes the window asks for, so it is sharp on a 4K screen too.
    for (const int size : {16, 24, 32, 48, 64}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 24.0, size / 24.0);
        QPen pen(colour, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        Pen d{painter, colour};
        draw(which, d);
        made.addPixmap(pixmap);
    }
    return made;
}

} // namespace clothcad::ui
