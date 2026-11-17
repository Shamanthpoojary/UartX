#ifndef LINEASSEMBLER_H
#define LINEASSEMBLER_H

#include "linerecord.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

/// How the byte stream is cut into records.
///
/// Auto suits almost every text console; the other modes exist because UartX
/// makes no assumption about what is on the wire -- a binary protocol has no
/// line endings at all, and some devices end their messages with a byte of
/// their own choosing.
struct Framing
{
    enum class Mode {
        Auto,         ///< CR, LF, CRLF and LFCR each count as one line break
        Terminator,   ///< a custom byte sequence ends each record
        IdleGap,      ///< a pause in the stream ends a record (binary protocols)
        FixedLength,  ///< every N bytes is a record
        None,         ///< no framing: only the length cap and the quiet timer cut
    };

    Mode       mode = Mode::Auto;
    QByteArray terminator;              ///< Terminator mode
    int        idleGapMs     = 20;      ///< IdleGap mode
    int        fixedLength   = 16;      ///< FixedLength mode
    int        maxLineBytes  = 4096;    ///< a longer run is force-split and flagged
    int        quietFlushMs  = 250;     ///< an unterminated line is shown after this

    /// The names offered in Settings, in Mode order.
    static const QStringList &modeNames();
    static Mode modeFromName(const QString &name);

    /// Parses a terminator typed by the user: plain text, with \r \n \t \\ and
    /// \xNN escapes so control bytes can be named. Returns empty on nonsense.
    static QByteArray parseTerminator(const QString &text);
};

/// One record cut from the stream.
struct AssembledLine
{
    QByteArray bytes;   ///< the bytes this record adds (terminator excluded)
    QByteArray whole;   ///< Continuation only: the full line, earlier partials included
    quint8     flags = 0;
};

/// Raw serial bytes in, records out. Works on bytes, never on text: a
/// multi-byte UTF-8 character split across two reads is simply two halves of
/// one line here, and is decoded whole later.
///
/// Time is passed in rather than read from a clock, so every framing rule can
/// be tested without waiting.
class LineAssembler
{
public:
    LineAssembler() = default;
    explicit LineAssembler(const Framing &framing) : m_framing(framing) {}

    void setFraming(const Framing &framing);
    const Framing &framing() const { return m_framing; }

    /// Feeds a chunk received at `nowMs` (any monotonic clock); returns the
    /// records it completed.
    QVector<AssembledLine> feed(const QByteArray &data, qint64 nowMs);

    /// Called on a timer: emits whatever the quiet timer or the idle gap says
    /// is due by `nowMs`.
    QVector<AssembledLine> poll(qint64 nowMs);

    /// Hands back everything still buffered as a final Partial, for when the
    /// port closes.
    QVector<AssembledLine> drain();

    bool hasPending() const { return !m_buf.isEmpty(); }
    void reset();

private:
    void emitComplete(QVector<AssembledLine> *out, quint8 extraFlags = 0);
    void emitPartial(QVector<AssembledLine> *out);
    void appendByte(char c, QVector<AssembledLine> *out);

    Framing    m_framing;
    QByteArray m_buf;              ///< the current line, terminator excluded
    int        m_flushed = 0;      ///< bytes of m_buf already sent out as Partial
    char       m_lastEnding = 0;   ///< Auto mode: CR or LF just seen, for pair swallowing
    qint64     m_lastRxMs = 0;
};

/// A run of text carrying whatever colour the device asked for.
struct AnsiSpan
{
    QString text;
    int     color = -1;   ///< ANSI colour 0..15, or -1 where the device said nothing
};

/// Splits a line into coloured runs: SGR colour codes are obeyed, and every
/// other escape sequence and control byte is discarded.
///
/// Firmware that colours its own output is common, and a viewer that ignores
/// the codes shows them as literal "[31m" noise. Keeping the colour as an
/// index rather than a QColor leaves this file free of QtGui, so the parser
/// stays testable on its own and the palette lives with the rest of the theme.
QVector<AnsiSpan> parseAnsiSpans(const QString &s);

/// Removes ANSI/VT escape sequences and non-printable control bytes, keeping
/// tabs.
///
/// For display and matching only. Firmware that colours its own output emits
/// sequences like ESC [ 3 1 m; a terminal emulator obeys them, but UartX is a
/// viewer, so without this they reach the screen as literal "[31m" noise
/// around every line. The bytes themselves are never altered -- HEX view still
/// shows every byte, and raw.log still records exactly what arrived.
QString stripEscapeSequences(const QString &s);

#endif // LINEASSEMBLER_H
