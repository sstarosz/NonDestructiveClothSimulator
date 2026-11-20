#include "ClothPanel.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "Section.h"
#include "Theme.h"
#include "ValueField.h"
#include "clothcad/core/Text.h"

namespace clothcad::ui {

ClothPanel::ClothPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("cloth");
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto* particles = new Section("Particles", this);
    auto* form = new QFormLayout(particles->content());
    distance_ = new ValueField(Unit::Length, particles->content());
    connect(distance_, &ValueField::typed, this, &ClothPanel::distanceTyped);
    count_ = new QLabel(particles->content());
    ownThread_ = new QCheckBox("own", particles->content());
    connect(ownThread_, &QCheckBox::toggled, this, &ClothPanel::ownThreadChanged);
    form->addRow("Distance", distance_);
    form->addRow("Count", count_);
    form->addRow("Thread", ownThread_);
    column->addWidget(particles);

    auto* body = new Section("Body", this);
    auto* bodyForm = new QFormLayout(body->content());
    body_ = new QLabel(body->content());
    body_->setObjectName("bodyName");
    bodyShown_ = new QCheckBox("shown", body->content());
    bodyShown_->setObjectName("bodyShown");
    bodyShown_->setChecked(true);
    bodyShown_->setToolTip("Hidden, the body is also taken out of the simulation.");
    connect(bodyShown_, &QCheckBox::clicked, this, &ClothPanel::bodyShownChanged);
    bodyForm->addRow("Shape", body_);
    bodyForm->addRow("Body", bodyShown_);
    column->addWidget(body);

    auto* now = new Section("Now", this);
    auto* nowColumn = new QVBoxLayout(now->content());
    now_ = new QLabel(now->content());
    now_->setTextFormat(Qt::PlainText);
    nowColumn->addWidget(now_);
    column->addWidget(now);
    column->addStretch(1);
}

void ClothPanel::show(const session::Session& session, bool ownThread, bool bodyShown) {
    Document none;
    distance_->show(Expr(session.particleDistance()), none, Result{});
    std::size_t particles = 0, triangles = 0;
    for (const sim::PanelMesh& panel : session.mesh()->panels) {
        particles += panel.points.size();
        triangles += panel.triangles.size();
    }
    count_->setText(QString("%1 particles\n%2 triangles\n%3 stitches").arg(particles).arg(triangles).arg(session.mesh()->stitches.size()));
    const QSignalBlocker quiet(ownThread_);
    ownThread_->setChecked(ownThread);
    bodyShown_->setChecked(bodyShown);
    const QString bodyName = session.avatarPath().empty() ? "Mannequin" : QString::fromStdString(session.avatarPath()).section('/', -1).section('\\', -1);
    // Working out how to collide with a new avatar takes a moment; the cloth waits for it.
    body_->setText(session.bodyReady() ? bodyName : bodyName + "  ·  preparing…");
    if (session.clothShown())
        now_->setText(QString("Time  %1 s\nStep  %2 ms%3")
                          .arg(session.cloth().time, 0, 'f', 1)
                          .arg(session.cloth().stepMilliseconds, 0, 'f', 1)
                          .arg(session.cloth().sewing ? "\nSewing" : ""));
    else
        now_->setText("Time  0.0 s");
}

} // namespace clothcad::ui
