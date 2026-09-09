#include "lineformat.h"

namespace LineFormat {

const QStringList &modes()
{
    static const QStringList list = {
        QStringLiteral("ASCII"),
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

QString body(const DisplayOptions &options, const QString &raw)
{
    if (options.mode == QLatin1String("ASCII"))
        return raw;

    const bool withAscii = (options.mode == QLatin1String("HEX + ASCII"));

    // The line arrived as latin-1, so every code point is one byte and the
    // round trip back to bytes is exact.
    QString hex;
    hex.reserve(raw.size() * 3);
    QString ascii;
    if (withAscii)
        ascii.reserve(raw.size());

    for (int i = 0; i < raw.size(); ++i) {
        const ushort byte = raw.at(i).unicode() & 0xFF;
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
