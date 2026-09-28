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

/// Removes ANSI/VT escape sequences and non-printable control bytes, keeping
/// tabs.
///
/// For display only. Firmware that colours its own output emits sequences like
/// ESC [ 3 1 m; a terminal emulator obeys them, but UartX is a viewer, so
/// without this they reach the screen as literal "[31m" noise around every
/// line. The bytes themselves are never altered -- colour rules still match the
/// raw text, HEX view still shows every byte, and the session log still records
/// exactly what arrived.
QString stripEscapeSequences(const QString &s);

#endif // LINEASSEMBLER_H
