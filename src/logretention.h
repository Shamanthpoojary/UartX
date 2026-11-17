#ifndef LOGRETENTION_H
#define LOGRETENTION_H

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QStringList>

/// How long, and how much, log data to keep.
struct RetentionPolicy
{
    qint64 maxAgeSecs = 12 * 3600;                 ///< 0 = keep forever
    qint64 maxBytes   = 1024LL * 1024 * 1024;      ///< 0 = no size cap

    /// The choices offered in Settings, with their values in seconds/bytes.
    static const QStringList &ageChoices();
    static qint64 ageFromChoice(const QString &choice);
    static QString choiceForAge(qint64 secs);
    static const QStringList &sizeChoices();
    static qint64 sizeFromChoice(const QString &choice);
    static QString choiceForSize(qint64 bytes);

    /// "keep 12 h / 1 GB" -- for the status bar.
    QString summary() const;
};

struct RetentionReport
{
    QStringList deletedRuns;       ///< run folders removed
    qint64      freedBytes   = 0;
    qint64      keptBytes    = 0;  ///< what remains under UartX's control
    /// The active runs on their own exceed the size cap. Nothing active is
    /// ever deleted, so this is a warning for the user, not an action.
    bool        activeOverCap = false;
};

/// Applies the policy to every UartX session folder directly inside `baseDir`.
///
/// Only folders holding a session.json written by UartX are considered;
/// anything else in the log folder -- v1.3 log files, the user's own notes --
/// is never touched. Oldest finished runs go first. A run is never deleted
/// while it is active (`activeRunDirs`, absolute paths) or pinned, and its age
/// is measured from when it ended. A session folder left with no runs is
/// removed with them.
///
/// Does blocking file I/O; run it off the GUI thread.
RetentionReport applyRetention(const QString &baseDir, const RetentionPolicy &policy,
                               const QSet<QString> &activeRunDirs, const QDateTime &now);

#endif // LOGRETENTION_H
