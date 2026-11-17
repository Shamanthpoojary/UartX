#ifndef PIPELINE_H
#define PIPELINE_H

#include "colorrules.h"
#include "lineassembler.h"
#include "lineformat.h"
#include "linerecord.h"
#include "logstore.h"
#include "recentring.h"
#include "textdecoder.h"

#include <QObject>
#include <QSharedPointer>
#include <QVector>
#include <atomic>
#include <memory>

class QTimer;

/// What a view is asked to do with one item of a batch.
struct DisplayItem
{
    enum class Kind { Line, System, Gap };

    Kind        kind = Kind::Line;
    DisplayLine line;               ///< what the terminal draws (Line), or the message
    bool        newline = true;     ///< false while the line is still open (Partial)
    /// For filter views: the complete line, once it is complete. For a line
    /// that arrived in one piece this is the same as `line`; for a line that
    /// was shown early by the quiet timer it is the whole line, delivered
    /// once, when its end arrives. Empty `wholeValid` = nothing for filters.
    DisplayLine whole;
    bool        wholeValid = false;
};

/// A batch of display work, handed to the GUI once per poll interval.
struct DisplayBatch
{
    QVector<DisplayItem> items;
    quint64 lastSeq     = 0;   ///< highest seq assigned so far
    quint64 totalLines  = 0;   ///< RX/TX/SYS records in the session
    qint64  rxBytes     = 0;
};
using DisplayBatchPtr = QSharedPointer<DisplayBatch>;

/// Log status, for the status bar and banners.
struct LogState
{
    bool    enabled = false;   ///< the user wants a log
    bool    created = false;   ///< files exist on disk
    bool    stopped = false;   ///< writing failed and has stopped
    QString reason;            ///< why it stopped
    QString sessionDir;
    QString runDir;
};

/// Everything the pipeline needs to know to start.
struct PipelineConfig
{
    QString         sessionId;
    Framing         framing;
    QString         encoding = QStringLiteral("Auto");
    DisplayOptions  display;
    ColorRuleSet    rules;
    bool            colorize     = true;
    bool            deviceColors = true;
    bool            logEnabled   = false;
    LogStoreOptions log;
    /// Display items held for a GUI that has fallen behind. Past this the
    /// oldest are dropped from the display -- never from the log -- and a
    /// marker says how many.
    int             maxPendingDisplay = 8000;
    int             pollIntervalMs = 40;
};

/// The session's data path, on a thread of its own.
///
///   serial thread --bytes--> Pipeline: raw.log, LineAssembler, decode,
///                            seq, lines.log, RecentRing, rule matching,
///                            display formatting --batch every 40 ms--> GUI
///
/// The GUI thread only paints what arrives in a batch, so a device spraying
/// at 2 Mbaud costs the GUI the same as one at 9600: whatever it can draw in
/// one interval. If it falls behind, the display coalesces (with a visible
/// marker) while the log, the ring and the matcher keep every record.
///
/// All public slots are meant to be invoked across threads (queued).
class Pipeline : public QObject
{
    Q_OBJECT

public:
    explicit Pipeline(int ringCapacity = 50000, QObject *parent = nullptr);
    ~Pipeline() override;

    /// Registers the types the queued slots and signals carry. Idempotent;
    /// call before the first cross-thread invocation.
    static void registerMetaTypes();

    /// Readable from any thread.
    const RecentRing &recent() const { return *m_ring; }

    /// The GUI calls this once a batch is drawn, so the next may be sent.
    void markBatchRendered() { m_batchInFlight.store(false); }

    /// Pure function used by the pipeline and the tests: the display form of
    /// a record whose text is already decoded.
    static DisplayLine makeDisplayLine(const LineRecord &record, const QString &text,
                                       const DisplayOptions &options, const ColorRuleSet &rules,
                                       bool colorize, bool deviceColors);

public slots:
    void start(const PipelineConfig &config);
    void onData(const QByteArray &data, qint64 wallMs, qint64 monoNs);
    /// A status change from the serial thread, recorded as a SYS line.
    void onStatus(const QString &status);
    /// Something UartX itself wants on record (and on screen).
    void recordSystem(const QString &message);
    /// Bytes that were transmitted. Typed keystrokes are collected up to
    /// Enter and recorded as one line, not shown again.
    void recordTx(const QByteArray &bytes, const QString &origin, bool typed);

    void setDisplay(const DisplayOptions &options, bool colorize, bool deviceColors);
    void setRules(const ColorRuleSet &rules);

    /// Flushes the open line, closes the log and stops the timer. Called
    /// (blocking) when the session closes.
    void finish();

signals:
    void batchReady(DisplayBatchPtr batch);
    void logStateChanged(const LogState &state);

private:
    void poll();
    void handle(const AssembledLine &line, qint64 wallMs, qint64 monoNs);
    LineRecord makeRecord(Direction dir, const QString &origin, const QByteArray &bytes,
                          quint8 flags, qint64 wallMs, qint64 monoNs);
    void commit(const LineRecord &record);
    void queueLine(const LineRecord &record, const QString &text, bool newline,
                   const DisplayLine *whole);
    void queueSystem(const QString &message);
    void queue(DisplayItem &&item);
    void flushTyped(qint64 wallMs, qint64 monoNs);
    void sendBatch();
    void publishLogState();

    PipelineConfig m_cfg;
    LineAssembler  m_assembler;
    TextDecoder    m_decoder;
    LogStore       m_log;
    std::unique_ptr<RecentRing> m_ring;
    QTimer        *m_timer = nullptr;

    quint64 m_nextSeq = 1;
    quint64 m_total   = 0;
    qint64  m_rxBytes = 0;
    qint64  m_lastWallMs = 0;
    qint64  m_lastMonoNs = 0;       ///< wire time of the last chunk
    qint64  m_lastFeedRealNs = 0;   ///< steady clock when it was processed

    QVector<DisplayItem> m_pending;
    quint64 m_dropped = 0;
    std::atomic_bool m_batchInFlight { false };

    QByteArray m_typed;           ///< keystrokes since the last Enter
    QString    m_typedOrigin;
    LogState   m_lastLogState;
    bool       m_logStatePublished = false;
    bool       m_finished = false;
};

Q_DECLARE_METATYPE(DisplayBatchPtr)
Q_DECLARE_METATYPE(LogState)
Q_DECLARE_METATYPE(PipelineConfig)


#endif // PIPELINE_H
