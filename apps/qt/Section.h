#pragma once
// A part of a dock that folds away under its header, as Maya's frames do.
//
//   auto* layers = new Section("Layers");
//   layers->content()->setLayout(rows);

#include <QWidget>

class QPushButton;

namespace clothcad::ui {

class Section : public QWidget {
    Q_OBJECT
public:
    explicit Section(const QString& title, QWidget* parent = nullptr);
    QWidget* content() const { return content_; }
    void setOpen(bool open);
    bool isOpen() const;

private:
    QString title_;
    QPushButton* header_;
    QWidget* content_;
};

} // namespace clothcad::ui
