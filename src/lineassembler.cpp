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

QString sanitizeForLog(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u == 0x09)
            out.append(QLatin1Char('\t'));
        else if (u < 0x20 || u >= 0x7F)
            out.append(QLatin1Char('.'));
        else
            out.append(c);
    }
    return out;
}
