#ifndef LINEASSEMBLER_H
#define LINEASSEMBLER_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>

/// Raw serial bytes in, normalized complete lines out.
///
/// CR, LF, CRLF and LFCR each count as exactly one line break; terminators
/// split across read chunks are handled; latin-1 decoding means no byte
/// sequence can raise.
class LineAssembler
{
public:
    LineAssembler();

    /// Feeds a chunk of bytes; returns every complete line it produced.
    QStringList feed(const QByteArray &data);

    /// True while an unterminated line is buffered.
    bool hasPartial() const { return !m_buf.isEmpty(); }

    /// Removes and returns the buffered unterminated line.
    QString takePartial();

    /// Milliseconds since the last feed() call.
    qint64 msSinceLastRx() const;

    void reset();

private:
    QString       m_buf;
    QChar         m_lastEnding;
    QElapsedTimer m_rxTimer;
};

/// Log sanitizer: keeps printable ASCII plus tab, turns anything else into '.'
/// so text editors always detect the encoding correctly.
QString sanitizeForLog(const QString &s);

#endif // LINEASSEMBLER_H
