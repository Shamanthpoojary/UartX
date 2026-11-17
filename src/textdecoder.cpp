#include "textdecoder.h"

#include <QStringDecoder>
#include <QStringEncoder>

namespace {

/// Windows-1252 differs from Latin-1 only in 0x80..0x9F. Zero marks the five
/// bytes the code page leaves undefined.
constexpr char16_t CP1252_HIGH[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

/// Multi-byte codecs worth offering when the Qt build can provide them.
const char *const LEGACY_CANDIDATES[] = {
    "GBK", "GB18030", "Big5", "Shift_JIS", "EUC-JP", "EUC-KR", "KOI8-R",
};

void appendInvalid(QString *out, uchar byte)
{
    // ‹xNN›: unmistakable, and never something a device would print itself.
    out->append(QChar(0x2039));
    out->append(QLatin1Char('x'));
    out->append(QString::number(byte, 16).rightJustified(2, QLatin1Char('0')).toUpper());
    out->append(QChar(0x203A));
}

/// Length of the valid UTF-8 sequence at p (1..4), or 0 when the byte there
/// does not start one. Overlong forms, surrogates and values past U+10FFFF
/// are rejected, as the standard requires.
int validUtf8(const uchar *p, int n, char32_t *cp)
{
    const uchar b0 = p[0];
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }
    int len = 0;
    char32_t v = 0;
    char32_t min = 0;
    if ((b0 & 0xE0) == 0xC0)      { len = 2; v = b0 & 0x1F; min = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { len = 3; v = b0 & 0x0F; min = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { len = 4; v = b0 & 0x07; min = 0x10000; }
    else
        return 0;
    if (n < len)
        return 0;
    for (int i = 1; i < len; ++i) {
        if ((p[i] & 0xC0) != 0x80)
            return 0;
        v = (v << 6) | (p[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
        return 0;
    *cp = v;
    return len;
}

void appendCodePoint(QString *out, char32_t cp)
{
    if (cp < 0x10000) {
        out->append(QChar(char16_t(cp)));
    } else {
        out->append(QChar::highSurrogate(cp));
        out->append(QChar::lowSurrogate(cp));
    }
}

} // namespace

TextDecoder::TextDecoder() = default;
TextDecoder::~TextDecoder() = default;

TextDecoder::TextDecoder(const TextDecoder &other)
{
    setEncoding(other.m_name);
}

TextDecoder &TextDecoder::operator=(const TextDecoder &other)
{
    if (this != &other)
        setEncoding(other.m_name);
    return *this;
}

QStringList TextDecoder::available()
{
    static const QStringList list = [] {
        QStringList names = {
            QStringLiteral("Auto"), QStringLiteral("UTF-8"),
            QStringLiteral("Latin-1"), QStringLiteral("Windows-1252"),
        };
        for (const char *name : LEGACY_CANDIDATES) {
            QStringDecoder probe(name, QStringConverter::Flag::Stateless);
            if (probe.isValid())
                names.append(QString::fromLatin1(name));
        }
        return names;
    }();
    return list;
}

bool TextDecoder::setEncoding(const QString &name)
{
    m_named.reset();
    m_name = name;
    if (name == QLatin1String("UTF-8"))             { m_kind = Kind::Utf8;        return true; }
    if (name == QLatin1String("Latin-1"))           { m_kind = Kind::Latin1;      return true; }
    if (name == QLatin1String("Windows-1252"))      { m_kind = Kind::Windows1252; return true; }
    if (name != QLatin1String("Auto") && available().contains(name)) {
        m_named = std::make_unique<QStringDecoder>(name.toLatin1().constData(),
                                                   QStringConverter::Flag::Stateless);
        if (m_named->isValid()) {
            m_kind = Kind::Named;
            return true;
        }
        m_named.reset();
    }
    m_kind = Kind::Auto;
    const bool known = (name == QLatin1String("Auto"));
    m_name = QStringLiteral("Auto");
    return known;
}

QString TextDecoder::decodeAuto(const QByteArray &bytes)
{
    QString out;
    out.reserve(bytes.size());
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData());
    const int n = bytes.size();
    int i = 0;
    while (i < n) {
        // Fast path for the ASCII that most consoles are made of.
        if (p[i] < 0x80) {
            out.append(QLatin1Char(char(p[i])));
            ++i;
            continue;
        }
        char32_t cp = 0;
        const int len = validUtf8(p + i, n - i, &cp);
        if (len == 0) {
            appendInvalid(&out, p[i]);
            ++i;
        } else {
            appendCodePoint(&out, cp);
            i += len;
        }
    }
    return out;
}

QString TextDecoder::decode(const QByteArray &bytes) const
{
    switch (m_kind) {
    case Kind::Auto:
        return decodeAuto(bytes);
    case Kind::Utf8:
        return QString::fromUtf8(bytes);
    case Kind::Latin1:
        return QString::fromLatin1(bytes);
    case Kind::Windows1252: {
        QString out;
        out.reserve(bytes.size());
        for (const char c : bytes) {
            const uchar b = uchar(c);
            if (b >= 0x80 && b < 0xA0) {
                const char16_t u = CP1252_HIGH[b - 0x80];
                if (u)
                    out.append(QChar(u));
                else
                    appendInvalid(&out, b);
            } else {
                out.append(QChar(char16_t(b)));
            }
        }
        return out;
    }
    case Kind::Named:
        return m_named->decode(bytes);
    }
    return decodeAuto(bytes);
}

QByteArray TextDecoder::encode(const QString &text) const
{
    switch (m_kind) {
    case Kind::Latin1:
        return text.toLatin1();
    case Kind::Windows1252: {
        QByteArray out;
        out.reserve(text.size());
        for (const QChar ch : text) {
            const char16_t u = ch.unicode();
            if (u < 0x80 || (u >= 0xA0 && u < 0x100)) {
                out.append(char(u));
                continue;
            }
            char mapped = '?';
            for (int i = 0; i < 32; ++i) {
                if (CP1252_HIGH[i] == u) {
                    mapped = char(0x80 + i);
                    break;
                }
            }
            out.append(mapped);
        }
        return out;
    }
    case Kind::Named: {
        QStringEncoder enc(m_name.toLatin1().constData());
        if (enc.isValid())
            return enc.encode(text);
        return text.toUtf8();
    }
    case Kind::Auto:
    case Kind::Utf8:
        break;
    }
    return text.toUtf8();
}

QString TextDecoder::escape(const QByteArray &bytes)
{
    QString out;
    out.reserve(bytes.size() + 8);
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData());
    const int n = bytes.size();
    int i = 0;
    auto hex = [&out](uchar b) {
        out.append(QStringLiteral("\\x"));
        out.append(QString::number(b, 16).rightJustified(2, QLatin1Char('0')));
    };
    while (i < n) {
        const uchar b = p[i];
        if (b < 0x80) {
            switch (b) {
            case '\\': out.append(QStringLiteral("\\\\")); break;
            case '\r': out.append(QStringLiteral("\\r"));  break;
            case '\n': out.append(QStringLiteral("\\n"));  break;
            case '\t': out.append(QStringLiteral("\\t"));  break;
            default:
                if (b < 0x20 || b == 0x7F)
                    hex(b);
                else
                    out.append(QLatin1Char(char(b)));
            }
            ++i;
            continue;
        }
        char32_t cp = 0;
        const int len = validUtf8(p + i, n - i, &cp);
        // C1 controls (U+0080..U+009F) are valid UTF-8 but invisible; spell
        // them out like the C0 ones.
        if (len == 0 || (cp >= 0x80 && cp < 0xA0)) {
            for (int k = 0; k < qMax(len, 1); ++k)
                hex(p[i + k]);
            i += qMax(len, 1);
        } else {
            appendCodePoint(&out, cp);
            i += len;
        }
    }
    return out;
}
