#include "lineformat.h"
#include "lineassembler.h"

namespace LineFormat {

const QStringList &modes()
{
    static const QStringList list = {
        QStringLiteral("ASCII"),
        QStringLiteral("Escaped"),
        QStringLiteral("HEX"),
        QStringLiteral("HEX + ASCII"),
    };
    return list;
}

QString prefix(const DisplayOptions &options, bool isTx, const QDateTime &when)
{
    if (!options.timestamps && !options.direction)
        return {};

    QString out;
    if (options.timestamps)
        out += QLatin1Char('[') + when.toString(QStringLiteral("HH:mm:ss.zzz")) + QLatin1Char(']');
    if (options.direction) {
        if (!out.isEmpty())
            out += QLatin1Char(' ');
        out += isTx ? QStringLiteral("TX") : QStringLiteral("RX");
    }
    return out + QStringLiteral("  ");
}

QString escapeControls(const QString &text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar ch : text) {
        const char16_t u = ch.unicode();
        switch (u) {
        case 0x5C: out += QStringLiteral("\\\\"); break;   // backslash
        case 0x0D: out += QStringLiteral("\\r");  break;
        case 0x0A: out += QStringLiteral("\\n");  break;
        case 0x09: out += QStringLiteral("\\t");  break;
        default:
            if (u < 0x20 || u == 0x7F || (u >= 0x80 && u < 0xA0))
                out += QStringLiteral("\\x%1").arg(int(u), 2, 16, QLatin1Char('0'));
            else
                out += ch;
        }
    }
    return out;
}

QString body(const DisplayOptions &options, const QString &text, const QByteArray &bytes)
{
    // Only the ASCII view is cleaned. The other views exist precisely to show
    // what actually arrived, escape bytes included.
    if (options.mode == QLatin1String("ASCII"))
        return stripEscapeSequences(text);
    if (options.mode == QLatin1String("Escaped"))
        return escapeControls(text);

    const bool withAscii = (options.mode == QLatin1String("HEX + ASCII"));

    QString hex;
    hex.reserve(bytes.size() * 3);
    QString ascii;
    if (withAscii)
        ascii.reserve(bytes.size());

    for (int i = 0; i < bytes.size(); ++i) {
        const uchar byte = uchar(bytes.at(i));
        if (i)
            hex += QLatin1Char(' ');
        hex += QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
        if (withAscii)
            ascii += (byte >= 0x20 && byte < 0x7F) ? QChar(byte) : QLatin1Char('.');
    }

    if (!withAscii)
        return hex;
    return hex + QStringLiteral("    ") + ascii;
}

} // namespace LineFormat
