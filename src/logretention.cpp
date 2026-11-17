#include "logretention.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace {

struct Choice { const char *label; qint64 value; };

constexpr qint64 H  = 3600;
constexpr qint64 MB = 1024LL * 1024;

/// An unfinished run touched this recently may still be being written.
constexpr qint64 LIVE_GRACE_SECS = 3600;

const Choice AGE[] = {
    { "1 hour", 1 * H }, { "6 hours", 6 * H }, { "12 hours", 12 * H },
    { "1 day", 24 * H }, { "7 days", 7 * 24 * H }, { "30 days", 30 * 24 * H },
    { "Forever", 0 },
};
const Choice SIZE[] = {
    { "256 MB", 256 * MB }, { "512 MB", 512 * MB }, { "1 GB", 1024 * MB },
    { "2 GB", 2048 * MB }, { "5 GB", 5120 * MB }, { "10 GB", 10240 * MB },
    { "50 GB", 51200 * MB }, { "No limit", 0 },
};

template <size_t N>
QStringList labels(const Choice (&list)[N])
{
    QStringList out;
    for (const Choice &c : list)
        out.append(QString::fromLatin1(c.label));
    return out;
}

template <size_t N>
qint64 valueOf(const Choice (&list)[N], const QString &label, qint64 fallback)
{
    for (const Choice &c : list)
        if (label == QLatin1String(c.label))
            return c.value;
    return fallback;
}

template <size_t N>
QString labelOf(const Choice (&list)[N], qint64 value, const QString &fallback)
{
    for (const Choice &c : list)
        if (c.value == value)
            return QString::fromLatin1(c.label);
    return fallback;
}

qint64 dirSize(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QDateTime newestFileTime(const QString &path)
{
    QDateTime newest;
    QDirIterator it(path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QDateTime t = it.fileInfo().lastModified();
        if (!newest.isValid() || t > newest)
            newest = t;
    }
    return newest.isValid() ? newest : QFileInfo(path).lastModified();
}

struct Run
{
    QString   sessionDir;
    QString   dir;          // absolute
    QString   relDir;       // as recorded in session.json
    QDateTime ended;        // invalid while active
    qint64    bytes = 0;
    bool      isProtected = false;
};

} // namespace

const QStringList &RetentionPolicy::ageChoices()
{
    static const QStringList l = labels(AGE);
    return l;
}
qint64 RetentionPolicy::ageFromChoice(const QString &c) { return valueOf(AGE, c, 12 * H); }
QString RetentionPolicy::choiceForAge(qint64 s) { return labelOf(AGE, s, QStringLiteral("12 hours")); }

const QStringList &RetentionPolicy::sizeChoices()
{
    static const QStringList l = labels(SIZE);
    return l;
}
qint64 RetentionPolicy::sizeFromChoice(const QString &c) { return valueOf(SIZE, c, 1024 * MB); }
QString RetentionPolicy::choiceForSize(qint64 b) { return labelOf(SIZE, b, QStringLiteral("1 GB")); }

QString RetentionPolicy::summary() const
{
    const QString age = maxAgeSecs ? choiceForAge(maxAgeSecs) : QStringLiteral("forever");
    const QString size = maxBytes ? choiceForSize(maxBytes) : QStringLiteral("no size limit");
    return QStringLiteral("keep %1 / %2").arg(age, size);
}

