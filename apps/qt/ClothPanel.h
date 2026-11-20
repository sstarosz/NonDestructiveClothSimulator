#pragma once
// The Cloth tab: the particles, the body, and how the cloth is doing now.
// Only reports what is changed.

#include <QWidget>

#include "clothcad/session/Session.h"

class QCheckBox;
class QLabel;

namespace clothcad::ui {

class ValueField;

class ClothPanel : public QWidget {
    Q_OBJECT
public:
    explicit ClothPanel(QWidget* parent = nullptr);
    void show(const session::Session& session, bool ownThread, bool bodyShown);
    ValueField* distance() const { return distance_; }

signals:
    void distanceTyped(QString text);
    void ownThreadChanged(bool on);
    // Shown or hidden; a hidden body is also out of the simulation.
    void bodyShownChanged(bool shown);

private:
    ValueField* distance_;
    QLabel* count_;
    QCheckBox* ownThread_;
    QCheckBox* bodyShown_;
    QLabel* body_;
    QLabel* now_;
};

} // namespace clothcad::ui
