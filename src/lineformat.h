#ifndef LINEFORMAT_H
#define LINEFORMAT_H

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
    QString mode       = QStringLiteral("ASCII");   ///< ASCII | HEX | HEX + ASCII
};

/// One line of traffic, in both its raw and its displayable forms.
///
/// `raw` is what matching, filtering and searching work on, so a colour rule
/// or filter written for the text keeps working when the view is switched to
/// hex.
struct LineRecord
{
    QString raw;          ///< exactly what came off the wire
    QString prefix;       ///< timestamp and/or direction, may be empty
    QString body;         ///< the raw line rendered per the display mode
    QColor  color;        ///< colour-rule colour, or the default/TX shade
    QString colorRule;    ///< name of the rule that matched, empty if none
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

/// The line body, rendered per the display mode.
QString body(const DisplayOptions &options, const QString &raw);

} // namespace LineFormat

#endif // LINEFORMAT_H
