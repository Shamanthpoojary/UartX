#include "pipeline.h"
#include "appconstants.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>
#include <QThread>

#include <chrono>

namespace {

qint64 monoNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// A transmitted command carries its line ending in the record, because that
/// is what went on the wire; on screen it would only add an empty line.
QByteArray withoutTrailingEol(QByteArray bytes)
{
    while (bytes.endsWith('\r') || bytes.endsWith('\n'))
        bytes.chop(1);
    return bytes;
}

} // namespace

void Pipeline::registerMetaTypes()
{
    static const bool done = [] {
        qRegisterMetaType<DisplayBatchPtr>("DisplayBatchPtr");
        qRegisterMetaType<LogState>("LogState");
        qRegisterMetaType<PipelineConfig>("PipelineConfig");
        qRegisterMetaType<DisplayOptions>("DisplayOptions");
        qRegisterMetaType<ColorRuleSet>("ColorRuleSet");
        return true;
    }();
    Q_UNUSED(done);
}

Pipeline::Pipeline(int ringCapacity, QObject *parent)
    : QObject(parent)
    , m_ring(std::make_shared<RecentRing>(ringCapacity))
{
    registerMetaTypes();
}

Pipeline::~Pipeline()
{
    m_log.close();
}

DisplayLine Pipeline::makeDisplayLine(const LineRecord &record, const QString &text,
                                      const DisplayOptions &options, const ColorRuleSet &rules,
                                      bool colorize, bool deviceColors)
{
    const bool isTx = (record.dir == Direction::Tx);
    const QByteArray bytes = isTx ? withoutTrailingEol(record.bytes) : record.bytes;
    QString shown = text;
    if (isTx) {
        while (shown.endsWith(QLatin1Char('\r')) || shown.endsWith(QLatin1Char('\n')))
            shown.chop(1);
    }

    DisplayLine line;
    line.seq    = record.seq;
    line.isTx   = isTx;
    line.raw    = stripEscapeSequences(shown);
    line.prefix = LineFormat::prefix(options, isTx,
                                     options.timestamps ? QDateTime::fromMSecsSinceEpoch(record.wallMs)
                                                        : QDateTime());
    line.body   = LineFormat::body(options, shown, bytes);

    // Rules match the decoded text with escape sequences removed, so a rule
    // keeps working when the view is switched to hex, and a device that
    // colours its own "ERROR" still matches a rule for "ERROR".
    const ColorRule *rule = rules.firstMatch(line.raw);
    line.colorRule = rule ? rule->name : QString();

    if (isTx)
        line.color = QColor(App::TERM_TX);
    else if (rule && colorize)
        line.color = rule->color;
    else
        line.color = QColor(App::TERM_FG);

    // The device's own colours are kept only where nothing else has claimed
    // the line: a matching colour rule is the user saying what matters, and
    // that outranks the firmware. The other views show bytes or escapes, not
    // colour, so they take none of this either.
    const bool ruleWins = rule && colorize;
    if (!isTx && !ruleWins && deviceColors && options.mode == QLatin1String("ASCII")) {
        QVector<AnsiSpan> spans = parseAnsiSpans(shown);
        // One uncoloured run is just a plain line; let the simple path draw it.
        if (spans.size() > 1 || (spans.size() == 1 && spans.first().color >= 0))
            line.spans = spans;
    }
    return line;
}

void Pipeline::start(const PipelineConfig &config)
{
    m_cfg = config;
    m_assembler.setFraming(config.framing);
    m_assembler.reset();
    m_decoder.setEncoding(config.encoding);
    if (config.logEnabled)
        m_log.open(config.log);

    // Created here, not in the constructor, so the timer belongs to the
    // pipeline thread it fires on.
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Pipeline::poll);
    m_timer->start(qMax(5, config.pollIntervalMs));
    publishLogState();
}

LineRecord Pipeline::makeRecord(Direction dir, const QString &origin, const QByteArray &bytes,
                                quint8 flags, qint64 wallMs, qint64 monoNs)
{
    LineRecord r;
    r.seq    = m_nextSeq++;
    r.wallMs = wallMs;
    r.monoNs = monoNs;
    r.dir    = dir;
    r.flags  = flags;
    r.origin = origin;
    r.bytes  = bytes;
    ++m_total;
    return r;
}

void Pipeline::commit(const LineRecord &record)
{
    m_log.appendRecord(record);
    m_ring->append(record);
}

