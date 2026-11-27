#pragma once
// The small dialog that is open while an avatar is chosen: the file's unit, how
// large to make the body (a scale, or the height it comes to), and the file's
// up axis and facing, filled in with a guess. The unit is how the file is read;
// a scale or a height typed makes the body larger or smaller and leaves the
// unit. It only reports; whoever opened it reads the file, shows the body and
// changes the document. It does not hold the window: that is for its opener, so
// that the 3D view can still be turned.
//
//   auto* dialog = new AvatarDialog(sim::sizeOf(file), sim::guessImport(file), window);
//   connect(dialog, &AvatarDialog::changed, [&](const AvatarImport& chosen) { show(sim::standing(file, chosen)); });
//   connect(dialog, &QDialog::accepted, [&] { wear(sim::standing(file, dialog->choice())); });
//   dialog->show();

#include <QDialog>

#include "twintailor/core/Document.h"
#include "twintailor/sim/Vec3.h"

class QComboBox;

namespace twintailor::ui {

class ValueField;

class AvatarDialog : public QDialog {
    Q_OBJECT
public:
    // `fileSize` is the size of the file's box in its own units (sim::sizeOf).
    AvatarDialog(sim::Vec3 fileSize, const AvatarImport& guess, QWidget* parent = nullptr);

    // The unit, scale, up and facing as chosen; where it stands is not chosen here.
    AvatarImport choice() const { return choice_; }

    // OK: takes what is being typed first, and stays open if that cannot be used.
    void accept() override;

signals:
    // Something else is chosen now.
    void changed(const twintailor::AvatarImport& choice);

private:
    double fileHeight() const;  // in the file's units, with the chosen axis up
    // What was typed into the scale's or the height's field, and a number of
    // that field (a fraction, or cm). One that cannot be used marks the field.
    void take(ValueField* field, const QString& text);
    void use(ValueField* field, double value);
    void chosen();      // shows the choice and says that it changed
    void showChoice();
    void listFacings();

    sim::Vec3 fileSize_;
    AvatarImport choice_;
    QComboBox* unit_;
    ValueField* scale_;
    ValueField* height_;
    QComboBox* up_;
    QComboBox* facing_;
};

} // namespace twintailor::ui
