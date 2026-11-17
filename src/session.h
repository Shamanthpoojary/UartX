#ifndef SESSION_H
#define SESSION_H

#include "pipeline.h"
#include "serialworker.h"

#include <QObject>
#include <QString>

class QThread;

/// One serial port, open until it is closed: the reader thread, the pipeline
/// thread, and the log they write. The model half of what used to live in
/// MainWindow; it owns no widgets, so a window -- or, later, a tab among
/// several -- just connects to its signals and draws.
///
/// Auto-reconnects happen inside the reader thread and stay in the same
/// session, with the same seq numbering and the same log folder.
class Session : public QObject
{
    Q_OBJECT

public:
    explicit Session(QObject *parent = nullptr);
    ~Session() override;

    /// "s1", "s2", ... unique for the life of the process.
    QString id() const { return m_id; }

    bool isOpen() const { return m_worker != nullptr; }

    /// Starts both threads. `pipeline.sessionId` is filled in here.
    void open(const SerialWorker::Config &serial, PipelineConfig pipeline);

    /// Stops the reader, lets the pipeline flush the open line and close the
    /// log, then stops the pipeline. Blocks until the log is closed, so the
    /// files are complete when this returns.
    void close();

    /// Transmits and records `bytes` as one TX line.
    void send(const QByteArray &bytes, const QString &origin = Origin::User);

    /// Transmits keystrokes typed into the terminal. They are recorded as one
    /// TX line per Enter, and not drawn again.
    void sendTyped(const QByteArray &bytes);

    /// Puts a SYS line in the record and on screen.
    void recordSystem(const QString &message);

    void setDisplay(const DisplayOptions &options, bool colorize, bool deviceColors);
    void setRules(const ColorRuleSet &rules);

    /// The GUI has drawn the last batch; the next one may come.
    void batchRendered();

    /// Recent records; valid while the session object lives.
    const RecentRing *recent() const;

    LogState logState() const { return m_logState; }

signals:
    void batchReady(DisplayBatchPtr batch);
    void statusChanged(const QString &status);
    /// The reader thread gave up (reconnect off and the port failed).
    void fatalError(const QString &message);
    void logStateChanged(const LogState &state);

private:
    void stopWorker();

    QString       m_id;
    SerialWorker *m_worker   = nullptr;
    QThread      *m_thread   = nullptr;
    Pipeline     *m_pipeline = nullptr;
    LogState      m_logState;
};

#endif // SESSION_H
