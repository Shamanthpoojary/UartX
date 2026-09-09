#ifndef SERIALWORKER_H
#define SERIALWORKER_H

#include <QThread>
#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <atomic>

/// Live enumeration of the machine's serial ports.
/// Windows: COM1, COM3, ...   Linux: /dev/ttyUSB0, /dev/ttyACM0, ...
QStringList availableSerialPorts();

/// A short label for a port, for lists and status text: unchanged on Windows,
/// without the "/dev/" prefix on Linux.
QString serialPortDisplayName(const QString &port);

/// Reader/writer thread. Entirely platform-independent: the operating-system
/// differences live behind serial::Port (see serialport.h).
class SerialWorker : public QThread
{
    Q_OBJECT

public:
    struct Config
    {
        QString port;
        int     baud      = 115200;
        int     databits  = 8;                          // 5..8
        QString parity    = QStringLiteral("None");     // None/Even/Odd/Mark/Space
        QString stopbits  = QStringLiteral("1");        // 1 / 1.5 / 2
        QString flow      = QStringLiteral("None");     // None / XON/XOFF / RTS/CTS
        bool    reconnect = true;
    };

    explicit SerialWorker(const Config &cfg, QObject *parent = nullptr);
    ~SerialWorker() override;

    /// Queues bytes for transmission (safe to call from any thread).
    void send(const QByteArray &data);

    /// Asks the thread to shut down; does not block.
    void stop();

signals:
    void dataReceived(const QByteArray &data);
    void statusChanged(const QString &status);
    void fatalError(const QString &message);

protected:
    void run() override;

private:
    void interruptibleSleep(int ms);

    Config           m_cfg;
    QMutex           m_txMutex;
    QByteArray       m_txBuf;
    std::atomic_bool m_stop { false };
};

#endif // SERIALWORKER_H
