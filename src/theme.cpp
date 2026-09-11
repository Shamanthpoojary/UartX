#include "theme.h"
#include "appconstants.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QStringList>

namespace Theme {

QPalette palette()
{
    const QColor window(0x14, 0x14, 0x14);
    const QColor base(0x12, 0x12, 0x12);
    const QColor text(0xd8, 0xd8, 0xd8);
    const QColor button(0x2a, 0x2a, 0x2a);
    const QColor dim(0x6a, 0x6a, 0x6a);

    QPalette p;
    p.setColor(QPalette::Window,          window);
    p.setColor(QPalette::WindowText,      text);
    p.setColor(QPalette::Base,            base);
    p.setColor(QPalette::AlternateBase,   QColor(0x1a, 0x1a, 0x1a));
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          button);
    p.setColor(QPalette::ButtonText,      text);
    p.setColor(QPalette::BrightText,      Qt::white);
    p.setColor(QPalette::ToolTipBase,     QColor(0x23, 0x23, 0x23));
    p.setColor(QPalette::ToolTipText,     text);
    p.setColor(QPalette::PlaceholderText, QColor(0x7d, 0x7d, 0x7d));
    p.setColor(QPalette::Highlight,       QColor(0x45, 0x45, 0x45));
    p.setColor(QPalette::HighlightedText, Qt::white);
    // The only non-grey in the palette, and only because an unreadable link is
    // worse than a trace of colour. Nothing else may introduce a hue.
    p.setColor(QPalette::Link,            QColor(0x9a, 0xc8, 0xff));

    p.setColor(QPalette::Disabled, QPalette::WindowText, dim);
    p.setColor(QPalette::Disabled, QPalette::Text,       dim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dim);
    return p;
}

void apply(::QApplication &app)
{
    app.setPalette(palette());
    app.setStyleSheet(styleSheet());
}