void Pipeline::onData(const QByteArray &data, qint64 wallMs, qint64 monoNs)
{
    if (m_finished)
        return;
    m_rxBytes += data.size();
    m_lastWallMs = wallMs;
    m_lastMonoNs = monoNs;

    // raw.log gets the bytes before anything interprets them: it is the one
    // file that has to match the wire exactly, whatever the framing or
    // encoding settings say.
    m_log.appendRaw(data);

    m_lastFeedRealNs = monoNowNs();
    const QVector<AssembledLine> lines = m_assembler.feed(data, monoNs / 1000000);
    for (const AssembledLine &line : lines)
        handle(line, wallMs, monoNs);
}

void Pipeline::handle(const AssembledLine &line, qint64 wallMs, qint64 monoNs)
{
    const LineRecord record = makeRecord(Direction::Rx, Origin::Device, line.bytes,
                                         line.flags, wallMs, monoNs);
    commit(record);

    const QString text = m_decoder.decode(line.bytes);
    const bool open = record.has(LineFlag::Partial);
    if (open) {
        queueLine(record, text, false, nullptr);
    } else if (record.has(LineFlag::Continuation)) {
        // The line was shown early; filter views get it now, complete, once.
        LineRecord whole = record;
        whole.bytes = line.whole;
        const DisplayLine wholeLine =
            makeDisplayLine(whole, m_decoder.decode(line.whole), m_cfg.display, m_cfg.rules,
                            m_cfg.colorize, m_cfg.deviceColors);
        queueLine(record, text, true, &wholeLine);
    } else {
        queueLine(record, text, true, nullptr);
    }
}

void Pipeline::queueLine(const LineRecord &record, const QString &text, bool newline,
                         const DisplayLine *whole)
{
    DisplayItem item;
    item.kind = DisplayItem::Kind::Line;
    item.line = makeDisplayLine(record, text, m_cfg.display, m_cfg.rules, m_cfg.colorize,
                                m_cfg.deviceColors);
    item.newline = newline;
    if (whole) {
        item.whole = *whole;
        item.wholeValid = true;
    } else if (newline) {
        item.whole = item.line;
        item.wholeValid = true;
    }
    queue(std::move(item));
}

void Pipeline::queueSystem(const QString &message)
{
    DisplayItem item;
    item.kind = DisplayItem::Kind::System;
    item.line.raw = message;
    item.line.body = message;
    queue(std::move(item));
}

void Pipeline::queue(DisplayItem &&item)
{
    m_pending.append(std::move(item));

    // The GUI has fallen behind. The oldest undrawn lines would scroll out of
    // the capped terminal anyway, so drop them from the display only -- the
    // log, the ring and the matcher already have them. Trimmed in chunks so
    // the cost of removing from the front is paid rarely.
    const int max = qMax(100, m_cfg.maxPendingDisplay);
    if (m_pending.size() > max + max / 4) {
        const int drop = m_pending.size() - max;
        m_pending.remove(0, drop);
        m_dropped += quint64(drop);
    }
}

void Pipeline::onStatus(const QString &status)
{
    recordSystem(status);
}

void Pipeline::recordSystem(const QString &message)
{
    if (m_finished)
        return;
    const LineRecord r = makeRecord(Direction::Sys, Origin::System, message.toUtf8(), 0,
                                    QDateTime::currentMSecsSinceEpoch(), monoNowNs());
    commit(r);
    queueSystem(message);
}

void Pipeline::recordTx(const QByteArray &bytes, const QString &origin, bool typed)
{
    if (m_finished || bytes.isEmpty())
        return;

    if (typed) {
        m_typedOrigin = origin;
        for (const char c : bytes) {
            if (c == '\r' || c == '\n') {
                flushTyped(QDateTime::currentMSecsSinceEpoch(), monoNowNs());
                continue;
            }
            if (m_typed.size() < 4096)
                m_typed.append(c);
        }
        return;
    }

    const LineRecord r = makeRecord(Direction::Tx, origin, bytes, 0,
                                    QDateTime::currentMSecsSinceEpoch(), monoNowNs());
    commit(r);
    queueLine(r, m_decoder.decode(bytes), true, nullptr);
}