RetentionReport applyRetention(const QString &baseDir, const RetentionPolicy &policy,
                               const QSet<QString> &activeRunDirs, const QDateTime &now)
{
    RetentionReport report;
    QVector<Run> runs;
    QHash<QString, QJsonObject> sessions;    // session dir -> its session.json

    const QDir base(baseDir);
    const QFileInfoList entries = base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString sessionDir = entry.absoluteFilePath();
        QFile f(QDir(sessionDir).filePath(QStringLiteral("session.json")));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
        if (doc.value(QStringLiteral("format")).toString() != QLatin1String("uartx-session"))
            continue;
        sessions.insert(sessionDir, doc);

        const QJsonArray list = doc.value(QStringLiteral("runs")).toArray();
        for (const QJsonValue &v : list) {
            const QJsonObject o = v.toObject();
            Run run;
            run.sessionDir = sessionDir;
            run.relDir = o.value(QStringLiteral("dir")).toString();
            if (run.relDir.isEmpty() || run.relDir.contains(QLatin1String(".."))
                || run.relDir.contains(QLatin1Char('/')) || run.relDir.contains(QLatin1Char('\\')))
                continue;   // never follow a path out of the session folder
            run.dir = QDir(sessionDir).filePath(run.relDir);
            if (!QFileInfo(run.dir).isDir())
                continue;
            run.bytes = dirSize(run.dir);

            const bool active = activeRunDirs.contains(QDir::cleanPath(run.dir));
            const bool pinned = o.value(QStringLiteral("pinned")).toBool(false);
            run.isProtected = active || pinned;

            const double endedMs = o.value(QStringLiteral("endedMs")).toDouble(0);
            if (endedMs > 0)
                run.ended = QDateTime::fromMSecsSinceEpoch(qint64(endedMs));
            else if (!active) {
                // Never closed, and not a run this process says it is writing.
                // Either UartX stopped without finishing it -- its last write
                // is then the best end time there is -- or someone else is
                // writing it right now: another UartX instance, or this one in
                // the moment between its first byte and telling us. A run
                // written to within the last hour is treated as live.
                run.ended = newestFileTime(run.dir);
                if (run.ended.secsTo(now) < LIVE_GRACE_SECS)
                    run.isProtected = true;
            }
            runs.append(run);
        }
    }

    // Oldest first; active runs (no end) sort last.
    std::sort(runs.begin(), runs.end(), [](const Run &a, const Run &b) {
        if (a.ended.isValid() != b.ended.isValid())
            return a.ended.isValid();
        return a.ended < b.ended;
    });

    qint64 total = 0;
    for (const Run &r : std::as_const(runs))
        total += r.bytes;

    QSet<QString> deleted;
    auto remove = [&](const Run &r) {
        if (QDir(r.dir).removeRecursively()) {
            deleted.insert(r.dir);
            report.deletedRuns.append(r.dir);
            report.freedBytes += r.bytes;
            total -= r.bytes;
        }
    };

    for (const Run &r : std::as_const(runs)) {
        if (r.isProtected || !r.ended.isValid())
            continue;
        const bool tooOld = policy.maxAgeSecs > 0
                            && r.ended.secsTo(now) > policy.maxAgeSecs;
        const bool overCap = policy.maxBytes > 0 && total > policy.maxBytes;
        if (tooOld || overCap)
            remove(r);
    }

    if (policy.maxBytes > 0 && total > policy.maxBytes) {
        qint64 protectedBytes = 0;
        for (const Run &r : std::as_const(runs))
            if (!deleted.contains(r.dir) && r.isProtected)
                protectedBytes += r.bytes;
        report.activeOverCap = protectedBytes > policy.maxBytes;
    }
    report.keptBytes = total;

    // Keep each session.json honest about which runs still exist, and drop a
    // session folder once its last run is gone.
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        const QString sessionDir = it.key();
        QJsonObject doc = it.value();
        const QJsonArray list = doc.value(QStringLiteral("runs")).toArray();
        QJsonArray kept;
        bool changed = false;
        for (const QJsonValue &v : list) {
            const QString dir = QDir(sessionDir).filePath(v.toObject().value(QStringLiteral("dir")).toString());
            if (deleted.contains(dir))
                changed = true;
            else
                kept.append(v);
        }
        if (!changed)
            continue;
        if (kept.isEmpty()) {
            bool anyActive = false;
            for (const QString &a : activeRunDirs)
                if (a.startsWith(QDir::cleanPath(sessionDir) + QLatin1Char('/')))
                    anyActive = true;
            if (!anyActive) {
                QDir(sessionDir).removeRecursively();
                continue;
            }
        }
        doc[QStringLiteral("runs")] = kept;
        QSaveFile out(QDir(sessionDir).filePath(QStringLiteral("session.json")));
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(doc).toJson(QJsonDocument::Indented));
            out.commit();
        }
    }
    return report;
}
