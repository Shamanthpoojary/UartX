#include "logstore.h"
#include "textdecoder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>

namespace {

/// Records seen before the folder exists are kept up to this many; a session
/// that sends thousands of commands and never hears back has bigger problems
/// than its own TX history.
constexpr int MAX_EARLY_RECORDS = 1000;

QString isoTime(qint64 ms)
{
    if (ms <= 0)
        return QString();
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
    // Local time with its UTC offset: readable at a glance, unambiguous to a
    // program, and the same on every platform.
    const int off = dt.offsetFromUtc();
    const QChar sign = off < 0 ? QLatin1Char('-') : QLatin1Char('+');
    const int a = qAbs(off);
    return dt.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"))
           + QStringLiteral("%1%2:%3")
                 .arg(sign)
                 .arg(a / 3600, 2, 10, QLatin1Char('0'))
                 .arg((a % 3600) / 60, 2, 10, QLatin1Char('0'));
}

QString dirTag(const LineRecord &r)
{
    QString tag;
    switch (r.dir) {
    case Direction::Rx:  tag = QStringLiteral("RX");  break;
    case Direction::Tx:  tag = QStringLiteral("TX");  break;
    case Direction::Sys: tag = QStringLiteral("SYS"); break;
    }
    tag += QLatin1Char('/');
    tag += r.origin.isEmpty() ? QStringLiteral("device") : r.origin;

    QString flags;
    if (r.has(LineFlag::Partial))      flags += QLatin1Char('P');
    if (r.has(LineFlag::Continuation)) flags += QLatin1Char('C');
    if (r.has(LineFlag::Split))        flags += QLatin1Char('S');
    if (r.has(LineFlag::Typed))        flags += QLatin1Char('T');
    if (!flags.isEmpty())
        tag += QLatin1Char('/') + flags;
    return tag;
}

/// Keeps a user-supplied run name safe inside a directory name.
QString safeName(const QString &name)
{
    QString out;
    for (const QChar c : name.trimmed()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
            out += c;
        else if (c.isSpace() || c == QLatin1Char('.'))
            out += QLatin1Char('-');
    }
    return out.left(48);
}

} // namespace

LogStore::~LogStore()
{
    close();
}

QString LogStore::segmentFileName(int segment)
{
    return segment <= 1 ? QStringLiteral("lines.log")
                        : QStringLiteral("lines.%1.log").arg(segment);
}

QString LogStore::runDirName(int run, const QString &name)
{
    QString dir = QStringLiteral("run-%1").arg(run, 3, 10, QLatin1Char('0'));
    const QString safe = safeName(name);
    if (!safe.isEmpty())
        dir += QLatin1Char('_') + safe;
    return dir;
}

QByteArray LogStore::formatRecord(const LineRecord &r)
{
    // SYS messages are stored as UTF-8 text, so the same escaping suits all
    // three directions.
    const QString text = TextDecoder::escape(r.bytes);
    QByteArray out = QByteArray::number(r.seq);
    out += '\t';
    out += isoTime(r.wallMs).toUtf8();
    out += '\t';
    out += dirTag(r).toUtf8();
    out += '\t';
    out += text.toUtf8();
    out += '\n';
    return out;
}

void LogStore::open(const LogStoreOptions &options)
{
    close();
    m_opt = options;
    if (m_opt.indexEvery < 1)
        m_opt.indexEvery = 1;
    m_armed = true;
    m_created = false;
    m_stopped = false;
    m_stopReported = false;
    m_stopReason.clear();
    m_runs = { RunInfo{} };
    m_runs.last().dir = runDirName(1, QString());
    m_early.clear();
    m_linesWritten = 0;
    m_bytesWritten = 0;
    m_sinceIndex = 0;
}

QString LogStore::sessionDir() const
{
    return QDir(m_opt.baseDir).filePath(m_opt.folderName);
}

QString LogStore::runDir() const
{
    return m_runs.isEmpty() ? QString() : QDir(sessionDir()).filePath(m_runs.last().dir);
}

QString LogStore::rawPath() const
{
    return QDir(runDir()).filePath(QStringLiteral("raw.log"));
}

bool LogStore::takeStopEvent()
{
    if (!m_stopped || m_stopReported)
        return false;
    m_stopReported = true;
    return true;
}

void LogStore::stop(const QString &reason)
{
    if (m_stopped)
        return;
    m_stopped = true;
    m_stopReason = reason;
    // Whatever is buffered cannot be trusted to land either; let it go rather
    // than block the pipeline on a failing disk.
    delete m_raw;   m_raw = nullptr;
    delete m_lines; m_lines = nullptr;
    delete m_index; m_index = nullptr;
}

