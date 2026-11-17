#ifndef AGENTBRIDGE_H
#define AGENTBRIDGE_H

#include "pipeline.h"
#include "recentring.h"

#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <memory>

/// What the agent API may know about the session, as of the last update.
/// Copied out under a lock, so the control thread reads it without ever
/// touching a widget or waiting on the GUI.
struct SessionSnapshot
{
    bool    open = false;
    QString id;
    QString port;
    QString baud;
    QString status;
    QString encoding = QStringLiteral("Auto");
    quint64 totalLines = 0;
    qint64  rxBytes = 0;
    LogState log;
    /// The session's recent lines. Kept after the session closes, so the
    /// agent can still read the tail of what just happened.
    std::shared_ptr<const RecentRing> ring;
};

/// Implemented by the window. Called on the GUI thread, by the control
/// thread, for anything that changes what the user sees.
class AgentHost
{
public:
    virtual ~AgentHost() = default;

    /// Performs `method` for `agent` and returns its result. A result holding
    /// "error" (a code name such as NOT_FOUND) and "message" is a failure.
    virtual QJsonObject agentAction(const QString &agent, const QString &method,
                                    const QJsonObject &params) = 0;

    /// Shows what an agent did -- every read included -- as a sentence the
    /// user can follow.
    virtual void agentActivity(const QString &agent, const QString &sentence) = 0;
};

class AgentBridge
{
public:
    SessionSnapshot get() const
    {
        QMutexLocker lock(&m_mutex);
        return m_snapshot;
    }

    template <typename F>
    void update(F &&change)
    {
        QMutexLocker lock(&m_mutex);
        change(m_snapshot);
    }

private:
    mutable QMutex  m_mutex;
    SessionSnapshot m_snapshot;
};

#endif // AGENTBRIDGE_H
