#ifndef LINEFORMAT_H
#define LINEFORMAT_H

#include "lineassembler.h"

#include <QString>
#include <QStringList>
#include <QColor>
#include <QDateTime>

/// How a line should be presented on screen.
///
/// These are display concerns only. Nothing here ever reaches the log file:
/// the session log always receives the raw bytes exactly as the device sent
/// them, so a capture stays usable by other tools.
struct DisplayOptions
{
    bool    timestamps = false;                     ///< [HH:MM:SS.mmm] prefix
    bool    direction  = false;                     ///< RX / TX marker
    QString mode       = QStringLiteral("ASCII");   ///< ASCII | Escaped | HEX | HEX + ASCII
};

/// One line of traffic as a view draws it: the decoded text plus its
/// rendered, coloured form. Built on the pipeline thread from a LineRecord
/// (see linerecord.h), so the GUI thread only paints.
///
/// `raw` is what filtering and searching work on, so a colour rule or filter
/// written for the text keeps working when the view is switched to hex.
struct DisplayLine
{
    quint64 seq = 0;      ///< the record this line shows
    /// The decoded text with escape sequences removed -- what colour rules
    /// and text filters match. A device that colours "ERROR" in red still
    /// matches a rule for "ERROR" this way.
    QString raw;
    QString prefix;       ///< timestamp and/or direction, may be empty
    QString body;         ///< the raw line rendered per the display mode
    QColor  color;        ///< colour-rule colour, or the default/TX shade
    QString colorRule;    ///< name of the rule that matched, empty if none

    /// The line split into the colour runs the device asked for. Empty unless
    /// the ASCII view is active and the device actually sent colour; a
    /// matching colour rule takes precedence over these, because a rule is the
    /// user stating what matters and that outranks the firmware's own opinion.
    QVector<AnsiSpan> spans;
    bool    isTx = false; ///< true for data this application sent

    /// What a "save the visible text" operation should write.
    QString displayText() const { return prefix + body; }
};

namespace LineFormat {

/// The display modes offered in Settings.
const QStringList &modes();

/// The dimmed prefix: "[12:34:56.789] RX  ", or an empty string when both
/// options are off.
QString prefix(const DisplayOptions &options, bool isTx, const QDateTime &when);

/// The line body, rendered per the display mode: the decoded text for the
/// text views, the bytes themselves for the hex views.
QString body(const DisplayOptions &options, const QString &text, const QByteArray &bytes);

/// Control characters made visible: \r, \n, \t, \x1b... -- the Escaped
/// display mode. Unlike the log's escaping this works on decoded text, so
/// whatever the session's encoding shows as a character stays a character.
QString escapeControls(const QString &text);

} // namespace LineFormat

#include <QMetaType>
Q_DECLARE_METATYPE(DisplayOptions)

#endif // LINEFORMAT_H
