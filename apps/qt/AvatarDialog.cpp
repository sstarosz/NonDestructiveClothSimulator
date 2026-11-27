#include "AvatarDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSignalBlocker>

#include "ValueField.h"
#include "clothcad/core/Text.h"
#include "clothcad/sim/AvatarImport.h"

namespace clothcad::ui {

namespace {

// 100 km: no body, and beyond it the numbers soon stop meaning anything.
constexpr double tooTall = 1e7;  // cm

// The axes in the order they are offered, with their names for people.
struct Offered {
    Axis axis;
    const char* name;
};
constexpr Offered axes[] = {{Axis::Y, "Y"}, {Axis::Z, "Z"}, {Axis::X, "X"}, {Axis::MinusY, "-Y"}, {Axis::MinusZ, "-Z"}, {Axis::MinusX, "-X"}};

Axis axisAt(const QComboBox* box) { return static_cast<Axis>(box->currentData().toInt()); }
void choose(QComboBox* box, Axis axis) { box->setCurrentIndex(box->findData(static_cast<int>(axis))); }

} // namespace

AvatarDialog::AvatarDialog(sim::Vec3 fileSize, const AvatarImport& guess, QWidget* parent)
    : QDialog(parent), fileSize_(fileSize), choice_(guess) {
    setWindowTitle("Avatar");
    auto* rows = new QFormLayout(this);

    unit_ = new QComboBox(this);
    unit_->setObjectName("avatarUnit");
    for (const sim::FileUnit& unit : sim::fileUnits) unit_->addItem(QString::fromUtf8(unit.name.data(), static_cast<qsizetype>(unit.name.size())), unit.cm);
    scale_ = new ValueField(Unit::Fraction, this);
    scale_->setObjectName("avatarScale");
    height_ = new ValueField(Unit::Length, this);
    height_->setObjectName("avatarHeight");
    up_ = new QComboBox(this);
    up_->setObjectName("avatarUp");
    for (const Offered& offered : axes) up_->addItem(offered.name, static_cast<int>(offered.axis));
    choose(up_, choice_.up);
    facing_ = new QComboBox(this);
    facing_->setObjectName("avatarFacing");
    listFacings();

    rows->addRow("File unit", unit_);
    rows->addRow("Scale", scale_);
    rows->addRow("Height", height_);
    rows->addRow("Up", up_);
    rows->addRow("Facing", facing_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    rows->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &AvatarDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &AvatarDialog::reject);
    // Enter is OK from the start, not the end of typing into a field.
    buttons->button(QDialogButtonBox::Ok)->setFocus();

    for (ValueField* field : {scale_, height_}) {
        connect(field, &ValueField::typed, this, [this, field](const QString& text) { take(field, text); });
        connect(field, &ValueField::dragged, this, [this, field](double value) { use(field, value); });
    }
    connect(unit_, &QComboBox::currentIndexChanged, this, [this] {
        choice_.unit = unit_->currentData().toDouble();
        chosen();
    });
    connect(up_, &QComboBox::currentIndexChanged, this, [this] {
        choice_.up = axisAt(up_);
        if (alongOneLine(choice_.up, choice_.facing)) choice_.facing = sim::usualFacing(choice_.up);
        listFacings();
        chosen();
    });
    connect(facing_, &QComboBox::currentIndexChanged, this, [this] {
        choice_.facing = axisAt(facing_);
        chosen();
    });
    showChoice();
}

double AvatarDialog::fileHeight() const { return sim::along(fileSize_, choice_.up); }

void AvatarDialog::take(ValueField* field, const QString& text) {
    const Document none;  // the numbers here are numbers: there are no parameters
    const std::variant<Expr, TextError> read = parseValue(text.toStdString(), field->unit(), none);
    if (const TextError* problem = std::get_if<TextError>(&read)) return field->refuse(QString::fromStdString(problem->message));
    use(field, valueOf(std::get<Expr>(read), Result{}).value_or(0.0));
}

void AvatarDialog::use(ValueField* field, double value) {
    const bool height = field == height_;
    if (height && !(fileHeight() > 0.0)) return field->refuse("The body is flat this way up. Choose another up axis.");
    // A height sets the scale: so many cm over what the file's unit alone would make it.
    const double rescale = height ? value / (fileHeight() * choice_.unit) : value;
    if (!(rescale > 0.0) || !(fileHeight() * choice_.unit * rescale <= tooTall))
        return field->refuse(rescale > 0.0 ? "That is too large for a body. Type less." : "Type a number above 0.");
    choice_.rescale = rescale;
    chosen();
}

void AvatarDialog::chosen() {
    showChoice();
    emit changed(choice_);
}

void AvatarDialog::showChoice() {
    const Document none;
    const Result nothing;
    for (ValueField* field : {scale_, height_}) field->setModified(false);  // what is chosen replaces what was typed
    scale_->show(Expr(choice_.rescale), none, nothing);
    height_->show(Expr(fileHeight() * choice_.cmPerUnit()), none, nothing);
    const QSignalBlocker quiet(unit_);
    unit_->setCurrentIndex(unit_->findData(choice_.unit));
}

// The four ways a body can face with the chosen axis up.
void AvatarDialog::listFacings() {
    const QSignalBlocker quiet(facing_);
    facing_->clear();
    for (const Offered& offered : axes)
        if (!alongOneLine(offered.axis, choice_.up)) facing_->addItem(offered.name, static_cast<int>(offered.axis));
    choose(facing_, choice_.facing);
}

void AvatarDialog::accept() {
    if (auto* typing = qobject_cast<ValueField*>(focusWidget())) typing->clearFocus();
    if (scale_->refused() || height_->refused()) return;
    QDialog::accept();
}

} // namespace clothcad::ui
