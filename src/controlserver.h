#ifndef CONTROLSERVER_H
#define CONTROLSERVER_H

#include "agentbridge.h"
#include "linequery.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <memory>

class QLocalServer;
class QLocalSocket;
class QTimer;

/// The agent API: a local socket (a named pipe on Windows, a Unix socket
/// elsewhere) speaking newline-delimited JSON-RPC 2.0.
///
/// The point is that the agent never reads the raw stream. It says what it is
/// looking for, and UartX does the searching and sends back a bounded
/// excerpt -- a few lines, capped, with a count of what was left out -- or
/// waits until the pattern appears and sends only that moment.
///
/// Runs on a thread of its own. Searches read the session's recent-lines ring
/// directly, which is thread-safe, so a regex over fifty thousand lines never
/// costs the window a frame. Anything that changes what the user sees --
/// opening a filter view, a note, connecting -- is handed to the window
/// (AgentHost) on the GUI thread, and every request is reported to the user
/// as a sentence.
///
/// Access: the socket accepts only the current OS user, and every connection
/// must first present the random token UartX writes to runtime.json in its
/// config folder. That keeps out other users of the machine; it does not keep
/// out other programs run by the same user, which can read the file too.
class ControlServer : public QObject
{
    Q_OBJECT

public:
    static constexpr int PROTOCOL = 1;

    ControlServer(std::shared_ptr<AgentBridge> bridge, AgentHost *host, QObject *hostObject);
    ~ControlServer() override;

    /// Where uartx-ctl looks for the endpoint and token.
    static QString runtimeFilePath();

public slots:
    /// Starts listening and writes runtime.json. Call on the control thread.
    void start();
    /// Answers every pending wait with APP_EXITED, closes the socket and
    /// removes runtime.json.
    void stop();

private:
    struct Client;
    struct Wait;

    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);
    void handle(QLocalSocket *socket, const QJsonObject &request);
    void reply(QLocalSocket *socket, const QJsonValue &id, const QJsonObject &result);
    void fail(QLocalSocket *socket, const QJsonValue &id, const QString &code,
              const QString &message);

    QJsonObject doStatus();
    QJsonObject doSummary(const QString &agent);
    QJsonObject doTail(const QString &agent, const QJsonObject &p);
    QJsonObject doSearch(const QString &agent, const QJsonObject &p);
    bool        startWait(QLocalSocket *socket, const QJsonValue &id, const QString &agent,
                          const QJsonObject &p, QString *error);
    void        pollWaits();
    QJsonObject callHost(const QString &agent, const QString &method, const QJsonObject &p);
    void        activity(const QString &agent, const QString &sentence);

    QJsonObject lineJson(const LineRecord &r, const SessionSnapshot &s) const;
    QString     textOf(const LineRecord &r, const SessionSnapshot &s) const;

    std::shared_ptr<AgentBridge> m_bridge;
    AgentHost                   *m_host = nullptr;
    QPointer<QObject>            m_hostObject;
    QLocalServer                *m_server = nullptr;
    QTimer                      *m_waitTimer = nullptr;
    QString                      m_token;
    QString                      m_endpoint;
    QHash<QLocalSocket *, std::shared_ptr<Client>> m_clients;
    QVector<std::shared_ptr<Wait>> m_waits;
};

#endif // CONTROLSERVER_H
