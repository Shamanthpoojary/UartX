#ifndef LOGSTORE_H
#define LOGSTORE_H

#include "linerecord.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

class QFile;

/// One entry of lines.idx: where the line with `seq` starts.
///
/// The index is sparse -- one entry every LogStoreOptions::indexEvery lines,
/// plus one at the start of every segment -- so it stays a few kilobytes for
/// a multi-gigabyte log, and a viewer can find any line with one binary
/// search and one seek.
struct LogIndexEntry
{
    quint64 seq     = 0;
    qint64  wallMs  = 0;
    quint32 segment = 1;    ///< 1 = lines.log, 2 = lines.2.log, ...
    quint64 offset  = 0;    ///< byte offset of the line within its segment

    static constexpr int DISK_SIZE = 32;
};

struct LogStoreOptions
{
    QString     baseDir;                        ///< the user's log folder
    QString     folderName;                     ///< session folder, already expanded
    QString     sessionId;                      ///< "s1"
    QString     port;
    QJsonObject settings;                       ///< recorded in session.json
    qint64      segmentBytes = 256LL * 1024 * 1024;
    int         indexEvery   = 1024;
};

/// Everything a session writes to disk.
///
///   <baseDir>/<folderName>/
///     session.json          what was opened, with which settings, and its runs
///     run-001/
///       raw.log             the exact bytes received -- nothing added, nothing changed
///       lines.log           seq <TAB> time <TAB> dir/origin[/flags] <TAB> escaped text
///       lines.2.log ...     further segments once lines.log reaches segmentBytes
///       lines.idx           sparse index into the segments
///
/// Nothing is created until the first byte arrives, so a session that
/// receives nothing leaves nothing behind -- the same promise v1.3 made.
/// Records that come before that first byte (the "connected" marker, say) are
/// held in memory and written once the folder exists.
///
/// A write failure -- disk full, folder removed, permission revoked -- stops
/// all writing for the rest of the session and is reported once through
/// takeStopEvent(); the session itself carries on.
///
/// Not thread-safe: owned and used by one pipeline thread.
class LogStore
{
public:
    LogStore() = default;
    ~LogStore();
    LogStore(const LogStore &) = delete;
    LogStore &operator=(const LogStore &) = delete;

    /// Arms the store. Checks nothing on disk yet; the caller has already
    /// validated the folder.
    void open(const LogStoreOptions &options);

    void appendRaw(const QByteArray &bytes);
    void appendRecord(const LineRecord &record);
    void flush();

    /// Ends the current run and starts run-NNN+1 with fresh files. Old runs
    /// stay where they are.
    void startNewRun(const QString &name = QString());

    /// Closes the run and records its end in session.json.
    void close();

    bool    isArmed()    const { return m_armed; }
    bool    hasCreated() const { return m_created; }
    bool    isStopped()  const { return m_stopped; }
    QString stopReason() const { return m_stopReason; }

    /// True exactly once after writing stopped, so the stop is reported once.
    bool takeStopEvent();

    QString sessionDir() const;
    QString runDir() const;
    QString rawPath() const;
    quint64 linesWritten() const { return m_linesWritten; }
    qint64  bytesWritten() const { return m_bytesWritten; }

    /// One record as it appears in lines.log, newline included.
    static QByteArray formatRecord(const LineRecord &record);
    static QString    segmentFileName(int segment);
    static QString    runDirName(int run, const QString &name);

private:
    bool ensureCreated();
    bool openRunFiles();
    bool openSegment(int segment);
    void closeRunFiles();
    void writeSessionJson();
    bool write(QFile *file, const QByteArray &data);
    void stop(const QString &reason);
    void writeIndexEntry(const LineRecord &record);

    struct RunInfo
    {
        int     number = 1;
        QString name;
        QString dir;
        qint64  startedMs = 0;
        qint64  endedMs   = 0;    ///< 0 while open
        quint64 firstSeq  = 0;
        quint64 lastSeq   = 0;
        bool    hasLines  = false;
    };

    LogStoreOptions  m_opt;
    bool             m_armed   = false;
    bool             m_created = false;
    bool             m_stopped = false;
    bool             m_stopReported = false;
    QString          m_stopReason;
    qint64           m_createdMs = 0;

    QVector<RunInfo> m_runs;
    QFile           *m_raw   = nullptr;
    QFile           *m_lines = nullptr;
    QFile           *m_index = nullptr;
    int              m_segment = 1;
    qint64           m_segmentSize = 0;
    int              m_sinceIndex = 0;

    QVector<LineRecord> m_early;    ///< records before the folder exists
    quint64          m_linesWritten = 0;
    qint64           m_bytesWritten = 0;
};

/// Read access to a run's lines.log and lines.idx. Used by the full-log viewer
/// and the tests; cheap enough to construct per query.
namespace LogReader {

QVector<LogIndexEntry> readIndex(const QString &runDir);

/// Up to `maxCount` lines starting at `fromSeq`, as written (newline
/// stripped). Uses the index to seek, so the cost does not grow with the log.
QVector<QByteArray> readLines(const QString &runDir, quint64 fromSeq, int maxCount);

} // namespace LogReader

#endif // LOGSTORE_H
