#include "session.h"

#include <QMetaObject>
#include <QThread>
#include <atomic>

namespace {

/// How long a teardown waits for the reader thread to leave run(). Generous
/// on purpose: a blocked driver call is what makes the wait matter, and the
/// caller copes with the timeout rather than destroying a live thread.
constexpr int WORKER_STOP_TIMEOUT_MS = 5000;

std::atomic_int g_nextSessionNumber { 1 };

} // namespace

Session::Session(QObject *parent)
    : QObject(parent)
    , m_id(QStringLiteral("s%1").arg(g_nextSessionNumber.fetch_add(1)))
{
    Pipeline::registerMetaTypes();
}

Session::~Session()
{
    close();
}

void Session::open(const SerialWorker::Config &serial, PipelineConfig pipeline)
{
    close();
    pipeline.sessionId = m_id;
    pipeline.log.sessionId = m_id;

    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("pipeline-%1").arg(m_id));
    m_pipeline = new Pipeline;
    m_pipeline->moveToThread(m_thread);

    // Pipeline -> GUI: queued, because the session lives on the GUI thread.
    connect(m_pipeline, &Pipeline::batchReady, this, &Session::batchReady);
    connect(m_pipeline, &Pipeline::logStateChanged, this, [this](const LogState &state) {
        m_logState = state;
        emit logStateChanged(state);
    });
    m_thread->start();
    QMetaObject::invokeMethod(m_pipeline, "start", Qt::QueuedConnection,
                              Q_ARG(PipelineConfig, pipeline));

    m_worker = new SerialWorker(serial);
    // Reader -> pipeline directly, thread to thread: the bytes never pass
    // through the GUI thread. Both signals go to the same receiver, so a
    // status line can never overtake the data that arrived before it.
    connect(m_worker, &SerialWorker::dataReceived, m_pipeline, &Pipeline::onData);
    connect(m_worker, &SerialWorker::statusChanged, m_pipeline, &Pipeline::onStatus);
    connect(m_worker, &SerialWorker::statusChanged, this, &Session::statusChanged);
    connect(m_worker, &SerialWorker::fatalError, this, &Session::fatalError);
    m_worker->start();
}

void Session::stopWorker()
{
    if (!m_worker)
        return;

    SerialWorker *worker = m_worker;
    m_worker = nullptr;        // callers test m_worker; nothing may reach it now

    // The window has already moved on, so its status must not be overwritten
    // by the worker's last words. The pipeline still wants them: the final
    // "disconnected" belongs in the log, and finish() is queued behind it.
    worker->disconnect(this);
    worker->stop();

    if (worker->wait(WORKER_STOP_TIMEOUT_MS)) {
        delete worker;         // run() has returned, so this is safe
        return;
    }

    // It is still inside run(). Destroying it now would abort the process, so
    // hand it to Qt to delete itself once it really finishes. It owns nothing
    // shared, so outliving this call costs nothing -- but whatever it says
    // from here on would arrive after the log is closed, so cut it off.
    if (m_pipeline)
        worker->disconnect(m_pipeline);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
}

void Session::close()
{
    // Reader first: once it is gone no more bytes can arrive, so the
    // pipeline's finish() really is the end of the data.
    stopWorker();

    if (m_pipeline) {
        // Blocking: when this returns the open line has been written and the
        // log files are closed. Queued behind any data the reader posted
        // before it stopped, so none of that is lost either.
        QMetaObject::invokeMethod(m_pipeline, "finish", Qt::BlockingQueuedConnection);
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    // finish() moved the pipeline back to this thread, so it can go now.
    // Queued batches it already emitted still arrive: they hold their data by
    // shared pointer, not through the pipeline.
    delete m_pipeline;
    m_pipeline = nullptr;
}

void Session::send(const QByteArray &bytes, const QString &origin)
{
    if (!m_worker || bytes.isEmpty())
        return;
    m_worker->send(bytes);
    QMetaObject::invokeMethod(m_pipeline, "recordTx", Qt::QueuedConnection,
                              Q_ARG(QByteArray, bytes), Q_ARG(QString, origin),
                              Q_ARG(bool, false));
}

void Session::sendTyped(const QByteArray &bytes)
{
    if (!m_worker || bytes.isEmpty())
        return;
    m_worker->send(bytes);
    QMetaObject::invokeMethod(m_pipeline, "recordTx", Qt::QueuedConnection,
                              Q_ARG(QByteArray, bytes), Q_ARG(QString, Origin::User),
                              Q_ARG(bool, true));
}

void Session::recordSystem(const QString &message)
{
    if (m_pipeline)
        QMetaObject::invokeMethod(m_pipeline, "recordSystem", Qt::QueuedConnection,
                                  Q_ARG(QString, message));
}

void Session::setDisplay(const DisplayOptions &options, bool colorize, bool deviceColors)
{
    if (m_pipeline)
        QMetaObject::invokeMethod(m_pipeline, "setDisplay", Qt::QueuedConnection,
                                  Q_ARG(DisplayOptions, options), Q_ARG(bool, colorize),
                                  Q_ARG(bool, deviceColors));
}

void Session::setRules(const ColorRuleSet &rules)
{
    if (m_pipeline)
        QMetaObject::invokeMethod(m_pipeline, "setRules", Qt::QueuedConnection,
                                  Q_ARG(ColorRuleSet, rules));
}

void Session::batchRendered()
{
    if (m_pipeline)
        m_pipeline->markBatchRendered();
}

std::shared_ptr<const RecentRing> Session::ring() const
{
    return m_pipeline ? m_pipeline->ring() : nullptr;
}

const RecentRing *Session::recent() const
{
    return m_pipeline ? &m_pipeline->recent() : nullptr;
}
