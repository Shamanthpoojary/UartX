// uartx-ctl: the command-line side of the UartX agent API.
//
// An AI agent (Claude Code) runs this instead of reading a serial log. It
// tells UartX what it is looking for; UartX does the searching and sends back
// a small, capped excerpt. Output is compact text by default -- one line per
// log line -- because every character costs the agent tokens; --json gives
// the structured form.
//
// Each call is one short connection: hello (with the token from runtime.json),
// one request, one reply.

#include "appconstants.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QThread>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

enum Exit { OK = 0, FAILED = 1, USAGE = 2, NOT_RUNNING = 3, TIMED_OUT = 4 };

void out(const QString &s)
{
    const QByteArray b = s.toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), stdout);
    std::fputc('\n', stdout);
}

void err(const QString &s)
{
    const QByteArray b = s.toUtf8();
    std::fwrite(b.constData(), 1, size_t(b.size()), stderr);
    std::fputc('\n', stderr);
}

const char *USAGE_TEXT = R"(uartx-ctl - ask a running UartX for exactly the UART lines you need

  uartx-ctl status                          session, port, line count, log folder
  uartx-ctl summary                         what kinds of lines are flowing (counts only)
  uartx-ctl tail [-n 20] [--match P]        the newest lines, optionally only matching P
  uartx-ctl search P [--before N] [--after N] [--max N] [--first] [--since SEQ]
                                            matching lines with context (newest matches first
                                            unless --first)
  uartx-ctl wait P [--timeout 30s] [--before N] [--after N]
                                            block until a NEW line matches P, then print that
                                            moment only (also wakes on disconnect)
  uartx-ctl filter add NAME P               open a view in UartX showing only lines matching P
  uartx-ctl filter list | filter rm ID
  uartx-ctl note TEXT                       show a plain-language explanation to the user
  uartx-ctl open [PORT] [--baud B] | close  connect / disconnect the port
  uartx-ctl launch                          start UartX if it is not running

  Patterns are literal and case-insensitive; add --regex and/or --case.
  --json prints the raw reply.  --agent ID (or env UARTX_AGENT) names the caller.
  Exit codes: 0 ok, 1 error, 2 usage, 3 UartX not running, 4 wait timed out.)";

struct Args
{
    QStringList positional;
    QHash<QString, QString> opts;
    QSet<QString> flags;

    QString opt(const QString &k, const QString &def = QString()) const { return opts.value(k, def); }
    bool flag(const QString &k) const { return flags.contains(k); }
};

Args parse(const QStringList &argv)
{
    static const QSet<QString> valued = { "-n", "--match", "--before", "--after", "--max", "--since",
                                          "--timeout", "--baud", "--agent" };
    Args a;
    for (int i = 1; i < argv.size(); ++i) {
        const QString s = argv[i];
        if (valued.contains(s) && i + 1 < argv.size())
            a.opts.insert(s, argv[++i]);
        else if (s.startsWith(QLatin1String("--")) || s == QLatin1String("-h"))
            a.flags.insert(s);
        else
            a.positional.append(s);
    }
    return a;
}

/// "30s", "5m", "1h", "1500ms" or plain seconds.
qint64 parseDurationMs(const QString &s, qint64 def)
{
    if (s.isEmpty())
        return def;
    bool ok = false;
    if (s.endsWith(QLatin1String("ms"))) { const qint64 v = s.chopped(2).toLongLong(&ok); return ok ? v : def; }
    const QChar u = s.back();
    const qint64 mult = u == QLatin1Char('h') ? 3600000 : u == QLatin1Char('m') ? 60000 : 1000;
    const QString num = u.isDigit() ? s : s.chopped(1);
    const qint64 v = num.toLongLong(&ok);
    return ok ? v * mult : def;
}

