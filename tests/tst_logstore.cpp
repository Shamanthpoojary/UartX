#include "logretention.h"
#include "logstore.h"

#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

LineRecord rx(quint64 seq, const QByteArray &bytes, quint8 flags = 0)
{
    LineRecord r;
    r.seq = seq;
    r.wallMs = 1700000000000LL + qint64(seq);
    r.dir = Direction::Rx;
    r.origin = Origin::Device;
    r.flags = flags;
    r.bytes = bytes;
    return r;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QJsonObject readJson(const QString &path)
{
    return QJsonDocument::fromJson(readAll(path)).object();
}

/// A finished session folder as UartX would leave it, with one run of
/// `bytes` bytes that ended `ageSecs` ago.
QString makeSession(const QString &base, const QString &name, qint64 ageSecs, int bytes,
                    bool pinned = false, bool open = false)
{
    const QString dir = QDir(base).filePath(name);
    QDir().mkpath(dir + QStringLiteral("/run-001"));
    QFile raw(dir + QStringLiteral("/run-001/raw.log"));
    raw.open(QIODevice::WriteOnly);
    raw.write(QByteArray(bytes, 'x'));
    raw.close();

    const qint64 ended = QDateTime::currentMSecsSinceEpoch() - ageSecs * 1000;
    QJsonObject run;
    run["id"] = 1;
    run["dir"] = QStringLiteral("run-001");
    run["pinned"] = pinned;
    run["endedMs"] = open ? QJsonValue() : QJsonValue(double(ended));
    QJsonObject doc;
    doc["format"] = QStringLiteral("uartx-session");
    doc["runs"] = QJsonArray{ run };
    QFile f(dir + QStringLiteral("/session.json"));
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(doc).toJson());
    return dir;
}

} // namespace

class TestLogStore : public QObject
{
    Q_OBJECT

private slots:
    void nothingOnDiskWithoutData()
    {
        QTemporaryDir tmp;
        LogStore log;
        log.open({ tmp.path(), QStringLiteral("sess_s1"), QStringLiteral("s1"),
                   QStringLiteral("COM5"), {} });
        LineRecord sys;
        sys.seq = 1;
        sys.dir = Direction::Sys;
        sys.bytes = "connected";
        log.appendRecord(sys);
        log.close();
        QVERIFY(!QFileInfo::exists(QDir(tmp.path()).filePath(QStringLiteral("sess_s1"))));
    }

    void rawIsByteExactAndLinesAreEscaped()
    {
        QTemporaryDir tmp;
        LogStore log;
        log.open({ tmp.path(), QStringLiteral("sess_s1"), QStringLiteral("s1"),
                   QStringLiteral("COM5"), {} });

        LineRecord sys;
        sys.seq = 1;
        sys.dir = Direction::Sys;
        sys.origin = Origin::System;
        sys.bytes = "connected";
        log.appendRecord(sys);          // held until the folder exists

        const QByteArray wire("boot\r\n\x1b[31mERR\xFF\r\n", 16);
        log.appendRaw(wire);
        log.appendRecord(rx(2, "boot"));
        log.appendRecord(rx(3, "\x1b[31mERR\xFF", LineFlag::Split));
        const QString run = log.runDir();
        log.close();

        QCOMPARE(readAll(run + QStringLiteral("/raw.log")), wire);

        const QList<QByteArray> lines = readAll(run + QStringLiteral("/lines.log")).split('\n');
        QCOMPARE(lines.size(), 4);              // three records and the final newline
        QVERIFY(lines[0].startsWith("1\t"));
        QVERIFY(lines[0].endsWith("\tSYS/system\tconnected"));
        QVERIFY(lines[1].endsWith("\tRX/device\tboot"));
        QVERIFY(lines[2].endsWith("\tRX/device/S\t\\x1b[31mERR\\xff"));
        QCOMPARE(lines[2].split('\t').size(), 4);

        const QJsonObject doc = readJson(QDir(run).filePath(QStringLiteral("../session.json")));
        QCOMPARE(doc.value("format").toString(), QStringLiteral("uartx-session"));
        const QJsonObject r = doc.value("runs").toArray().at(0).toObject();
        QCOMPARE(r.value("firstSeq").toInt(), 1);
        QCOMPARE(r.value("lastSeq").toInt(), 3);
        QVERIFY(r.value("endedMs").toDouble() > 0);
    }

    void indexRotationAndRead()
    {
        QTemporaryDir tmp;
        LogStore log;
        LogStoreOptions o{ tmp.path(), QStringLiteral("s"), QStringLiteral("s1"),
                           QStringLiteral("COM5"), {} };
        o.segmentBytes = 4096;
        o.indexEvery = 10;
        log.open(o);
        log.appendRaw("x");
        for (quint64 seq = 1; seq <= 1000; ++seq)
            log.appendRecord(rx(seq, QByteArray("line number ") + QByteArray::number(seq)));
        const QString run = log.runDir();
        log.close();

        QVERIFY(QFileInfo::exists(run + QStringLiteral("/lines.2.log")));
        const QVector<LogIndexEntry> index = LogReader::readIndex(run);
        QVERIFY(index.size() >= 100);
        QCOMPARE(index.first().seq, quint64(1));
        for (int i = 1; i < index.size(); ++i)
            QVERIFY(index[i].seq > index[i - 1].seq);

        // Reads that start mid-block, at a segment edge, and run across
        // segments all return exactly the lines asked for.
        for (quint64 from : { quint64(1), quint64(437), quint64(999) }) {
            const QVector<QByteArray> got = LogReader::readLines(run, from, 50);
            QCOMPARE(got.size(), int(qMin<quint64>(50, 1001 - from)));
            for (int i = 0; i < got.size(); ++i)
                QCOMPARE(got[i].left(got[i].indexOf('\t')).toULongLong(), from + quint64(i));
        }
    }

