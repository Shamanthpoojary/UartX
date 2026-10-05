#include "lineassembler.h"

LineAssembler::LineAssembler()
{
    reset();
}

void LineAssembler::reset()
{
    m_buf.clear();
    m_lastEnding = QChar();
    m_rxTimer.start();
}

QStringList LineAssembler::feed(const QByteArray &data)
{
    QStringList out;
    out.reserve(data.size() / 40 + 1);

    for (const char raw : data) {
        // latin-1 decode: every byte maps to exactly one code point
        const QChar ch(static_cast<ushort>(static_cast<unsigned char>(raw)));

        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            // second half of a CRLF / LFCR pair: swallow it
            if (!m_lastEnding.isNull() && ch != m_lastEnding) {
                m_lastEnding = QChar();
                continue;
            }
            m_lastEnding = ch;
            out.append(m_buf);
            m_buf.clear();
        } else {
            m_lastEnding = QChar();
            m_buf.append(ch);
        }
    }

    m_rxTimer.restart();
    return out;
}

QString LineAssembler::takePartial()
{
    const QString s = m_buf;
    m_buf.clear();
    return s;
}

qint64 LineAssembler::msSinceLastRx() const
{
    return m_rxTimer.elapsed();
}

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