QJsonObject readRuntime()
{
    QFile f(QDir(App::configDir()).filePath(QStringLiteral("runtime.json")));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

class Connection
{
public:
    bool open(const QString &agent, QString *error)
    {
        const QJsonObject rt = readRuntime();
        if (rt.isEmpty()) {
            *error = QStringLiteral("UartX is not running (no runtime.json). Start it, or run: uartx-ctl launch");
            return false;
        }
        m_socket.connectToServer(rt.value("endpoint").toString());
        if (!m_socket.waitForConnected(3000)) {
            *error = QStringLiteral("UartX is not answering on its control socket (%1). Is it still running?")
                         .arg(m_socket.errorString());
            return false;
        }
        QJsonObject hello{ { "token", rt.value("token").toString() }, { "agent", agent },
                           { "client", "uartx-ctl" }, { "protocol", 1 } };
        QJsonObject reply;
        if (!call("hello", hello, 5000, &reply, error))
            return false;
        return true;
    }

    /// Sends one request and waits for its reply. False with `error` set on a
    /// transport problem or an error reply; `errorName` gets the code.
    bool call(const QString &method, const QJsonObject &params, qint64 timeoutMs, QJsonObject *result,
              QString *error, QString *errorName = nullptr)
    {
        const QJsonObject req{ { "jsonrpc", "2.0" }, { "id", ++m_id }, { "method", method }, { "params", params } };
        m_socket.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
        m_socket.flush();

        QDeadlineTimer deadline(timeoutMs);
        while (!m_buffer.contains('\n')) {
            if (m_socket.state() != QLocalSocket::ConnectedState && !m_socket.bytesAvailable()) {
                *error = QStringLiteral("UartX closed the connection");
                if (errorName) *errorName = QStringLiteral("APP_EXITED");
                return false;
            }
            const qint64 left = deadline.remainingTime();
            if (left == 0) {
                *error = QStringLiteral("no reply from UartX in time");
                if (errorName) *errorName = QStringLiteral("TIMEOUT");
                return false;
            }
            if (m_socket.waitForReadyRead(left < 0 ? 1000 : int(qMin<qint64>(left, 1000))))
                m_buffer += m_socket.readAll();
        }
        const int nl = m_buffer.indexOf('\n');
        const QJsonObject resp = QJsonDocument::fromJson(m_buffer.left(nl)).object();
        m_buffer.remove(0, nl + 1);
        if (resp.contains("error")) {
            const QJsonObject e = resp.value("error").toObject();
            const QString name = e.value("data").toObject().value("name").toString();
            if (errorName) *errorName = name;
            *error = QStringLiteral("%1: %2").arg(name, e.value("message").toString());
            return false;
        }
        *result = resp.value("result").toObject();
        return true;
    }

private:
    QLocalSocket m_socket;
    QByteArray   m_buffer;
    int          m_id = 0;
};

QString fmtLine(const QJsonObject &l)
{
    if (l.value("gapBefore").toBool())
        out(QStringLiteral("  ⋯"));
    const QString dir = l.value("dir").toString();
    QString tag = dir;
    if (dir == QLatin1String("TX") && l.value("origin").toString() != QLatin1String("user"))
        tag = QStringLiteral("TX:") + l.value("origin").toString();
    QString s = QStringLiteral("%1#%2 %3 %4 %5")
                    .arg(l.value("hit").toBool() ? QStringLiteral("> ") : QStringLiteral("  "))
                    .arg(qint64(l.value("seq").toDouble()))
                    .arg(l.value("time").toString(), tag.leftJustified(3), l.value("text").toString());
    if (l.value("cut").toInt() > 0)
        s += QStringLiteral(" …(+%1 chars)").arg(l.value("cut").toInt());
    if (l.value("partial").toBool())
        s += QStringLiteral(" …(line still open)");
    return s;
}

void printLines(const QJsonArray &lines)
{
    for (const QJsonValue &v : lines)
        out(fmtLine(v.toObject()));
}

int launch()
{
    if (!readRuntime().isEmpty()) {
        QString error;
        Connection probe;
        if (probe.open(QStringLiteral("launcher"), &error)) {
            out(QStringLiteral("UartX is already running"));
            return OK;
        }
    }
    const QString dir = QCoreApplication::applicationDirPath();
#if defined(Q_OS_WIN)
    const QString exe = QDir(dir).filePath(QStringLiteral("UartX.exe"));
#elif defined(Q_OS_MACOS)
    const QString exe = QDir(dir).filePath(QStringLiteral("UartX"));
#else
    const QString exe = QDir(dir).filePath(QStringLiteral("UartX"));
#endif
    if (!QFileInfo::exists(exe) || !QProcess::startDetached(exe, {})) {
        err(QStringLiteral("cannot start %1").arg(QDir::toNativeSeparators(exe)));
        return FAILED;
    }
    QDeadlineTimer deadline(15000);
    while (!deadline.hasExpired()) {
        QThread::msleep(250);
        QString error;
        Connection c;
        if (!readRuntime().isEmpty() && c.open(QStringLiteral("launcher"), &error)) {
            out(QStringLiteral("UartX started"));
            return OK;
        }
    }
    err(QStringLiteral("UartX did not come up within 15 s"));
    return NOT_RUNNING;
}

} // namespace

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
    QCoreApplication app(argc, argv);
    const Args a = parse(app.arguments());
    const bool json = a.flag("--json");

    if (a.positional.isEmpty() || a.flag("--help") || a.flag("-h")) {
        out(QString::fromLatin1(USAGE_TEXT));
        return a.positional.isEmpty() && !a.flag("--help") && !a.flag("-h") ? USAGE : OK;
    }

    const QString cmd = a.positional.value(0);
    if (cmd == QLatin1String("launch"))
        return launch();

    QString agent = a.opt("--agent", qEnvironmentVariable("UARTX_AGENT"));
    if (agent.isEmpty())
        agent = QStringLiteral("claude");

    QString method;
    QJsonObject params;
    qint64 timeoutMs = 15000;
    const auto queryFlags = [&a, &params] {
        if (a.flag("--regex")) params["regex"] = true;
        if (a.flag("--case"))  params["case"] = true;
    };

    if (cmd == QLatin1String("status") || cmd == QLatin1String("summary")) {
        method = cmd;
    } else if (cmd == QLatin1String("tail")) {
        method = cmd;
        params["n"] = a.opt("-n", QStringLiteral("20")).toInt();
        if (!a.opt("--match").isEmpty())
            params["match"] = a.opt("--match");
        queryFlags();
    } else if (cmd == QLatin1String("search") && a.positional.size() >= 2) {
        method = cmd;
        params["pattern"] = a.positional[1];
        params["before"] = a.opt("--before", QStringLiteral("0")).toInt();
        params["after"] = a.opt("--after", QStringLiteral("0")).toInt();
        params["max"] = a.opt("--max", QStringLiteral("20")).toInt();
        if (a.flag("--first")) params["first"] = true;
        if (!a.opt("--since").isEmpty()) params["since"] = a.opt("--since").toDouble();
        queryFlags();
    } else if (cmd == QLatin1String("wait") && a.positional.size() >= 2) {
        method = cmd;
        params["pattern"] = a.positional[1];
        params["before"] = a.opt("--before", QStringLiteral("10")).toInt();
        params["after"] = a.opt("--after", QStringLiteral("20")).toInt();
        const qint64 t = parseDurationMs(a.opt("--timeout"), 30000);
        params["timeoutMs"] = double(t);
        timeoutMs = t + 10000;
        queryFlags();
    } else if (cmd == QLatin1String("filter") && a.positional.value(1) == QLatin1String("add")
               && a.positional.size() >= 4) {
        method = QStringLiteral("filter.add");
        params["name"] = a.positional[2];
        params["pattern"] = a.positional[3];
        queryFlags();
    } else if (cmd == QLatin1String("filter") && a.positional.value(1) == QLatin1String("list")) {
        method = QStringLiteral("filter.list");
    } else if (cmd == QLatin1String("filter") && a.positional.value(1) == QLatin1String("rm")
               && a.positional.size() >= 3) {
        method = QStringLiteral("filter.rm");
        params["id"] = a.positional[2];
    } else if (cmd == QLatin1String("note") && a.positional.size() >= 2) {
        method = cmd;
        params["text"] = a.positional.mid(1).join(QLatin1Char(' '));
    } else if (cmd == QLatin1String("open")) {
        method = cmd;
        if (a.positional.size() >= 2) params["port"] = a.positional[1];
        if (!a.opt("--baud").isEmpty()) params["baud"] = a.opt("--baud");
        timeoutMs = 60000;    // a dialog may be up waiting for the user
    } else if (cmd == QLatin1String("close")) {
        method = cmd;
    } else {
        err(QStringLiteral("unknown or incomplete command: %1\n").arg(a.positional.join(QLatin1Char(' '))));
        out(QString::fromLatin1(USAGE_TEXT));
        return USAGE;
    }

    Connection c;
    QString error;
    if (!c.open(agent, &error)) {
        err(error);
        return NOT_RUNNING;
    }
    QJsonObject r;
    QString errorName;
    if (!c.call(method, params, timeoutMs, &r, &error, &errorName)) {
        err(QStringLiteral("error %1").arg(error));
        return errorName == QLatin1String("TIMEOUT") ? TIMED_OUT : FAILED;
    }
    if (json) {
        out(QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Indented)));
        return OK;
    }

    // ------------------------------------------------------------ compact text
    if (method == QLatin1String("status")) {
        const QJsonObject s = r.value("session").toObject();
        const QJsonObject log = s.value("log").toObject();
        out(QStringLiteral("UartX %1 | session %2 %3 | %4 @ %5 | %6 | %7 lines, %8 in memory (#%9-#%10)")
                .arg(r.value("version").toString(), s.value("id").toString(),
                     s.value("open").toBool() ? QStringLiteral("open") : QStringLiteral("closed"),
                     s.value("port").toString(), s.value("baud").toString(), s.value("status").toString())
                .arg(qint64(s.value("lines").toDouble()))
                .arg(s.value("inMemory").toInt())
                .arg(qint64(s.value("firstSeq").toDouble()))
                .arg(qint64(s.value("lastSeq").toDouble())));
        out(QStringLiteral("log: %1").arg(!log.value("enabled").toBool()  ? QStringLiteral("off")
                                          : log.value("stopped").toBool() ? QStringLiteral("STOPPED - ") + log.value("reason").toString()
                                          : log.value("created").toBool() ? log.value("dir").toString()
                                                                          : QStringLiteral("waiting for data")));
    } else if (method == QLatin1String("summary")) {
        out(QStringLiteral("%1 %2 | #%3-#%4 | RX %5, TX %6, SYS %7 | %8 lines in the last 10 s")
                .arg(r.value("port").toString(), r.value("open").toBool() ? QStringLiteral("open") : QStringLiteral("closed"))
                .arg(qint64(r.value("firstSeq").toDouble()))
                .arg(qint64(r.value("lastSeq").toDouble()))
                .arg(r.value("rx").toInt()).arg(r.value("tx").toInt()).arg(r.value("sys").toInt())
                .arg(qint64(r.value("linesLast10s").toDouble())));
        QStringList kinds;
        for (const QJsonValue &v : r.value("kinds").toArray())
            kinds << QStringLiteral("%1 %2").arg(v.toObject().value("tag").toString()).arg(v.toObject().value("count").toInt());
        out(QStringLiteral("line kinds (first word): %1%2").arg(kinds.join(QStringLiteral(", ")),
            r.value("kindsTotal").toInt() > kinds.size()
                ? QStringLiteral(", +%1 more kinds").arg(r.value("kindsTotal").toInt() - kinds.size()) : QString()));
        const QJsonArray ev = r.value("systemEvents").toArray();
        if (!ev.isEmpty()) {
            out(QStringLiteral("session events:"));
            printLines(ev);
        }
    } else if (method == QLatin1String("tail")) {
        printLines(r.value("lines").toArray());
    } else if (method == QLatin1String("search")) {
        printLines(r.value("lines").toArray());
        const int total = r.value("matches").toInt();
        const int omitted = r.value("omittedMatches").toInt();
        QString foot = QStringLiteral("-- %1 match%2 for %3 in the last %4 lines")
                           .arg(total).arg(total == 1 ? QString() : QStringLiteral("es"))
                           .arg(r.value("pattern").toString()).arg(r.value("searched").toInt());
        if (omitted)
            foot += QStringLiteral("; %1 more not sent (use --max, --first or a narrower pattern)").arg(omitted);
        if (r.value("omittedForBudget").toInt())
            foot += QStringLiteral("; %1 context lines cut to stay under 200").arg(r.value("omittedForBudget").toInt());
        out(foot);
    } else if (method == QLatin1String("wait")) {
        const bool sys = r.value("event").toString() == QLatin1String("sys");
        out(sys ? QStringLiteral("== session event at #%1").arg(qint64(r.value("hitSeq").toDouble()))
                : QStringLiteral("== %1 matched at #%2%3")
                      .arg(r.value("pattern").toString())
                      .arg(qint64(r.value("hitSeq").toDouble()))
                      .arg(r.value("matches").toInt() > 1
                               ? QStringLiteral(" (×%1 in this burst)").arg(r.value("matches").toInt())
                               : QString()));
        printLines(r.value("lines").toArray());
    } else if (method == QLatin1String("filter.add")) {
        out(QStringLiteral("view %1 “%2” opened in UartX: %3 matching lines so far")
                .arg(r.value("id").toString(), r.value("name").toString()).arg(r.value("shown").toInt()));
    } else if (method == QLatin1String("filter.list")) {
        const QJsonArray views = r.value("views").toArray();
        if (views.isEmpty())
            out(QStringLiteral("no agent views open (the user may have closed them)"));
        for (const QJsonValue &v : views) {
            const QJsonObject o = v.toObject();
            out(QStringLiteral("%1  %2  pattern=%3%4  shown=%5")
                    .arg(o.value("id").toString(), o.value("name").toString(), o.value("pattern").toString(),
                         o.value("regex").toBool() ? QStringLiteral(" (regex)") : QString())
                    .arg(o.value("shown").toInt()));
        }
    } else if (method == QLatin1String("filter.rm")) {
        out(QStringLiteral("closed view %1").arg(r.value("removed").toString()));
    } else if (method == QLatin1String("note")) {
        out(QStringLiteral("note shown to the user"));
    } else if (method == QLatin1String("open")) {
        out(QStringLiteral("%1 %2 at %3 baud (session %4)")
                .arg(r.value("already").toBool() ? QStringLiteral("already open:") : QStringLiteral("opened"),
                     r.value("port").toString(), r.value("baud").toString(), r.value("session").toString()));
    } else if (method == QLatin1String("close")) {
        out(QStringLiteral("closed %1").arg(r.value("closed").toString()));
    }
    return OK;
}
