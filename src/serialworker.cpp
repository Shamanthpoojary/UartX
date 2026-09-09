#include "serialworker.h"
#include "serialport.h"

namespace {

constexpr int READ_TIMEOUT_MS = 50;    // how long a read waits on an idle line
constexpr int READ_CHUNK      = 4096;

serial::Config toPlatformConfig(const SerialWorker::Config &cfg)
{
    serial::Config out;
    out.port     = cfg.port.toStdString();
    out.baud     = cfg.baud;
    out.dataBits = cfg.databits;
    out.parity   = cfg.parity.toStdString();
    out.stopBits = cfg.stopbits.toStdString();
    out.flow     = cfg.flow.toStdString();
    return out;
}

} // namespace

QStringList availableSerialPorts()
{
    QStringList out;
    for (const std::string &p : serial::Port::available())
        out.append(QString::fromStdString(p));
    return out;
}

QString serialPortDisplayName(const QString &port)
{
    return QString::fromStdString(serial::Port::displayName(port.toStdString()));
}

// ---------------------------------------------------------------------------

SerialWorker::SerialWorker(const Config &cfg, QObject *parent)
    : QThread(parent)
    , m_cfg(cfg)
{
}

SerialWorker::~SerialWorker()
{
    stop();
    wait(2000);
}

void SerialWorker::send(const QByteArray &data)
{
    QMutexLocker lock(&m_txMutex);
    m_txBuf.append(data);
}

void SerialWorker::stop()
{
    m_stop.store(true);
}

void SerialWorker::interruptibleSleep(int ms)
{
    const int step = 50;
    for (int elapsed = 0; elapsed < ms && !m_stop.load(); elapsed += step)
        msleep(static_cast<unsigned long>(qMin(step, ms - elapsed)));
}

void SerialWorker::run()
{
    serial::Port port;
    const serial::Config cfg = toPlatformConfig(m_cfg);

    bool announcedWait = false;
    QByteArray chunk;
    chunk.resize(READ_CHUNK);

    while (!m_stop.load()) {
        if (!port.isOpen()) {
            std::string error;
            if (port.open(cfg, READ_TIMEOUT_MS, &error)) {
                announcedWait = false;
                emit statusChanged(QStringLiteral("connected"));
            } else {
                if (!m_cfg.reconnect) {
                    emit fatalError(QString::fromStdString(error));
                    return;
                }
                if (!announcedWait) {
                    announcedWait = true;
                    emit statusChanged(QStringLiteral("waiting for port"));
                }
                interruptibleSleep(1000);
                continue;
            }
        }

        const int got = port.read(chunk.data(), READ_CHUNK);
        if (got < 0) {
            // Device rebooted or the USB-serial adapter blipped: drop the
            // handle and (if allowed) keep retrying.
            port.close();
            emit statusChanged(QStringLiteral("disconnected: %1").arg(m_cfg.port));
            emit statusChanged(QStringLiteral("reconnecting"));
            if (!m_cfg.reconnect) {
                emit fatalError(QStringLiteral("connection lost"));
                return;
            }
            interruptibleSleep(300);
            continue;
        }

        if (got > 0)
            emit dataReceived(QByteArray(chunk.constData(), got));

        QByteArray pending;
        {
            QMutexLocker lock(&m_txMutex);
            pending.swap(m_txBuf);
        }
        if (!pending.isEmpty())
            port.write(pending.constData(), pending.size());
    }

    port.close();
    emit statusChanged(QStringLiteral("disconnected"));
}
