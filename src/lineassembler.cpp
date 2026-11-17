#include "lineassembler.h"

// ------------------------------------------------------------------- Framing

const QStringList &Framing::modeNames()
{
    static const QStringList names = {
        QStringLiteral("Auto (CR / LF / CRLF)"),
        QStringLiteral("Custom terminator"),
        QStringLiteral("Idle gap"),
        QStringLiteral("Fixed length"),
        QStringLiteral("None"),
    };
    return names;
}

Framing::Mode Framing::modeFromName(const QString &name)
{
    const int i = modeNames().indexOf(name);
    return i < 0 ? Mode::Auto : static_cast<Mode>(i);
}

QByteArray Framing::parseTerminator(const QString &text)
{
    const QByteArray in = text.toUtf8();
    QByteArray out;
    for (int i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (c != '\\' || i + 1 >= in.size()) {
            out.append(c);
            continue;
        }
        const char e = in.at(++i);
        switch (e) {
        case 'r':  out.append('\r'); break;
        case 'n':  out.append('\n'); break;
        case 't':  out.append('\t'); break;
        case '0':  out.append('\0'); break;
        case '\\': out.append('\\'); break;
        case 'x': {
            bool ok = false;
            const int v = in.mid(i + 1, 2).toInt(&ok, 16);
            if (!ok || i + 2 >= in.size())
                return {};
            out.append(static_cast<char>(v));
            i += 2;
            break;
        }
        default:
            return {};
        }
    }
    return out;
}

// ------------------------------------------------------------- LineAssembler

void LineAssembler::setFraming(const Framing &framing)
{
    m_framing = framing;
    // A terminator that has been typed away to nothing would never match, so
    // the stream would only ever be cut by the length cap; Auto is the
    // reasonable thing to fall back to.
    if (m_framing.mode == Framing::Mode::Terminator && m_framing.terminator.isEmpty())
        m_framing.mode = Framing::Mode::Auto;
    if (m_framing.fixedLength < 1)
        m_framing.fixedLength = 1;
    if (m_framing.maxLineBytes < 16)
        m_framing.maxLineBytes = 16;
}

void LineAssembler::reset()
{
    m_buf.clear();
    m_flushed = 0;
    m_lastEnding = 0;
    m_lastRxMs = 0;
}

void LineAssembler::emitComplete(QVector<AssembledLine> *out, quint8 extraFlags)
{
    AssembledLine line;
    if (m_flushed > 0) {
        // The front of this line is already on screen. Send only what is new,
        // plus the whole line for anything that has to see it complete --
        // filter views, and later the matcher -- so nothing is matched twice
        // and nothing is matched on half a line.
        line.bytes = m_buf.mid(qMin(m_flushed, int(m_buf.size())));
        line.whole = m_buf;
        line.flags = LineFlag::Continuation | extraFlags;
    } else {
        line.bytes = m_buf;
        line.flags = extraFlags;
    }
    out->append(line);
    m_buf.clear();
    m_flushed = 0;
}

void LineAssembler::emitPartial(QVector<AssembledLine> *out)
{
    AssembledLine line;
    line.bytes = m_buf.mid(m_flushed);
    line.flags = LineFlag::Partial | (m_flushed > 0 ? LineFlag::Continuation : 0);
    out->append(line);
    m_flushed = m_buf.size();
}

void LineAssembler::appendByte(char c, QVector<AssembledLine> *out)
{
    switch (m_framing.mode) {
    case Framing::Mode::Auto:
        if (c == '\r' || c == '\n') {
            // Second half of a CRLF / LFCR pair: swallow it.
            if (m_lastEnding && c != m_lastEnding) {
                m_lastEnding = 0;
                return;
            }
            m_lastEnding = c;
            emitComplete(out);
            return;
        }
        m_lastEnding = 0;
        m_buf.append(c);
        break;

    case Framing::Mode::Terminator:
        m_buf.append(c);
        if (m_buf.endsWith(m_framing.terminator)) {
            m_buf.chop(m_framing.terminator.size());
            // The start of the terminator may already have gone out in a
            // partial; it cannot be taken back, only not counted twice.
            m_flushed = qMin(m_flushed, int(m_buf.size()));
            emitComplete(out);
            return;
        }
        break;

    case Framing::Mode::FixedLength:
        m_buf.append(c);
        if (m_buf.size() >= m_framing.fixedLength) {
            emitComplete(out);
            return;
        }
        break;

    case Framing::Mode::IdleGap:
    case Framing::Mode::None:
        m_buf.append(c);
        break;
    }

    // Runaway output -- a binary stream read as text, or a device that never
    // ends a line -- must not grow one record without bound.
    if (m_buf.size() >= m_framing.maxLineBytes)
        emitComplete(out, LineFlag::Split);
}

QVector<AssembledLine> LineAssembler::feed(const QByteArray &data, qint64 nowMs)
{
    QVector<AssembledLine> out;
    out.reserve(data.size() / 40 + 1);

    // In idle-gap framing the pause *before* this chunk is what ends the
    // previous record, so check it before the new bytes join the buffer.
    if (m_framing.mode == Framing::Mode::IdleGap && !m_buf.isEmpty()
        && nowMs - m_lastRxMs >= m_framing.idleGapMs) {
        emitComplete(&out);
    }

    for (const char c : data)
        appendByte(c, &out);

    m_lastRxMs = nowMs;
    return out;
}

