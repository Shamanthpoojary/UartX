#ifndef TERMINALVIEW_H
#define TERMINALVIEW_H

#include <QPlainTextEdit>
#include <QHash>
#include <QTextCharFormat>
#include <QColor>

/// The shared dark-terminal widget: colour-per-line output, no word wrap,
/// capped scrollback, right-click / Ctrl+C copy, and (in the main window)
/// keystroke forwarding to the device.
///
/// The widget knows nothing about filters or log formats -- callers decide
/// what colour a line gets.
class TerminalView : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit TerminalView(QWidget *parent = nullptr);

    /// Appends one line in `color`. While a partial (not yet
    /// newline-terminated) line is open the text is appended in the default
    /// colour, so a continuation cannot recolour what is already on screen.
    void appendLine(const QString &line, const QColor &color, bool newline = true);

    /// Appends a line preceded by a dimmed prefix -- the timestamp and RX/TX
    /// marker -- so the annotation sits behind the content it describes.
    void appendLine(const QString &prefix, const QColor &prefixColor,
                    const QString &line, const QColor &color, bool newline = true);

    void clearScreen();

    void setAutoscroll(bool on) { m_autoscroll = on; }
    bool autoscroll() const     { return m_autoscroll; }

    /// Set false for the filtered views, which must not talk to the device.
    void setForwardKeys(bool on) { m_forwardKeys = on; }

    /// Wrap a batch of appendLine() calls to avoid repainting per line.
    void beginBatch();
    void endBatch();

signals:
    /// Emitted for every keystroke that should reach the device.
    /// `isReturn` marks Enter, whose byte sequence the caller decides.
    void keyTyped(bool isReturn, const QString &text);

    /// Ctrl+C or right-click: the owner performs the copy and shows a notice.
    void copyRequested();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    const QTextCharFormat &formatFor(const QColor &color);
    void scrollToBottom();

    QHash<QRgb, QTextCharFormat> m_formats;
    QTextCharFormat              m_defaultFormat;
    bool m_autoscroll  = true;
    bool m_forwardKeys = true;
    bool m_partialOpen = false;
    bool m_batching    = false;
};

#endif // TERMINALVIEW_H