QString styleSheet()
{
    return QStringLiteral(R"(
        /* A neutral black-and-grey palette: no accent hue anywhere, so the
           only colour on screen comes from the user's own colour rules. */
        QMainWindow, QMenuBar { background: #141414; }

        /* Dialogs need saying explicitly. A stylesheet only reaches the widget
           types it names, so anything left out keeps the platform's own
           colours -- which on Linux is a light window, and the QLabel rule
           below then paints light-grey text onto it. */
        QDialog, QMessageBox, QInputDialog, QFileDialog, QColorDialog,
        QFontDialog, QProgressDialog, QWizard { background: #141414; }
        QMessageBox QLabel, QInputDialog QLabel { color: #d8d8d8; }
        QMenuBar { border-bottom: 1px solid #2e2e2e; padding: 2px 4px; }
        QMenuBar::item { padding: 5px 12px; border-radius: 4px; color: #d8d8d8; }
        QMenuBar::item:selected { background: #2e2e2e; }
        QMenu { background: #1c1c1c; border: 1px solid #3a3a3a; color: #d8d8d8; }
        QMenu::item { padding: 5px 24px 5px 22px; }
        QMenu::item:selected { background: #333333; }
        QMenu::separator { height: 1px; background: #333333; margin: 4px 8px; }

        #ribbonBar { background: #1a1a1a; border-bottom: 1px solid #2e2e2e; }
        #ribbonGroup {
            background: #202020;
            border: 1px solid #2e2e2e;
            border-radius: 6px;
        }
        #ribbonGroupTitle { color: #7d7d7d; font-size: 10px; }
        #ribbonSummary { color: #9a9a9a; padding-left: 4px; }
        #ribbonChevron { border: none; }
        #ribbonChevron:hover { background: #303030; border-radius: 4px; }
        #fieldLabel { color: #b4b4b4; }

        QLabel { color: #d8d8d8; }
        QGroupBox {
            color: #b4b4b4;
            border: 1px solid #2e2e2e;
            border-radius: 6px;
            margin-top: 8px;
            padding-top: 6px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
        }

        QPushButton {
            background: #2a2a2a;
            border: 1px solid #3a3a3a;
            border-radius: 4px;
            padding: 4px 10px;
            color: #d8d8d8;
        }
        QPushButton:hover   { background: #343434; border-color: #4a4a4a; }
        QPushButton:pressed { background: #404040; }
        QPushButton:disabled { color: #6a6a6a; background: #1e1e1e; border-color: #2e2e2e; }
        #primaryButton {
            background: #4a4a4a;
            border: 1px solid #666666;
            color: #ffffff;
            font-weight: bold;
        }
        #primaryButton:hover   { background: #565656; }
        #primaryButton:pressed { background: #3e3e3e; }

        QComboBox, QLineEdit {
            background: #121212;
            border: 1px solid #3a3a3a;
            border-radius: 4px;
            padding: 3px 6px;
            color: #d8d8d8;
            selection-background-color: #454545;
            selection-color: #ffffff;
        }
        QComboBox:focus, QLineEdit:focus { border-color: #7a7a7a; }
        QComboBox:disabled, QLineEdit:disabled { color: #6a6a6a; background: #1a1a1a; }

        /* Styling the drop-down sub-control stops Qt drawing its own arrow, so
           the chevron is supplied as an image and the divider makes it read as
           a dropdown at a glance. */
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 20px;
            border-left: 1px solid #3a3a3a;
        }
        QComboBox::down-arrow {
            image: url(:/icons/chevron_down.png);
            width: 11px;
            height: 11px;
        }
        QComboBox::down-arrow:disabled { image: url(:/icons/chevron_down_dim.png); }
        QComboBox QAbstractItemView {
            background: #1c1c1c;
            border: 1px solid #3a3a3a;
            selection-background-color: #3a3a3a;
            color: #d8d8d8;
            outline: none;
        }

        QCheckBox { color: #d8d8d8; spacing: 6px; }
        QCheckBox::indicator, QAbstractItemView::indicator {
            width: 15px;
            height: 15px;
            border: 1px solid #4a4a4a;
            border-radius: 3px;
            background: #121212;
        }
        QCheckBox::indicator:hover, QAbstractItemView::indicator:hover {
            border-color: #7a7a7a;
        }
        QCheckBox::indicator:checked, QAbstractItemView::indicator:checked {
            background: #5a5a5a;
            border-color: #7a7a7a;
            image: url(:/icons/check.png);
        }
        QCheckBox::indicator:disabled { border-color: #333333; background: #1a1a1a; }

        QTableWidget, QTableView {
            background: #161616;
            alternate-background-color: #1a1a1a;
            border: 1px solid #2e2e2e;
            gridline-color: #2a2a2a;
            color: #d8d8d8;
            selection-background-color: #333333;
            selection-color: #ffffff;
            /* Without this the platform draws a focus rectangle tight against
               the cell text, which reads as a stray bar beside every value. */
            outline: none;
        }
        QTableView::item {
            padding: 4px 8px;
            border: none;
        }
        QTableView::item:selected { background: #333333; color: #ffffff; }
        QHeaderView::section {
            background: #232323;
            color: #b4b4b4;
            border: none;
            border-right: 1px solid #2e2e2e;
            border-bottom: 1px solid #2e2e2e;
            padding: 4px;
        }

        QScrollBar:vertical, QScrollBar:horizontal { background: #161616; border: none; }
        QScrollBar:vertical { width: 12px; }
        QScrollBar:horizontal { height: 12px; }
        QScrollBar::handle {
            background: #3a3a3a;
            border-radius: 5px;
            min-width: 24px;
            min-height: 24px;
        }
        QScrollBar::handle:hover { background: #4a4a4a; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        QTextBrowser { background: #161616; color: #d8d8d8; }

        /* The rest of what dialogs are built from. Each of these would
           otherwise be drawn from the platform palette. */
        QListWidget, QListView, QTreeView, QPlainTextEdit, QTextEdit {
            background: #121212;
            border: 1px solid #3a3a3a;
            color: #d8d8d8;
            selection-background-color: #454545;
            selection-color: #ffffff;
            outline: none;
        }
        QSpinBox, QDoubleSpinBox {
            background: #121212;
            border: 1px solid #3a3a3a;
            border-radius: 4px;
            padding: 3px 6px;
            color: #d8d8d8;
        }
        QRadioButton { color: #d8d8d8; spacing: 6px; }
        QToolButton { background: transparent; border: none; color: #d8d8d8; }
        QTabWidget::pane { border: 1px solid #2e2e2e; background: #141414; }
        QTabBar::tab {
            background: #1c1c1c;
            color: #b4b4b4;
            border: 1px solid #2e2e2e;
            padding: 5px 12px;
        }
        QTabBar::tab:selected { background: #2a2a2a; color: #e8e8e8; }
        QSplitter::handle { background: #2e2e2e; }

        QStatusBar { background: #141414; color: #9a9a9a; }
        QStatusBar::item { border: none; }
        QToolTip {
            background: #232323;
            color: #d8d8d8;
            border: 1px solid #3a3a3a;
            padding: 3px;
        }
    )");
}

} // namespace Theme

QFont Theme::monospaceFont()
{
    static const QFont chosen = [] {
        // Ordered by preference per platform, ending in the generic names the
        // fontconfig/GDI aliases resolve.
        const QStringList candidates = {
#if defined(Q_OS_WIN)
            QStringLiteral("Consolas"),
            QStringLiteral("Lucida Console"),
#elif defined(Q_OS_MACOS)
            QStringLiteral("Menlo"),
            QStringLiteral("Monaco"),
#else
            QStringLiteral("DejaVu Sans Mono"),
            QStringLiteral("Liberation Mono"),
            QStringLiteral("Noto Sans Mono"),
            QStringLiteral("Ubuntu Mono"),
            QStringLiteral("Monospace"),
#endif
        };

        const QStringList installed = QFontDatabase::families();
        for (const QString &family : candidates) {
            if (installed.contains(family, Qt::CaseInsensitive)) {
                QFont f(family, App::TERM_FONT_SIZE);
                f.setStyleHint(QFont::Monospace);
                f.setFixedPitch(true);
                return f;
            }
        }

        // Nothing matched by name: let the style hint pick a fixed-pitch face.
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setPointSize(App::TERM_FONT_SIZE);
        f.setStyleHint(QFont::Monospace);
        f.setFixedPitch(true);
        return f;
    }();
    return chosen;
}

QIcon Theme::appIcon()
{
    static const QIcon icon = [] {
        QIcon result;
        for (int size : { 16, 24, 32, 48, 64, 128, 256 }) {
            const QString path = QStringLiteral(":/icons/uartx_%1.png").arg(size);
            const QPixmap pm(path);
            if (!pm.isNull())
                result.addPixmap(pm);
        }
        return result;
    }();
    return icon;
}

QColor Theme::contrastingInk(const QColor &background)
{
    // Rec. 601 luma: close enough to perceived brightness for a two-way choice.
    const double luma = 0.299 * background.red()
                      + 0.587 * background.green()
                      + 0.114 * background.blue();
    return luma < 128.0 ? QColor(Qt::white) : QColor(Qt::black);
}

QPixmap Theme::wordmark(const QColor &ink, int width)
{
    const QPixmap mask(QStringLiteral(":/icons/uartx_logo.png"));
    if (mask.isNull())
        return {};

    // The asset is white-on-transparent, so painting the ink through its own
    // alpha recolours the lettering and leaves the background untouched.
    QPixmap tinted(mask.size());
    tinted.fill(Qt::transparent);
    QPainter p(&tinted);
    p.drawPixmap(0, 0, mask);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(tinted.rect(), ink);
    p.end();

    return width > 0 ? tinted.scaledToWidth(width, Qt::SmoothTransformation) : tinted;
}
