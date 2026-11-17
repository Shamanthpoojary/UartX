#include "controlserver.h"
#include "appconstants.h"
#include "lineassembler.h"
#include "textdecoder.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>
#include <map>

namespace {

/// Hard caps on what one response may carry. The agent pays for every line
/// it is sent, so the defaults are small and the ceiling is firm; whatever is
/// left out is always counted, never silently dropped.
constexpr int MAX_LINES_PER_RESPONSE = 200;
constexpr int DEFAULT_MATCHES        = 20;
constexpr int DEFAULT_TAIL           = 20;
constexpr int MAX_LINE_CHARS         = 300;
constexpr int MAX_CONTEXT            = 50;
constexpr int WAIT_POLL_MS           = 50;
constexpr int WAIT_QUIET_MS          = 300;    // after a match: stop once the device goes quiet...
constexpr int WAIT_MAX_AFTER_MS      = 3000;   // ...or after this long, whichever is first
constexpr int MAX_REQUEST_BYTES      = 64 * 1024;

QString endpointName()
{
    // One endpoint per OS user: two people logged in to the same machine
    // each get their own UartX, and neither can see the other's.
    const QByteArray who = (QDir::homePath() + QLatin1Char('|') + App::configDir()).toUtf8();
    return QStringLiteral("uartx-")
           + QString::fromLatin1(QCryptographicHash::hash(who, QCryptographicHash::Sha1).toHex().left(12));
}

QString dirName(Direction d)
{
    switch (d) {
    case Direction::Rx:  return QStringLiteral("RX");
    case Direction::Tx:  return QStringLiteral("TX");
    case Direction::Sys: return QStringLiteral("SYS");
    }
    return QStringLiteral("RX");
}

int intParam(const QJsonObject &p, const char *key, int def, int lo, int hi)
{
    const QJsonValue v = p.value(QLatin1String(key));
    const int n = v.isDouble() ? v.toInt() : def;
    return qBound(lo, n, hi);
}

QString seqRange(quint64 a, quint64 b)
{
    return a == b ? QStringLiteral("#%1").arg(a) : QStringLiteral("#%1–#%2").arg(a).arg(b);
}

QString plural(qint64 n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(QLocale().toString(n), n == 1 ? one : many);
}

/// The "kind" of a line for the summary: its first word, cut at the first
/// separator. Firmware-agnostic -- it assumes nothing about the format, it
/// just groups lines that start the same way ("BLE", "WIFI", "E (123)").
QString tagOf(const QString &text)
{
    const QString t = text.trimmed();
    int end = 0;
    while (end < t.size() && end < 24) {
        const QChar c = t.at(end);
        if (c.isSpace() || c == QLatin1Char(':') || c == QLatin1Char('=') || c == QLatin1Char('['))
            break;
        ++end;
    }
    return end ? t.left(end) : QStringLiteral("(blank)");
}

} // namespace

struct ControlServer::Client
{
    QByteArray buffer;
    bool       authed = false;
    QString    agent = QStringLiteral("claude");
};

struct ControlServer::Wait
{
    QPointer<QLocalSocket> socket;
    QJsonValue  id;
    QString     agent;
    LineQuery   query;
    int         before = 10;
    int         after = 20;
    QDeadlineTimer deadline;
    qint64      timeoutMs = 0;
    std::shared_ptr<const RecentRing> ring;
    quint64     nextSeq = 0;
    quint64     hitSeq = 0;
    int         hits = 0;
    int         afterSeen = 0;
    bool        sysEvent = false;
    QElapsedTimer quiet;
    QElapsedTimer sinceHit;
};

ControlServer::ControlServer(std::shared_ptr<AgentBridge> bridge, AgentHost *host,
                             QObject *hostObject)
    : m_bridge(std::move(bridge))
    , m_host(host)
    , m_hostObject(hostObject)
{
}

ControlServer::~ControlServer() = default;

QString ControlServer::runtimeFilePath()
{
    return QDir(App::configDir()).filePath(QStringLiteral("runtime.json"));
}