    void newRunKeepsTheOldOne()
    {
        QTemporaryDir tmp;
        LogStore log;
        log.open({ tmp.path(), QStringLiteral("s"), QStringLiteral("s1"),
                   QStringLiteral("COM5"), {} });
        log.appendRaw("a\n");
        log.appendRecord(rx(1, "a"));
        const QString first = log.runDir();
        log.startNewRun(QStringLiteral("verify I2C fix"));
        log.appendRaw("b\n");
        log.appendRecord(rx(2, "b"));
        const QString second = log.runDir();
        log.close();

        QVERIFY(first != second);
        QVERIFY(second.endsWith(QStringLiteral("run-002_verify-I2C-fix")));
        QCOMPARE(readAll(first + QStringLiteral("/raw.log")), QByteArray("a\n"));
        QCOMPARE(readAll(second + QStringLiteral("/raw.log")), QByteArray("b\n"));
    }

    void retentionByAge()
    {
        QTemporaryDir tmp;
        const QString old = makeSession(tmp.path(), QStringLiteral("old"), 13 * 3600, 10);
        const QString fresh = makeSession(tmp.path(), QStringLiteral("fresh"), 3600, 10);
        RetentionPolicy p;
        p.maxAgeSecs = 12 * 3600;
        p.maxBytes = 0;
        const RetentionReport r = applyRetention(tmp.path(), p, {}, QDateTime::currentDateTime());
        QCOMPARE(r.deletedRuns.size(), 1);
        QVERIFY(!QFileInfo::exists(old));            // empty session folder goes too
        QVERIFY(QFileInfo::exists(fresh + QStringLiteral("/run-001")));
    }

    void retentionBySizeOldestFirst()
    {
        QTemporaryDir tmp;
        const QString a = makeSession(tmp.path(), QStringLiteral("a"), 300, 600);
        const QString b = makeSession(tmp.path(), QStringLiteral("b"), 200, 600);
        const QString c = makeSession(tmp.path(), QStringLiteral("c"), 100, 600);
        RetentionPolicy p;
        p.maxAgeSecs = 0;
        p.maxBytes = 1300;
        applyRetention(tmp.path(), p, {}, QDateTime::currentDateTime());
        QVERIFY(!QFileInfo::exists(a));
        QVERIFY(QFileInfo::exists(b));
        QVERIFY(QFileInfo::exists(c));
    }

    void retentionNeverTouchesProtectedOrForeign()
    {
        QTemporaryDir tmp;
        const QString pinned = makeSession(tmp.path(), QStringLiteral("pinned"), 99 * 3600, 10, true);
        const QString active = makeSession(tmp.path(), QStringLiteral("active"), 0, 5000, false, true);

        // A v1.3 log and an unrelated folder in the same place.
        QFile legacy(QDir(tmp.path()).filePath(QStringLiteral("uartx_2026-01-01_COM5_120000.log")));
        legacy.open(QIODevice::WriteOnly);
        legacy.write(QByteArray(100000, 'x'));
        legacy.close();
        QDir().mkpath(QDir(tmp.path()).filePath(QStringLiteral("notes")));

        RetentionPolicy p;
        p.maxAgeSecs = 3600;
        p.maxBytes = 1000;
        const QSet<QString> activeDirs{ QDir::cleanPath(active + QStringLiteral("/run-001")) };
        const RetentionReport r = applyRetention(tmp.path(), p, activeDirs, QDateTime::currentDateTime());

        QVERIFY(r.deletedRuns.isEmpty());
        QVERIFY(r.activeOverCap);
        QVERIFY(QFileInfo::exists(pinned + QStringLiteral("/run-001")));
        QVERIFY(QFileInfo::exists(active + QStringLiteral("/run-001")));
        QVERIFY(legacy.exists());
        QVERIFY(QFileInfo::exists(QDir(tmp.path()).filePath(QStringLiteral("notes"))));
    }

    void unfinishedRecentRunIsTreatedAsLive()
    {
        // Not in the active set -- another UartX instance, say -- but written
        // to moments ago and never closed: the size cap must leave it alone.
        QTemporaryDir tmp;
        const QString other = makeSession(tmp.path(), QStringLiteral("other"), 0, 5000, false, true);
        RetentionPolicy p;
        p.maxAgeSecs = 0;
        p.maxBytes = 1000;
        const RetentionReport r = applyRetention(tmp.path(), p, {}, QDateTime::currentDateTime());
        QVERIFY(r.deletedRuns.isEmpty());
        QVERIFY(QFileInfo::exists(other + QStringLiteral("/run-001")));
    }

    void policyChoicesRoundTrip()
    {
        for (const QString &c : RetentionPolicy::ageChoices())
            QCOMPARE(RetentionPolicy::choiceForAge(RetentionPolicy::ageFromChoice(c)), c);
        for (const QString &c : RetentionPolicy::sizeChoices())
            QCOMPARE(RetentionPolicy::choiceForSize(RetentionPolicy::sizeFromChoice(c)), c);
        RetentionPolicy p;
        QCOMPARE(p.summary(), QStringLiteral("keep 12 hours / 1 GB"));
    }
};

QTEST_GUILESS_MAIN(TestLogStore)
#include "tst_logstore.moc"
