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

QString stripEscapeSequences(const QString &s)
{
    QString out;
    out.reserve(s.size());

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
                i += 2;
                while (i < n) {
                    const ushort c = s.at(i).unicode();
                    ++i;
                    if (c >= 0x40 && c <= 0x7E)
                        break;
                }
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
            out.append(QLatin1Char('\t'));
        else if (u >= 0x20 && u != 0x7F)
            out.append(s.at(i));
        // anything else is a control byte with nothing to show

        ++i;
    }
    return out;
}