void ControlServer::start()
{
    m_endpoint = endpointName();

    // A second UartX would otherwise open a second pipe of the same name and
    // the agent's calls would land on whichever answered first. Until UartX
    // is single-instance, the first one keeps the API.
    {
        QLocalSocket probe;
        probe.connectToServer(m_endpoint);
        if (probe.waitForConnected(200)) {
            qWarning("agent API: another UartX already serves %s", qPrintable(m_endpoint));
            return;
        }
    }

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(m_endpoint);    // a stale Unix socket from a crash
    if (!m_server->listen(m_endpoint)) {
        qWarning("agent API: cannot listen on %s: %s", qPrintable(m_endpoint),
                 qPrintable(m_server->errorString()));
        return;
    }
    connect(m_server, &QLocalServer::newConnection, this, &ControlServer::onNewConnection);

    m_waitTimer = new QTimer(this);
    connect(m_waitTimer, &QTimer::timeout, this, &ControlServer::pollWaits);
    m_waitTimer->start(WAIT_POLL_MS);

    QByteArray raw(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), 4);
    m_token = QString::fromLatin1(raw.toHex());

    QJsonObject rt;
    rt["endpoint"] = m_endpoint;
    rt["pid"]      = double(QCoreApplication::applicationPid());
    rt["protocol"] = PROTOCOL;
    rt["version"]  = App::VERSION;
    rt["token"]    = m_token;
    QDir().mkpath(App::configDir());
    QSaveFile f(runtimeFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(rt).toJson(QJsonDocument::Indented));
        if (f.commit())
            QFile::setPermissions(runtimeFilePath(), QFile::ReadOwner | QFile::WriteOwner);
    }
}

void ControlServer::stop()
{
    for (const auto &w : std::as_const(m_waits))
        if (w->socket)
            fail(w->socket, w->id, QStringLiteral("APP_EXITED"), QStringLiteral("UartX is closing"));
    m_waits.clear();
    if (m_server) {
        m_server->close();
        // Only our own file: a second instance that failed to start must not
        // delete the first one's.
        QFile f(runtimeFilePath());
        if (f.open(QIODevice::ReadOnly)
            && QJsonDocument::fromJson(f.readAll()).object().value("token").toString() == m_token) {
            f.close();
            QFile::remove(runtimeFilePath());
        }
    }
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        it.key()->disconnectFromServer();
    m_clients.clear();
}

void ControlServer::onNewConnection()
{
    while (QLocalSocket *s = m_server->nextPendingConnection()) {
        m_clients.insert(s, std::make_shared<Client>());
        connect(s, &QLocalSocket::readyRead, this, [this, s] { onReadyRead(s); });
        connect(s, &QLocalSocket::disconnected, this, [this, s] {
            m_clients.remove(s);
            s->deleteLater();
        });
    }
}

void ControlServer::onReadyRead(QLocalSocket *socket)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;
    Client &c = *it.value();
    c.buffer += socket->readAll();
    if (c.buffer.size() > MAX_REQUEST_BYTES) {
        socket->disconnectFromServer();
        return;
    }
    int nl;
    while ((nl = c.buffer.indexOf('\n')) >= 0) {
        const QByteArray line = c.buffer.left(nl).trimmed();
        c.buffer.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (!doc.isObject()) {
            fail(socket, QJsonValue(), QStringLiteral("PARSE_ERROR"), err.errorString());
            continue;
        }
        handle(socket, doc.object());
    }
}