QVector<AssembledLine> LineAssembler::poll(qint64 nowMs)
{
    QVector<AssembledLine> out;
    if (m_buf.isEmpty())
        return out;

    const qint64 quiet = nowMs - m_lastRxMs;

    if (m_framing.mode == Framing::Mode::IdleGap && quiet >= m_framing.idleGapMs) {
        emitComplete(&out);
        return out;
    }

    // An unterminated line that has gone quiet is shown as-is, so prompts
    // without a newline still appear.
    if (m_buf.size() > m_flushed && quiet >= m_framing.quietFlushMs)
        emitPartial(&out);
    return out;
}

QVector<AssembledLine> LineAssembler::drain()
{
    QVector<AssembledLine> out;
    if (m_buf.size() > m_flushed)
        emitPartial(&out);
    m_buf.clear();
    m_flushed = 0;
    m_lastEnding = 0;
    return out;
}

// ---------------------------------------------------------------- ANSI spans

namespace {

/// Applies one SGR sequence's parameters to the running colour state.
///
/// Only the foreground matters here: UartX paints no backgrounds, and a device
/// asking for one would otherwise fight the terminal's own palette.
void applySgr(const QString &params, int *color, bool *bold)
{
    const QStringList parts = params.split(QLatin1Char(';'));
    for (const QString &part : parts) {
        bool ok = false;
        const int code = part.isEmpty() ? 0 : part.toInt(&ok);
        if (!part.isEmpty() && !ok)
            continue;

        if (code == 0) {                       // reset everything
            *color = -1;
            *bold = false;
        } else if (code == 1) {
            *bold = true;
            if (*color >= 0 && *color < 8)     // bold promotes to the bright half
                *color += 8;
        } else if (code == 22) {
            *bold = false;
            if (*color >= 8)
                *color -= 8;
        } else if (code >= 30 && code <= 37) {
            *color = (code - 30) + (*bold ? 8 : 0);
        } else if (code >= 90 && code <= 97) {
            *color = (code - 90) + 8;
        } else if (code == 39) {               // back to the default foreground
            *color = -1;
        }
        // 40-49 are backgrounds and 38/48 are the 256-colour and truecolour
        // forms; all deliberately ignored.
    }
}

} // namespace

QVector<AnsiSpan> parseAnsiSpans(const QString &s)
{
    QVector<AnsiSpan> spans;
    QString current;
    int color = -1;
    bool bold = false;

    auto flush = [&]() {
        if (!current.isEmpty()) {
            spans.append(AnsiSpan{ current, color });
            current.clear();
        }
    };

    int i = 0;
    const int n = s.size();
    while (i < n) {
        const ushort u = s.at(i).unicode();

        if (u == 0x1B) {                     // ESC
            if (i + 1 >= n) {                // dangling ESC at the end of a line
                ++i;
                continue;
            }
            const ushort next = s.at(i + 1).unicode();

            if (next == '[') {
                // CSI: ESC [ parameters, ending on a byte in 0x40..0x7E.
                // Colour codes are this shape -- ESC [ 3 1 m.
                const int paramStart = i + 2;
                int j = paramStart;
                ushort final = 0;
                while (j < n) {
                    const ushort c = s.at(j).unicode();
                    ++j;
                    if (c >= 0x40 && c <= 0x7E) {
                        final = c;
                        break;
                    }
                }
                if (final == 'm') {
                    // A colour change starts a new run.
                    flush();
                    applySgr(s.mid(paramStart, j - paramStart - 1), &color, &bold);
                }
                i = j;
            } else if (next == ']') {
                // OSC: ESC ] ... terminated by BEL, or by ST (ESC backslash).
                i += 2;
                while (i < n) {
                    const ushort c = s.at(i).unicode();
                    if (c == 0x07) { ++i; break; }
                    if (c == 0x1B && i + 1 < n && s.at(i + 1) == QLatin1Char('\\')) {
                        i += 2;
                        break;
                    }
                    ++i;
                }
            } else if (next == '(' || next == ')' || next == '*' || next == '+') {
                i += 3;                      // charset selection: ESC ( B
            } else {
                i += 2;                      // any other two-byte escape
            }
            continue;
        }

        if (u == 0x09)                       // tabs are worth keeping
            current.append(QLatin1Char('\t'));
        else if (u >= 0x20 && u != 0x7F)
            current.append(s.at(i));
        // anything else is a control byte with nothing to show

        ++i;
    }

    flush();
    return spans;
}

QString stripEscapeSequences(const QString &s)
{
    // Expressed through the span parser so the cleaned text and the coloured
    // runs can never disagree about what survived.
    QString out;
    const QVector<AnsiSpan> spans = parseAnsiSpans(s);
    for (const AnsiSpan &span : spans)
        out += span.text;
    return out;
}
