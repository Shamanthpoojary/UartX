#include "terminalview.h"
#include "appconstants.h"
#include "theme.h"

#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QFont>

TerminalView::TerminalView(QWidget *parent)
    : QPlainTextEdit(parent)
{
    const QFont f = Theme::monospaceFont();
    setFont(f);

    setReadOnly(true);
    setUndoRedoEnabled(false);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setMaximumBlockCount(App::MAX_SCROLLBACK_LINES);
    setFocusPolicy(Qt::StrongFocus);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setCursorWidth(0);

    setStyleSheet(QStringLiteral(
        "QPlainTextEdit {"
        "  background-color: %1;"
        "  color: %2;"
        "  selection-background-color: %3;"
        "  selection-color: %4;"
        "  border: 1px solid #2b2b2b;"
        "}").arg(App::TERM_BG, App::TERM_FG, App::TERM_SEL_BG, App::TERM_SEL_FG));

    m_defaultFormat.setForeground(QColor(App::TERM_FG));
    m_defaultFormat.setFont(f);
}

const QTextCharFormat &TerminalView::formatFor(const QColor &color)
{
    if (!color.isValid())
        return m_defaultFormat;

    const QRgb key = color.rgb();
    auto it = m_formats.find(key);
    if (it == m_formats.end()) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        fmt.setFont(font());
        it = m_formats.insert(key, fmt);
    }
    return it.value();
}

void TerminalView::beginBatch()
{
    m_batching = true;
    setUpdatesEnabled(false);
}

void TerminalView::endBatch()
{
    m_batching = false;
    setUpdatesEnabled(true);
    if (m_autoscroll)
        scrollToBottom();
}

void TerminalView::appendLine(const QString &line, const QColor &color, bool newline)
{
    appendLine(QString(), QColor(), line, color, newline);
}

void TerminalView::appendLine(const QString &prefix, const QColor &prefixColor,
                              const QString &line, const QColor &color, bool newline)
{
    QTextCursor cur(document());
    cur.movePosition(QTextCursor::End);

    // A continuation of an already-printed partial line stays uncoloured, and
    // must not repeat the prefix.
    if (!m_partialOpen && !prefix.isEmpty())
        cur.insertText(prefix, formatFor(prefixColor));

    const QTextCharFormat &fmt = m_partialOpen ? m_defaultFormat : formatFor(color);
    cur.insertText(newline ? line + QLatin1Char('\n') : line, fmt);

    m_partialOpen = !newline;

    if (m_autoscroll && !m_batching)
        scrollToBottom();
}

void TerminalView::clearScreen()
{
    clear();
    m_partialOpen = false;
}

void TerminalView::scrollToBottom()
{
    QScrollBar *bar = verticalScrollBar();
    bar->setValue(bar->maximum());
}

void TerminalView::keyPressEvent(QKeyEvent *e)
{
    // Ctrl+C copies the selection rather than sending 0x03 to the device.
    if (e->matches(QKeySequence::Copy)
        || (e->key() == Qt::Key_C && e->modifiers().testFlag(Qt::ControlModifier))) {
        emit copyRequested();
        e->accept();
        return;
    }

    // Selection / navigation keys keep their normal meaning; they are never
    // forwarded to the device.
    switch (e->key()) {
    case Qt::Key_Up:     case Qt::Key_Down:
    case Qt::Key_Left:   case Qt::Key_Right:
    case Qt::Key_PageUp: case Qt::Key_PageDown:
    case Qt::Key_Home:   case Qt::Key_End:
        QPlainTextEdit::keyPressEvent(e);
        return;
    default:
        break;
    }

    if (!m_forwardKeys || e->modifiers().testFlag(Qt::ControlModifier)) {
        QPlainTextEdit::keyPressEvent(e);
        return;
    }

    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        emit keyTyped(true, QString());
        e->accept();
        return;
    }

    const QString text = e->text();
    if (!text.isEmpty() && text.at(0) != QChar(0)) {
        emit keyTyped(false, text);
        e->accept();
        return;
    }

    QPlainTextEdit::keyPressEvent(e);
}

void TerminalView::contextMenuEvent(QContextMenuEvent *e)
{
    // Right-click copies the selection directly, without a menu.
    emit copyRequested();
    e->accept();
}