void ControlServer::reply(QLocalSocket *socket, const QJsonValue &id, const QJsonObject &result)
{
    if (!socket)
        return;
    QJsonObject o;
    o["jsonrpc"] = QStringLiteral("2.0");
    o["id"] = id;
    if (result.contains(QStringLiteral("error"))) {
        QJsonObject e;
        e["code"] = -32000;
        e["message"] = result.value("message").toString();
        e["data"] = QJsonObject{ { QStringLiteral("name"), result.value("error") } };
        o["error"] = e;
    } else {
        o["result"] = result;
    }
    socket->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

void ControlServer::fail(QLocalSocket *socket, const QJsonValue &id, const QString &code,
                         const QString &message)
{
    reply(socket, id, QJsonObject{ { QStringLiteral("error"), code },
                                   { QStringLiteral("message"), message } });
}

void ControlServer::handle(QLocalSocket *socket, const QJsonObject &req)
{
    Client &c = *m_clients.value(socket);
    const QJsonValue id = req.value("id");
    const QString method = req.value("method").toString();
    const QJsonObject p = req.value("params").toObject();

    if (method == QLatin1String("hello")) {
        if (p.value("token").toString() != m_token || m_token.isEmpty()) {
            fail(socket, id, QStringLiteral("ACCESS_DENIED"),
                 QStringLiteral("wrong or missing token; is this the UartX that wrote runtime.json?"));
            socket->disconnectFromServer();
            return;
        }
        c.authed = true;
        const QString agent = p.value("agent").toString().trimmed();
        if (!agent.isEmpty())
            c.agent = agent.left(32);
        reply(socket, id, QJsonObject{ { "protocol", PROTOCOL }, { "app", App::NAME },
                                       { "version", App::VERSION }, { "agent", c.agent } });
        return;
    }
    if (!c.authed) {
        fail(socket, id, QStringLiteral("ACCESS_DENIED"), QStringLiteral("send hello first"));
        return;
    }

    if (method == QLatin1String("status"))
        reply(socket, id, doStatus());
    else if (method == QLatin1String("summary"))
        reply(socket, id, doSummary(c.agent));
    else if (method == QLatin1String("tail"))
        reply(socket, id, doTail(c.agent, p));
    else if (method == QLatin1String("search"))
        reply(socket, id, doSearch(c.agent, p));
    else if (method == QLatin1String("wait")) {
        QString error;
        if (!startWait(socket, id, c.agent, p, &error))
            fail(socket, id, QStringLiteral("INVALID_PATTERN"), error);
        // otherwise the reply comes when the pattern appears or time runs out
    } else if (method == QLatin1String("open") || method == QLatin1String("close")
               || method == QLatin1String("filter.add") || method == QLatin1String("filter.list")
               || method == QLatin1String("filter.rm") || method == QLatin1String("note")) {
        reply(socket, id, callHost(c.agent, method, p));
    } else {
        fail(socket, id, QStringLiteral("NOT_FOUND"), QStringLiteral("unknown method '%1'").arg(method));
    }
}

QJsonObject ControlServer::callHost(const QString &agent, const QString &method, const QJsonObject &p)
{
    QJsonObject result{ { "error", "APP_EXITED" }, { "message", "UartX window is gone" } };
    if (!m_hostObject)
        return result;
    // Blocking: the answer is what the window did. The window never waits on
    // this thread, so this cannot deadlock.
    AgentHost *host = m_host;
    QMetaObject::invokeMethod(
        m_hostObject, [&result, host, agent, method, p] { result = host->agentAction(agent, method, p); },
        Qt::BlockingQueuedConnection);
    return result;
}

void ControlServer::activity(const QString &agent, const QString &sentence)
{
    if (!m_hostObject)
        return;
    AgentHost *host = m_host;
    QMetaObject::invokeMethod(m_hostObject, [host, agent, sentence] { host->agentActivity(agent, sentence); },
                              Qt::QueuedConnection);
}

// ------------------------------------------------------------------ reads

QString ControlServer::textOf(const LineRecord &r, const SessionSnapshot &s) const
{
    if (r.dir == Direction::Sys)
        return QString::fromUtf8(r.bytes);
    TextDecoder d;
    d.setEncoding(s.encoding);
    QString t = stripEscapeSequences(d.decode(r.bytes));
    while (t.endsWith(QLatin1Char('\r')) || t.endsWith(QLatin1Char('\n')))
        t.chop(1);
    return t;
}

QJsonObject ControlServer::lineJson(const LineRecord &r, const SessionSnapshot &s) const
{
    QString text = textOf(r, s);
    QJsonObject o;
    o["seq"]    = double(r.seq);
    o["time"]   = QDateTime::fromMSecsSinceEpoch(r.wallMs).toString(QStringLiteral("HH:mm:ss.zzz"));
    o["dir"]    = dirName(r.dir);
    o["origin"] = r.origin;
    if (r.has(LineFlag::Partial))
        o["partial"] = true;
    // A runaway line must not eat the agent's budget; say how much was cut.
    if (text.size() > MAX_LINE_CHARS) {
        o["cut"] = text.size() - MAX_LINE_CHARS;
        text.truncate(MAX_LINE_CHARS);
    }
    o["text"] = text;
    return o;
}

QJsonObject ControlServer::doStatus()
{
    const SessionSnapshot s = m_bridge->get();
    QJsonObject session;
    session["open"]    = s.open;
    session["id"]      = s.id;
    session["port"]    = s.port;
    session["baud"]    = s.baud;
    session["status"]  = s.status;
    session["lines"]   = double(s.totalLines);
    session["rxBytes"] = double(s.rxBytes);
    session["log"] = QJsonObject{ { "enabled", s.log.enabled }, { "created", s.log.created },
                                  { "stopped", s.log.stopped }, { "reason", s.log.reason },
                                  { "dir", QDir::toNativeSeparators(s.log.runDir) } };
    if (s.ring) {
        session["firstSeq"] = double(s.ring->firstSeq());
        session["lastSeq"]  = double(s.ring->lastSeq());
        session["inMemory"] = s.ring->size();
    }
    return QJsonObject{ { "app", App::NAME }, { "version", App::VERSION }, { "session", session } };
}

QJsonObject ControlServer::doSummary(const QString &agent)
{
    const SessionSnapshot s = m_bridge->get();
    if (!s.ring)
        return { { "error", "NOT_FOUND" }, { "message", "no session has been opened yet" } };

    const QVector<LineRecord> all = s.ring->since(0, s.ring->capacity());
    std::map<QString, int> tags;
    int rx = 0, tx = 0, sys = 0;
    QJsonArray events;
    for (const LineRecord &r : all) {
        if (r.dir == Direction::Tx) { ++tx; continue; }
        if (r.dir == Direction::Sys) {
            ++sys;
            if (events.size() < 20)
                events.append(lineJson(r, s));
            continue;
        }
        ++rx;
        ++tags[tagOf(textOf(r, s))];
    }
    QVector<std::pair<int, QString>> ranked;
    for (const auto &kv : tags)
        ranked.append({ kv.second, kv.first });
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QJsonArray top;
    for (int i = 0; i < ranked.size() && i < 20; ++i)
        top.append(QJsonObject{ { "tag", ranked[i].second }, { "count", ranked[i].first } });

    // The last few seconds' rate says whether the device is talking now.
    qint64 recent = 0;
    if (!all.isEmpty()) {
        const qint64 cutoff = all.last().monoNs - 10LL * 1000000000LL;
        for (auto it = all.crbegin(); it != all.crend() && it->monoNs >= cutoff; ++it)
            ++recent;
    }

    activity(agent, QStringLiteral("asked for a summary of %1 (counts only, no log lines sent)")
                        .arg(plural(all.size(), QStringLiteral("line"), QStringLiteral("lines"))));
    QJsonObject o;
    o["port"] = s.port;
    o["open"] = s.open;
    o["firstSeq"] = all.isEmpty() ? 0.0 : double(all.first().seq);
    o["lastSeq"]  = all.isEmpty() ? 0.0 : double(all.last().seq);
    o["rx"] = rx;
    o["tx"] = tx;
    o["sys"] = sys;
    o["linesLast10s"] = double(recent);
    o["kinds"] = top;
    o["kindsTotal"] = int(tags.size());
    o["systemEvents"] = events;
    return o;
}

QJsonObject ControlServer::doTail(const QString &agent, const QJsonObject &p)
{
    const SessionSnapshot s = m_bridge->get();
    if (!s.ring)
        return { { "error", "NOT_FOUND" }, { "message", "no session has been opened yet" } };
    const int n = intParam(p, "n", DEFAULT_TAIL, 1, MAX_LINES_PER_RESPONSE);
    const LineQuery q(p.value("match").toString(), p.value("regex").toBool(), p.value("case").toBool());
    if (!q.isValid())
        return { { "error", "INVALID_PATTERN" }, { "message", q.error() } };

    const QVector<LineRecord> all = s.ring->since(0, s.ring->capacity());
    QVector<LineRecord> picked;
    for (auto it = all.crbegin(); it != all.crend() && picked.size() < n; ++it)
        if (q.matches(textOf(*it, s)))
            picked.prepend(*it);

    QJsonArray lines;
    for (const LineRecord &r : picked)
        lines.append(lineJson(r, s));
    if (!picked.isEmpty())
        activity(agent, QStringLiteral("read the last %1%2 (%3)")
                            .arg(plural(picked.size(), QStringLiteral("line"), QStringLiteral("lines")),
                                 q.pattern().isEmpty() ? QString()
                                                       : QStringLiteral(" matching ") + q.describe(),
                                 seqRange(picked.first().seq, picked.last().seq)));
    return { { "lines", lines } };
}

QJsonObject ControlServer::doSearch(const QString &agent, const QJsonObject &p)
{
    const SessionSnapshot s = m_bridge->get();
    if (!s.ring)
        return { { "error", "NOT_FOUND" }, { "message", "no session has been opened yet" } };
    const LineQuery q(p.value("pattern").toString(), p.value("regex").toBool(), p.value("case").toBool());
    if (!q.isValid())
        return { { "error", "INVALID_PATTERN" }, { "message", q.error() } };
    if (q.pattern().isEmpty())
        return { { "error", "INVALID_PATTERN" }, { "message", "search needs a pattern; use tail for recent lines" } };

    const int maxMatches = intParam(p, "max", DEFAULT_MATCHES, 1, MAX_LINES_PER_RESPONSE);
    const int before = intParam(p, "before", 0, 0, MAX_CONTEXT);
    const int after = intParam(p, "after", 0, 0, MAX_CONTEXT);
    const bool fromStart = p.value("first").toBool();

    const QVector<LineRecord> all = s.ring->since(quint64(p.value("since").toDouble(0)), s.ring->capacity());
    QVector<int> hits;
    for (int i = 0; i < all.size(); ++i)
        if (q.matches(textOf(all[i], s)))
            hits.append(i);

    // Most recent matches by default -- live debugging is about what just
    // happened -- or the earliest ones when asked (the first error of a boot).
    const int total = hits.size();
    if (total > maxMatches)
        hits = fromStart ? hits.mid(0, maxMatches) : hits.mid(total - maxMatches);

    // Context around each hit, overlapping ranges merged, under one budget.
    QVector<bool> take(all.size(), false);
    QVector<bool> isHit(all.size(), false);
    for (int h : std::as_const(hits)) {
        isHit[h] = true;
        for (int i = qMax(0, h - before); i <= qMin(all.size() - 1, h + after); ++i)
            take[i] = true;
    }
    QJsonArray lines;
    int sent = 0, cutForBudget = 0;
    quint64 firstSent = 0, lastSent = 0, prevSeq = 0;
    for (int i = 0; i < all.size(); ++i) {
        if (!take[i])
            continue;
        if (sent >= MAX_LINES_PER_RESPONSE) {
            ++cutForBudget;
            continue;
        }
        QJsonObject o = lineJson(all[i], s);
        if (isHit[i])
            o["hit"] = true;
        // A jump in seq marks where one excerpt ends and the next begins.
        if (prevSeq && all[i].seq != prevSeq + 1)
            o["gapBefore"] = true;
        prevSeq = all[i].seq;
        lines.append(o);
        if (!firstSent)
            firstSent = all[i].seq;
        lastSent = all[i].seq;
        ++sent;
    }

    activity(agent, total
                        ? QStringLiteral("searched for %1: %2, sent %3 (%4)")
                              .arg(q.describe(),
                                   plural(total, QStringLiteral("match"), QStringLiteral("matches")),
                                   plural(sent, QStringLiteral("line"), QStringLiteral("lines")),
                                   seqRange(firstSent, lastSent))
                        : QStringLiteral("searched for %1: no matches in the last %2")
                              .arg(q.describe(), plural(all.size(), QStringLiteral("line"),
                                                        QStringLiteral("lines"))));
    QJsonObject o;
    o["pattern"] = q.describe();
    o["matches"] = total;
    o["omittedMatches"] = total - hits.size();
    o["omittedForBudget"] = cutForBudget;
    o["searched"] = all.size();
    o["lines"] = lines;
    return o;
}

// ------------------------------------------------------------------- wait

bool ControlServer::startWait(QLocalSocket *socket, const QJsonValue &id, const QString &agent,
                              const QJsonObject &p, QString *error)
{
    auto w = std::make_shared<Wait>();
    w->query = LineQuery(p.value("pattern").toString(), p.value("regex").toBool(), p.value("case").toBool());
    if (!w->query.isValid() || w->query.pattern().isEmpty()) {
        *error = w->query.isValid() ? QStringLiteral("wait needs a pattern") : w->query.error();
        return false;
    }
    w->socket = socket;
    w->id = id;
    w->agent = agent;
    w->before = intParam(p, "before", 10, 0, MAX_CONTEXT);
    w->after = intParam(p, "after", 20, 0, MAX_CONTEXT * 2);
    w->timeoutMs = qBound<qint64>(1000, qint64(p.value("timeoutMs").toDouble(30000)), 24LL * 3600 * 1000);
    w->deadline = QDeadlineTimer(w->timeoutMs);

    // From now on: a wait is about what happens next, not what already did.
    const SessionSnapshot s = m_bridge->get();
    w->ring = s.ring;
    w->nextSeq = s.ring ? s.ring->lastSeq() + 1 : 1;
    w->quiet.start();
    m_waits.append(w);
    activity(agent, QStringLiteral("is waiting for %1 (up to %2 s); nothing is sent until it appears")
                        .arg(w->query.describe())
                        .arg(w->timeoutMs / 1000));
    return true;
}

void ControlServer::pollWaits()
{
    if (m_waits.isEmpty())
        return;
    const SessionSnapshot s = m_bridge->get();

    for (int wi = m_waits.size() - 1; wi >= 0; --wi) {
        const std::shared_ptr<Wait> w = m_waits[wi];
        if (!w->socket) {                          // the agent gave up
            m_waits.remove(wi);
            continue;
        }
        // A session opened after the wait began: follow it from its start.
        if (!w->ring && s.ring) {
            w->ring = s.ring;
            w->nextSeq = s.ring->firstSeq();
        }

        if (w->ring) {
            const QVector<LineRecord> fresh = w->ring->since(w->nextSeq, 20000);
            if (!fresh.isEmpty()) {
                w->nextSeq = fresh.last().seq + 1;
                w->quiet.restart();
            }
            for (const LineRecord &r : fresh) {
                if (w->hitSeq) {
                    ++w->afterSeen;
                    if (w->query.matches(textOf(r, s)))
                        ++w->hits;          // later matches merge into this event
                    continue;
                }
                // Losing the device is always worth waking up for.
                const bool sysEnd = r.dir == Direction::Sys
                                    && (r.bytes.startsWith("disconnected") || r.bytes.startsWith("log stopped"));
                if (sysEnd || (r.dir != Direction::Sys && w->query.matches(textOf(r, s)))) {
                    w->hitSeq = r.seq;
                    w->hits = 1;
                    w->sysEvent = sysEnd;
                    w->sinceHit.start();
                }
            }
        }

        const bool done = w->hitSeq
                          && (w->afterSeen >= w->after || w->quiet.elapsed() >= WAIT_QUIET_MS
                              || w->sinceHit.elapsed() >= WAIT_MAX_AFTER_MS || w->sysEvent);
        if (done) {
            const quint64 from = w->hitSeq > quint64(w->before) ? w->hitSeq - quint64(w->before) : 1;
            const int count = qMin(MAX_LINES_PER_RESPONSE, w->before + 1 + w->afterSeen);
            const QVector<LineRecord> window = w->ring->since(from, count);
            QJsonArray lines;
            for (const LineRecord &r : window) {
                QJsonObject o = lineJson(r, s);
                if (r.seq == w->hitSeq)
                    o["hit"] = true;
                lines.append(o);
            }
            QJsonObject result;
            result["event"] = w->sysEvent ? QStringLiteral("sys") : QStringLiteral("match");
            result["pattern"] = w->query.describe();
            result["hitSeq"] = double(w->hitSeq);
            result["matches"] = w->hits;
            result["lines"] = lines;
            reply(w->socket, w->id, result);
            const quint64 last = window.isEmpty() ? w->hitSeq : window.last().seq;
            activity(w->agent, w->sysEvent
                                   ? QStringLiteral("was woken by a session event at #%1; sent %2 (%3)")
                                         .arg(w->hitSeq)
                                         .arg(plural(lines.size(), QStringLiteral("line"), QStringLiteral("lines")),
                                              seqRange(from, last))
                                   : QStringLiteral("was woken: %1 appeared at #%2%3; sent %4 (%5)")
                                         .arg(w->query.describe())
                                         .arg(w->hitSeq)
                                         .arg(w->hits > 1 ? QStringLiteral(" (×%1)").arg(w->hits) : QString(),
                                              plural(lines.size(), QStringLiteral("line"), QStringLiteral("lines")),
                                              seqRange(from, last)));
            m_waits.remove(wi);
            continue;
        }

        if (!w->hitSeq && w->deadline.hasExpired()) {
            fail(w->socket, w->id, QStringLiteral("TIMEOUT"),
                 QStringLiteral("nothing matched %1 within %2 s")
                     .arg(w->query.describe())
                     .arg(w->timeoutMs / 1000));
            activity(w->agent, QStringLiteral("stopped waiting for %1: it did not appear within %2 s")
                                   .arg(w->query.describe())
                                   .arg(w->timeoutMs / 1000));
            m_waits.remove(wi);
        }
    }
}
