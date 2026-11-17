#include "linequery.h"
#include "recentring.h"

#include <QtTest>

class TestLineQuery : public QObject
{
    Q_OBJECT

private slots:
    void literalIsCaseInsensitiveByDefault()
    {
        const LineQuery q(QStringLiteral("ble hci"), false, false);
        QVERIFY(q.matches(QStringLiteral("BLE HCI command_complete_count=1")));
        QVERIFY(!q.matches(QStringLiteral("BLE_HOST progress")));
        QVERIFY(!LineQuery(QStringLiteral("ble"), false, true).matches(QStringLiteral("BLE")));
    }

    void regexAndErrors()
    {
        const LineQuery q(QStringLiteral("status=[1-9]"), true, false);
        QVERIFY(q.isValid());
        QVERIFY(q.matches(QStringLiteral("last_opcode=3110 status=12")));
        QVERIFY(!q.matches(QStringLiteral("last_opcode=3110 status=0")));
        QCOMPARE(q.describe(), QStringLiteral("/status=[1-9]/"));

        const LineQuery bad(QStringLiteral("status=[1-"), true, false);
        QVERIFY(!bad.isValid());
        QVERIFY(!bad.error().isEmpty());
        QVERIFY(!bad.matches(QStringLiteral("anything")));

        QVERIFY(!LineQuery(QString(600, QLatin1Char('a')), false, false).isValid());
    }

    void emptyMatchesEverything()
    {
        QVERIFY(LineQuery().matches(QStringLiteral("x")));
    }

    void ringSinceUsesSeqArithmetic()
    {
        RecentRing ring(100);
        for (quint64 seq = 1; seq <= 250; ++seq) {
            LineRecord r;
            r.seq = seq;
            ring.append(r);
        }
        QCOMPARE(ring.firstSeq(), quint64(151));
        QCOMPARE(ring.lastSeq(), quint64(250));
        QCOMPARE(ring.since(0, 1000).size(), 100);
        QCOMPARE(ring.since(200, 1000).first().seq, quint64(200));
        QCOMPARE(ring.since(200, 5).last().seq, quint64(204));
        QVERIFY(ring.since(251, 10).isEmpty());
        QCOMPARE(ring.tail(3).first().seq, quint64(248));
    }
};

QTEST_GUILESS_MAIN(TestLineQuery)
#include "tst_linequery.moc"
