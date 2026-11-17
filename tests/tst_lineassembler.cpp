#include "lineassembler.h"

#include <QtTest>

namespace {

QList<QByteArray> bytesOf(const QVector<AssembledLine> &lines)
{
    QList<QByteArray> out;
    for (const AssembledLine &l : lines)
        out.append(l.bytes);
    return out;
}

} // namespace

class TestLineAssembler : public QObject
{
    Q_OBJECT

private slots:
    void autoEndings()
    {
        LineAssembler a;
        const auto lines = a.feed("one\rtwo\nthree\r\nfour\n\rfive\n", 0);
        QCOMPARE(bytesOf(lines), (QList<QByteArray>{ "one", "two", "three", "four", "five" }));
        for (const AssembledLine &l : lines)
            QCOMPARE(l.flags, quint8(0));
    }

    void emptyLinesSurvive()
    {
        LineAssembler a;
        QCOMPARE(bytesOf(a.feed("a\n\nb\n", 0)), (QList<QByteArray>{ "a", "", "b" }));
    }

    void crlfSplitAcrossChunks()
    {
        LineAssembler a;
        QCOMPARE(bytesOf(a.feed("abc\r", 0)), (QList<QByteArray>{ "abc" }));
        // The LF completing the pair arrives in the next read: no empty line.
        QCOMPARE(bytesOf(a.feed("\ndef\r\n", 1)), (QList<QByteArray>{ "def" }));
    }

    void utf8SplitAcrossChunksStaysWhole()
    {
        // "é" is C3 A9; a read boundary between the two bytes must not break
        // the character, because lines are bytes until they are decoded.
        LineAssembler a;
        QVERIFY(a.feed("caf\xC3", 0).isEmpty());
        const auto lines = a.feed("\xA9\n", 1);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0].bytes, QByteArray("caf\xC3\xA9"));
    }

    void binaryBytesAreKept()
    {
        LineAssembler a;
        const QByteArray data("\x00\x01\xFF\x1B[31mX\n", 10);
        const auto lines = a.feed(data, 0);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0].bytes, data.left(9));
    }

    void quietFlushThenContinuation()
    {
        Framing f;
        f.quietFlushMs = 250;
        LineAssembler a(f);
        QVERIFY(a.feed("login: ", 0).isEmpty());
        QVERIFY(a.poll(100).isEmpty());           // not quiet long enough

        auto partial = a.poll(300);
        QCOMPARE(partial.size(), 1);
        QCOMPARE(partial[0].bytes, QByteArray("login: "));
        QCOMPARE(partial[0].flags, quint8(LineFlag::Partial));
        QVERIFY(a.poll(600).isEmpty());           // flushed once, not again

        auto rest = a.feed("root\n", 700);
        QCOMPARE(rest.size(), 1);
        QCOMPARE(rest[0].bytes, QByteArray("root"));
        QCOMPARE(rest[0].whole, QByteArray("login: root"));
        QCOMPARE(rest[0].flags, quint8(LineFlag::Continuation));
    }

    void secondPartialIsAlsoContinuation()
    {
        LineAssembler a;
        a.feed("a", 0);
        a.poll(1000);
        a.feed("b", 1001);
        auto second = a.poll(2000);
        QCOMPARE(second.size(), 1);
        QCOMPARE(second[0].bytes, QByteArray("b"));
        QCOMPARE(second[0].flags, quint8(LineFlag::Partial | LineFlag::Continuation));
        auto end = a.feed("\n", 2001);
        QCOMPARE(end[0].bytes, QByteArray());
        QCOMPARE(end[0].whole, QByteArray("ab"));
    }

    void forcedSplitAtMaxLength()
    {
        Framing f;
        f.maxLineBytes = 16;
        LineAssembler a(f);
        const auto lines = a.feed(QByteArray(40, 'x') + "\n", 0);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].bytes.size(), 16);
        QCOMPARE(lines[0].flags, quint8(LineFlag::Split));
        QCOMPARE(lines[1].bytes.size(), 16);
        QCOMPARE(lines[2].bytes.size(), 8);
        QCOMPARE(lines[2].flags, quint8(0));
    }

    void customTerminatorAcrossChunks()
    {
        Framing f;
        f.mode = Framing::Mode::Terminator;
        f.terminator = Framing::parseTerminator(QStringLiteral("\\x03;"));
        QCOMPARE(f.terminator, QByteArray("\x03;"));
        LineAssembler a(f);
        QVERIFY(a.feed("AB\x03", 0).isEmpty());
        QCOMPARE(bytesOf(a.feed(";CD\x03;\n", 1)), (QList<QByteArray>{ "AB", "CD" }));
    }

    void idleGapFraming()
    {
        Framing f;
        f.mode = Framing::Mode::IdleGap;
        f.idleGapMs = 20;
        LineAssembler a(f);
        QVERIFY(a.feed("\xAA\x55\x01", 0).isEmpty());
        QVERIFY(a.feed("\x02", 5).isEmpty());                 // same packet
        auto lines = a.feed("\xAA\x55", 50);                  // gap ended the first
        QCOMPARE(bytesOf(lines), (QList<QByteArray>{ QByteArray("\xAA\x55\x01\x02") }));
        lines = a.poll(80);                                   // gap ends the second
        QCOMPARE(bytesOf(lines), (QList<QByteArray>{ QByteArray("\xAA\x55") }));
        QCOMPARE(lines[0].flags, quint8(0));
    }

    void fixedLengthFraming()
    {
        Framing f;
        f.mode = Framing::Mode::FixedLength;
        f.fixedLength = 4;
        LineAssembler a(f);
        QCOMPARE(bytesOf(a.feed("12345678\n9", 0)),
                 (QList<QByteArray>{ "1234", "5678" }));
    }

    void noneFramingOnlyQuietFlush()
    {
        Framing f;
        f.mode = Framing::Mode::None;
        LineAssembler a(f);
        QVERIFY(a.feed("a\nb\r", 0).isEmpty());
        const auto lines = a.poll(1000);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0].bytes, QByteArray("a\nb\r"));
    }

    void drainReturnsTheOpenLine()
    {
        LineAssembler a;
        a.feed("tail", 0);
        const auto rest = a.drain();
        QCOMPARE(rest.size(), 1);
        QCOMPARE(rest[0].bytes, QByteArray("tail"));
        QVERIFY(!a.hasPending());
    }

    void badTerminatorFallsBackToAuto()
    {
        QVERIFY(Framing::parseTerminator(QStringLiteral("\\q")).isEmpty());
        Framing f;
        f.mode = Framing::Mode::Terminator;
        LineAssembler a;
        a.setFraming(f);
        QCOMPARE(a.framing().mode, Framing::Mode::Auto);
    }
};

QTEST_GUILESS_MAIN(TestLineAssembler)
#include "tst_lineassembler.moc"