bool LogStore::write(QFile *file, const QByteArray &data)
{
    if (!file)
        return false;
    if (file->write(data) != data.size()) {
        stop(file->errorString());
        return false;
    }
    m_bytesWritten += data.size();
    return true;
}

bool LogStore::ensureCreated()
{
    if (m_created)
        return true;
    if (!m_armed || m_stopped)
        return false;

    // Two sessions on the same port in the same second would otherwise share
    // a folder; the second one gets a suffix instead.
    const QString base = m_opt.folderName;
    for (int n = 2; QFileInfo::exists(sessionDir()); ++n)
        m_opt.folderName = base + QStringLiteral("_%1").arg(n);

    if (!QDir().mkpath(sessionDir())) {
        stop(QStringLiteral("cannot create %1").arg(sessionDir()));
        return false;
    }
    m_created = true;
    m_createdMs = QDateTime::currentMSecsSinceEpoch();
    m_runs.last().startedMs = m_createdMs;
    if (!openRunFiles())
        return false;
    writeSessionJson();

    const QVector<LineRecord> early = std::move(m_early);
    m_early.clear();
    for (const LineRecord &r : early)
        appendRecord(r);
    return !m_stopped;
}

bool LogStore::openRunFiles()
{
    const QString dir = runDir();
    if (!QDir().mkpath(dir)) {
        stop(QStringLiteral("cannot create %1").arg(dir));
        return false;
    }

    m_raw = new QFile(QDir(dir).filePath(QStringLiteral("raw.log")));
    if (!m_raw->open(QIODevice::WriteOnly)) {
        stop(m_raw->errorString());
        return false;
    }
    m_index = new QFile(QDir(dir).filePath(QStringLiteral("lines.idx")));
    if (!m_index->open(QIODevice::WriteOnly)) {
        stop(m_index->errorString());
        return false;
    }
    m_sinceIndex = 0;
    return openSegment(1);
}

bool LogStore::openSegment(int segment)
{
    delete m_lines;
    m_lines = new QFile(QDir(runDir()).filePath(segmentFileName(segment)));
    if (!m_lines->open(QIODevice::WriteOnly)) {
        stop(m_lines->errorString());
        return false;
    }
    m_segment = segment;
    m_segmentSize = 0;
    // A segment always starts with an index entry, so a reader can land in
    // any segment without scanning the ones before it.
    m_sinceIndex = 0;
    return true;
}

void LogStore::closeRunFiles()
{
    for (QFile *f : { m_raw, m_lines, m_index }) {
        if (f) {
            f->flush();
            f->close();
        }
    }
    delete m_raw;   m_raw = nullptr;
    delete m_lines; m_lines = nullptr;
    delete m_index; m_index = nullptr;
}

void LogStore::appendRaw(const QByteArray &bytes)
{
    if (!m_armed || m_stopped || bytes.isEmpty())
        return;
    if (!ensureCreated())
        return;
    write(m_raw, bytes);
}

void LogStore::writeIndexEntry(const LineRecord &r)
{
    uchar buf[LogIndexEntry::DISK_SIZE] = {};
    qToLittleEndian<quint64>(r.seq, buf);
    qToLittleEndian<qint64>(r.wallMs, buf + 8);
    qToLittleEndian<quint32>(quint32(m_segment), buf + 16);
    // 4 reserved bytes at 20
    qToLittleEndian<quint64>(quint64(m_segmentSize), buf + 24);
    write(m_index, QByteArray(reinterpret_cast<const char *>(buf), sizeof buf));
}

void LogStore::appendRecord(const LineRecord &record)
{
    if (!m_armed || m_stopped)
        return;
    if (!m_created) {
        if (m_early.size() < MAX_EARLY_RECORDS)
            m_early.append(record);
        return;
    }

    const QByteArray line = formatRecord(record);
    if (m_segmentSize > 0 && m_segmentSize + line.size() > m_opt.segmentBytes) {
        m_lines->flush();
        if (!openSegment(m_segment + 1))
            return;
    }

    if (m_sinceIndex == 0)
        writeIndexEntry(record);
    m_sinceIndex = (m_sinceIndex + 1) % m_opt.indexEvery;

    if (!write(m_lines, line))
        return;
    m_segmentSize += line.size();
    ++m_linesWritten;

    RunInfo &run = m_runs.last();
    if (!run.hasLines) {
        run.firstSeq = record.seq;
        run.hasLines = true;
    }
    run.lastSeq = record.seq;
}

void LogStore::flush()
{
    if (m_stopped)
        return;
    for (QFile *f : { m_raw, m_lines, m_index }) {
        if (f && !f->flush()) {
            stop(f->errorString());
            return;
        }
    }
}