void Pipeline::flushTyped(qint64 wallMs, qint64 monoNs)
{
    if (m_typed.isEmpty())
        return;
    // Recorded so the log shows what was sent, but not drawn: local echo (if
    // on) already put the characters on screen as they were typed.
    const LineRecord r = makeRecord(Direction::Tx, m_typedOrigin, m_typed, LineFlag::Typed,
                                    wallMs, monoNs);
    commit(r);
    m_typed.clear();
}

void Pipeline::setDisplay(const DisplayOptions &options, bool colorize, bool deviceColors)
{
    m_cfg.display = options;
    m_cfg.colorize = colorize;
    m_cfg.deviceColors = deviceColors;
}

void Pipeline::setRules(const ColorRuleSet &rules)
{
    m_cfg.rules = rules;
}

void Pipeline::poll()
{
    // "Quiet" has to mean the wire was quiet, not that this thread was busy.
    // If the pipeline is running behind, data from the wire is already queued
    // for it, and comparing the last wire timestamp with the clock would cut
    // lines whose ends are sitting in that queue. So the assembler's clock is
    // the last wire time plus however long this thread has been idle since.
    const qint64 idleNs = monoNowNs() - m_lastFeedRealNs;
    const QVector<AssembledLine> due = m_assembler.poll((m_lastMonoNs + idleNs) / 1000000);
    for (const AssembledLine &line : due)
        handle(line, QDateTime::currentMSecsSinceEpoch(), monoNowNs());

    m_log.flush();
    publishLogState();
    sendBatch();
}

void Pipeline::sendBatch()
{
    if (m_batchInFlight.load())
        return;    // the GUI is still drawing the last one; keep collecting

    if (m_pending.isEmpty() && m_dropped == 0)
        return;

    auto batch = DisplayBatchPtr::create();
    if (m_dropped) {
        DisplayItem gap;
        gap.kind = DisplayItem::Kind::Gap;
        const QString where = m_log.isArmed() && !m_log.isStopped()
                                  ? QStringLiteral("all saved to log")
                                  : QStringLiteral("all kept in memory; log saving is off");
        gap.line.body = QStringLiteral("… %1 lines not displayed (%2) …")
                            .arg(m_dropped)
                            .arg(where);
        gap.line.raw = gap.line.body;
        batch->items.append(gap);
        m_dropped = 0;
    }
    batch->items += std::move(m_pending);
    m_pending.clear();
    batch->lastSeq    = m_nextSeq - 1;
    batch->totalLines = m_total;
    batch->rxBytes    = m_rxBytes;

    m_batchInFlight.store(true);
    emit batchReady(batch);
}

void Pipeline::publishLogState()
{
    if (m_log.takeStopEvent()) {
        // Recorded in the ring and on screen; the log itself can no longer
        // take it, which is the point.
        recordSystem(QStringLiteral("log stopped: %1 - the session continues, but nothing more is saved")
                         .arg(m_log.stopReason()));
    }

    LogState state;
    state.enabled    = m_cfg.logEnabled;
    state.created    = m_log.hasCreated();
    state.stopped    = m_log.isStopped();
    state.reason     = m_log.stopReason();
    state.sessionDir = m_log.sessionDir();
    state.runDir     = m_log.runDir();

    // Polled every interval; only a change is worth a signal.
    if (m_logStatePublished && state.enabled == m_lastLogState.enabled
        && state.created == m_lastLogState.created && state.stopped == m_lastLogState.stopped
        && state.runDir == m_lastLogState.runDir)
        return;
    m_lastLogState = state;
    m_logStatePublished = true;
    emit logStateChanged(state);
}

void Pipeline::finish()
{
    if (m_finished)
        return;

    // Whatever is still buffered is the end of the session's data; show it
    // and log it rather than lose it.
    const QVector<AssembledLine> rest = m_assembler.drain();
    for (const AssembledLine &line : rest)
        handle(line, QDateTime::currentMSecsSinceEpoch(), monoNowNs());
    flushTyped(QDateTime::currentMSecsSinceEpoch(), monoNowNs());

    if (m_timer)
        m_timer->stop();
    m_log.close();
    m_finished = true;

    // The last batch goes out even if the GUI has not acknowledged the one
    // before: nothing else will ever be sent after it.
    m_batchInFlight.store(false);
    sendBatch();
    publishLogState();

    // Hand the object back to the main thread so it can be deleted there
    // after this thread has stopped.
    if (QCoreApplication::instance())
        moveToThread(QCoreApplication::instance()->thread());
}
