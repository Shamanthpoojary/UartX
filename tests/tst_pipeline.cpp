#include "pipeline.h"

#include <QtTest>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>

/// Drives the real pipeline on its own thread, the way a session does, and
/// checks the promise the design rests on: whatever the display does, the
/// log and the ring keep every line, in order, with consecutive seq numbers.
class TestPipeline : public QObject
{
    Q_OBJECT

private:
    struct Run
    {
        QVector<DisplayBatchPtr> batches;
        QString runDir;
        QByteArray wire;
        int lines = 0;
        qint64 elapsedMs = 0;
        QVector<LineRecord> tail;
    };

    /// Pushes `lineCount` lines through a pipeline in random-sized chunks, as
    /// fast as the queue accepts them -- far faster than any baud rate --
    /// with a GUI that acknowledges batches only if `ack` is set.
    Run pump(int lineCount, bool ack, const QString &logDir)
    {
        Run run;
        QThread thread;
        auto *pipe = new Pipeline(50000);
        pipe->moveToThread(&thread);
        connect(pipe, &Pipeline::batchReady, this, [&run, pipe, ack](DisplayBatchPtr b) {
            run.batches.append(b);
            if (ack)
                pipe->markBatchRendered();
        });
        thread.start();

        PipelineConfig cfg;
        cfg.logEnabled = true;
        cfg.log.baseDir = logDir;
        cfg.log.folderName = QStringLiteral("stress_s1");
        cfg.log.sessionId = QStringLiteral("s1");
        cfg.log.port = QStringLiteral("loopback");
        cfg.maxPendingDisplay = 8000;
        cfg.pollIntervalMs = 10;
        QMetaObject::invokeMethod(pipe, "start", Qt::QueuedConnection, Q_ARG(PipelineConfig, cfg));

        QByteArray wire;
        for (int i = 1; i <= lineCount; ++i) {
            wire += "I (";
            wire += QByteArray::number(i);
            wire += ") app: sensor=0x3c value=";
            wire += QByteArray::number(i * 7 % 1000);
            wire += (i % 3 == 0) ? "\n" : "\r\n";
        }
        run.wire = wire;
        run.lines = lineCount;

        QElapsedTimer timer;
        timer.start();
        auto *rng = QRandomGenerator::global();
        qint64 mono = 0;
        for (int pos = 0; pos < wire.size();) {
            const int n = qMin(int(rng->bounded(1, 4096)), int(wire.size()) - pos);
            mono += 1000000;    // 1 ms apart: never quiet long enough to flush a partial
            QMetaObject::invokeMethod(pipe, "onData", Qt::QueuedConnection,
                                      Q_ARG(QByteArray, wire.mid(pos, n)),
                                      Q_ARG(qint64, QDateTime::currentMSecsSinceEpoch()),
                                      Q_ARG(qint64, mono));
            pos += n;
            if ((pos & 0xFFFF) < n)
                QCoreApplication::processEvents();   // let the "GUI" draw now and then
        }
        QMetaObject::invokeMethod(pipe, "finish", Qt::BlockingQueuedConnection);
        run.elapsedMs = timer.elapsed();
        thread.quit();
        thread.wait();
        QCoreApplication::processEvents();

        run.tail = pipe->recent().tail(3);
        run.runDir = QDir(logDir).filePath(QStringLiteral("stress_s1/run-001"));
        delete pipe;
        return run;
    }

private slots:
    void noLineLostAndOrderKept()
    {
        QTemporaryDir tmp;
        const int N = 200000;
        const Run run = pump(N, true, tmp.path());

        QFile raw(run.runDir + QStringLiteral("/raw.log"));
        QVERIFY(raw.open(QIODevice::ReadOnly));
        QCOMPARE(raw.readAll(), run.wire);

        QFile lines(run.runDir + QStringLiteral("/lines.log"));
        QVERIFY(lines.open(QIODevice::ReadOnly));
        int count = 0;
        while (!lines.atEnd()) {
            const QByteArray line = lines.readLine();
            const QList<QByteArray> f = line.split('\t');
            QCOMPARE(f.size(), 4);
            ++count;
            QCOMPARE(f[0].toInt(), count);                     // consecutive seq
            QCOMPARE(f[2], QByteArray("RX/device"));
            QVERIFY(f[3].startsWith("I (" + QByteArray::number(count) + ")"));
        }
        QCOMPARE(count, N);
        QCOMPARE(run.tail.last().seq, quint64(N));

        const double mb = run.wire.size() / 1048576.0;
        qInfo("pipeline: %d lines, %.1f MB in %lld ms (%.1f MB/s; 921600 baud is 0.09 MB/s)",
              N, mb, run.elapsedMs, mb * 1000.0 / qMax<qint64>(1, run.elapsedMs));
    }

    void displayCoalescesButAccountsForEveryLine()
    {
        // A GUI that never acknowledges: the display must coalesce instead of
        // queueing without bound, and say how many lines it skipped.
        QTemporaryDir tmp;
        const int N = 50000;
        const Run run = pump(N, false, tmp.path());

        quint64 shown = 0;
        quint64 skipped = 0;
        const QRegularExpression gap(QStringLiteral("(\\d+) lines not displayed"));
        for (const DisplayBatchPtr &b : run.batches) {
            for (const DisplayItem &item : b->items) {
                if (item.kind == DisplayItem::Kind::Line)
                    ++shown;
                else if (item.kind == DisplayItem::Kind::Gap)
                    skipped += gap.match(item.line.body).captured(1).toULongLong();
            }
        }
        QVERIFY(skipped > 0);
        QCOMPARE(shown + skipped, quint64(N));
        QVERIFY(shown <= 8000 + 8000 / 4 + 1);
    }

    void partialLineReachesFiltersOnce()
    {
        PipelineConfig cfg;
        cfg.framing.quietFlushMs = 50;
        cfg.pollIntervalMs = 10;
        QThread thread;
        auto *pipe = new Pipeline;
        pipe->moveToThread(&thread);
        QVector<DisplayItem> items;
        connect(pipe, &Pipeline::batchReady, this, [&items, pipe](DisplayBatchPtr b) {
            items += b->items;
            pipe->markBatchRendered();
        });
        thread.start();
        QMetaObject::invokeMethod(pipe, "start", Qt::QueuedConnection, Q_ARG(PipelineConfig, cfg));

        const qint64 now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch()).count();
        QMetaObject::invokeMethod(pipe, "onData", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, QByteArray("login: ")),
                                  Q_ARG(qint64, QDateTime::currentMSecsSinceEpoch()),
                                  Q_ARG(qint64, now));
        QTRY_VERIFY_WITH_TIMEOUT(!items.isEmpty(), 2000);   // the quiet timer showed it
        QCOMPARE(items[0].newline, false);
        QCOMPARE(items[0].wholeValid, false);                 // filters wait for the end

        QMetaObject::invokeMethod(pipe, "onData", Qt::QueuedConnection,
                                  Q_ARG(QByteArray, QByteArray("root\n")),
                                  Q_ARG(qint64, QDateTime::currentMSecsSinceEpoch()),
                                  Q_ARG(qint64, now + 1000000000LL));
        QTRY_COMPARE_WITH_TIMEOUT(items.size(), 2, 2000);
        QCOMPARE(items[1].line.body, QStringLiteral("root"));
        QVERIFY(items[1].wholeValid);
        QCOMPARE(items[1].whole.raw, QStringLiteral("login: root"));

        QMetaObject::invokeMethod(pipe, "finish", Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
        delete pipe;
    }
};

QTEST_GUILESS_MAIN(TestPipeline)
#include "tst_pipeline.moc"