void LogStore::startNewRun(const QString &name)
{
    if (!m_armed || m_stopped)
        return;
    if (!m_created) {
        // Nothing on disk yet: the pending run simply takes the new name.
        m_runs.last().name = name;
        m_runs.last().dir = runDirName(m_runs.last().number, name);
        return;
    }
    m_runs.last().endedMs = QDateTime::currentMSecsSinceEpoch();
    closeRunFiles();

    RunInfo next;
    next.number = m_runs.last().number + 1;
    next.name = name;
    next.dir = runDirName(next.number, name);
    next.startedMs = QDateTime::currentMSecsSinceEpoch();
    m_runs.append(next);
    if (openRunFiles())
        writeSessionJson();
}

void LogStore::close()
{
    if (!m_armed)
        return;
    if (m_created && !m_stopped) {
        m_runs.last().endedMs = QDateTime::currentMSecsSinceEpoch();
        closeRunFiles();
        writeSessionJson();
    } else {
        closeRunFiles();
    }
    m_early.clear();
    m_armed = false;
}

void LogStore::writeSessionJson()
{
    if (!m_created || m_stopped)
        return;

    QJsonArray runs;
    for (const RunInfo &r : std::as_const(m_runs)) {
        QJsonObject o;
        o["id"]        = r.number;
        o["name"]      = r.name;
        o["dir"]       = r.dir;
        o["started"]   = isoTime(r.startedMs);
        o["startedMs"] = double(r.startedMs);
        o["ended"]     = r.endedMs ? QJsonValue(isoTime(r.endedMs)) : QJsonValue();
        o["endedMs"]   = r.endedMs ? QJsonValue(double(r.endedMs)) : QJsonValue();
        o["firstSeq"]  = r.hasLines ? QJsonValue(double(r.firstSeq)) : QJsonValue();
        o["lastSeq"]   = r.hasLines ? QJsonValue(double(r.lastSeq)) : QJsonValue();
        o["pinned"]    = false;
        runs.append(o);
    }

    QJsonObject doc;
    // The marker retention looks for: a folder without it was not made by
    // UartX and is never touched.
    doc["format"]    = QStringLiteral("uartx-session");
    doc["version"]   = 1;
    doc["id"]        = m_opt.sessionId;
    doc["port"]      = m_opt.port;
    doc["created"]   = isoTime(m_createdMs);
    doc["settings"]  = m_opt.settings;
    doc["runs"]      = runs;

    QSaveFile file(QDir(sessionDir()).filePath(QStringLiteral("session.json")));
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(doc).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        stop(file.errorString());
    }
}

// --------------------------------------------------------------- LogReader

namespace LogReader {

QVector<LogIndexEntry> readIndex(const QString &runDir)
{
    QVector<LogIndexEntry> out;
    QFile f(QDir(runDir).filePath(QStringLiteral("lines.idx")));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray all = f.readAll();
    const auto *p = reinterpret_cast<const uchar *>(all.constData());
    const int count = all.size() / LogIndexEntry::DISK_SIZE;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const uchar *e = p + i * LogIndexEntry::DISK_SIZE;
        LogIndexEntry entry;
        entry.seq     = qFromLittleEndian<quint64>(e);
        entry.wallMs  = qFromLittleEndian<qint64>(e + 8);
        entry.segment = qFromLittleEndian<quint32>(e + 16);
        entry.offset  = qFromLittleEndian<quint64>(e + 24);
        out.append(entry);
    }
    return out;
}

QVector<QByteArray> readLines(const QString &runDir, quint64 fromSeq, int maxCount)
{
    QVector<QByteArray> out;
    if (maxCount <= 0)
        return out;

    const QVector<LogIndexEntry> index = readIndex(runDir);
    int segment = 1;
    qint64 offset = 0;
    // The last index entry at or before fromSeq is where to start reading.
    auto it = std::upper_bound(index.begin(), index.end(), fromSeq,
                               [](quint64 seq, const LogIndexEntry &e) { return seq < e.seq; });
    if (it != index.begin()) {
        --it;
        segment = int(it->segment);
        offset = qint64(it->offset);
    }

    while (out.size() < maxCount) {
        QFile f(QDir(runDir).filePath(LogStore::segmentFileName(segment)));
        if (!f.open(QIODevice::ReadOnly))
            break;
        if (!f.seek(offset))
            break;
        while (out.size() < maxCount) {
            QByteArray line = f.readLine();
            if (line.isEmpty())
                break;
            if (line.endsWith('\n'))
                line.chop(1);
            const int tab = line.indexOf('\t');
            const quint64 seq = line.left(tab).toULongLong();
            if (seq >= fromSeq)
                out.append(line);
        }
        ++segment;
        offset = 0;
    }
    return out;
}

} // namespace LogReader
