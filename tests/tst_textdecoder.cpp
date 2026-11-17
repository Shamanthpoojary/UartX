#include "lineformat.h"
#include "textdecoder.h"

#include <QtTest>

class TestTextDecoder : public QObject
{
    Q_OBJECT

private slots:
    void autoKeepsValidUtf8()
    {
        TextDecoder d;
        QCOMPARE(d.decode("caf\xC3\xA9 \xE2\x9C\x93 \xF0\x9F\x98\x80"),
                 QString::fromUtf8("caf\xC3\xA9 \xE2\x9C\x93 \xF0\x9F\x98\x80"));
    }

    void autoMarksInvalidBytes()
    {
        TextDecoder d;
        // A lone continuation byte, a truncated sequence, and 0xFF.
        QCOMPARE(d.decode("a\x80" "b\xC3" "c\xFF"),
                 QString::fromUtf8("a\xE2\x80\xB9x80\xE2\x80\xBA" "b\xE2\x80\xB9xC3\xE2\x80\xBA"
                                   "c\xE2\x80\xB9xFF\xE2\x80\xBA"));
    }

    void autoRejectsOverlongAndSurrogates()
    {
        TextDecoder d;
        // C0 AF is an overlong '/'; ED A0 80 is a UTF-16 surrogate.
        const QString s = d.decode("\xC0\xAF\xED\xA0\x80");
        QVERIFY(!s.contains(QLatin1Char('/')));
        QCOMPARE(s.count(QChar(0x2039)), 5);
    }

    void latin1AndCp1252()
    {
        TextDecoder d;
        QVERIFY(d.setEncoding(QStringLiteral("Latin-1")));
        QCOMPARE(d.decode("\xE9\x80"), QString(QChar(0xE9)) + QChar(0x80));
        QVERIFY(d.setEncoding(QStringLiteral("Windows-1252")));
        QCOMPARE(d.decode("\x80\x81"), QString(QChar(0x20AC)) + QStringLiteral("‹x81›"));
        QCOMPARE(d.encode(QString(QChar(0x20AC))), QByteArray("\x80"));
    }

    void unknownEncodingFallsBack()
    {
        TextDecoder d;
        QVERIFY(!d.setEncoding(QStringLiteral("Klingon-8")));
        QCOMPARE(d.encoding(), QStringLiteral("Auto"));
        QVERIFY(TextDecoder::available().startsWith(QStringLiteral("Auto")));
    }

    void escapeForLog()
    {
        QCOMPARE(TextDecoder::escape("a\tb\\c\r\n\x1b[0m\x7F"),
                 QStringLiteral("a\\tb\\\\c\\r\\n\\x1b[0m\\x7f"));
        // Valid UTF-8 stays readable; invalid bytes are spelled out.
        QCOMPARE(TextDecoder::escape("\xC3\xA9\xFF"), QString::fromUtf8("\xC3\xA9") + QStringLiteral("\\xff"));
        // C1 controls are valid UTF-8 but invisible.
        QCOMPARE(TextDecoder::escape("\xC2\x85"), QStringLiteral("\\xc2\\x85"));
        // The result never contains a raw tab or newline: lines.log stays one
        // record per line, four tab-separated fields.
        const QString e = TextDecoder::escape(QByteArray("\t\n\r", 3));
        QVERIFY(!e.contains(QLatin1Char('\t')) && !e.contains(QLatin1Char('\n')));
    }

    void encodeMatchesDecode()
    {
        TextDecoder d;
        const QString text = QString::fromUtf8("temp 25\xC2\xB0" "C");
        QCOMPARE(d.decode(d.encode(text)), text);
        d.setEncoding(QStringLiteral("Latin-1"));
        QCOMPARE(d.decode(d.encode(text)), text);
    }

    void displayModes()
    {
        DisplayOptions o;
        const QByteArray bytes("\x1b[31mERR\r", 9);
        const QString text = TextDecoder::decodeAuto(bytes);
        o.mode = QStringLiteral("ASCII");
        QCOMPARE(LineFormat::body(o, text, bytes), QStringLiteral("ERR"));
        o.mode = QStringLiteral("Escaped");
        QCOMPARE(LineFormat::body(o, text, bytes), QStringLiteral("\\x1b[31mERR\\r"));
        o.mode = QStringLiteral("HEX");
        QCOMPARE(LineFormat::body(o, text, bytes), QStringLiteral("1B 5B 33 31 6D 45 52 52 0D"));
    }
};

QTEST_GUILESS_MAIN(TestTextDecoder)
#include "tst_textdecoder.moc"
