#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace twintailor::ui::theme {

void apply(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, panel);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, field);
    p.setColor(QPalette::AlternateBase, header);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::PlaceholderText, quiet);
    p.setColor(QPalette::Button, header);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, QColor(74, 61, 31));
    p.setColor(QPalette::HighlightedText, QColor(255, 210, 122));
    p.setColor(QPalette::ToolTipBase, QColor(15, 16, 18));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Mid, QColor(58, 61, 68));
    p.setColor(QPalette::Dark, QColor(18, 19, 21));
    p.setColor(QPalette::Disabled, QPalette::Text, quiet.darker(140));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, quiet.darker(140));
    app.setPalette(p);
    // Only what the palette cannot say: dock titles, the shelf, fields.
    app.setStyleSheet(R"(
        QDockWidget::title { background: #2b2d32; padding: 4px 8px; }
        QToolBar#shelfRow { background: #2b2d32; border: 0; spacing: 3px; padding: 4px 6px; }
        QToolBar#shelfRow QToolButton { border-radius: 6px; padding: 4px; }
        QToolBar#shelfRow QToolButton:checked { background: #4a3d1f; border: 1px solid #ffbf45; }
        QLineEdit { border: 1px solid #3a3d44; border-radius: 3px; padding: 1px 6px; }
        QLineEdit[formula="true"] { color: #39c5bb; border-color: #1f635e; }
        QLineEdit[refused="true"] { border-color: #ff6b61; }
        QPushButton#sectionHeader { text-align: left; background: #26282c; border: 0; border-top: 1px solid #1a1b1e; padding: 4px 8px; font-weight: 600; }
        QStatusBar { background: #121315; }
    )");
}

} // namespace twintailor::ui::theme
