#ifndef SERIALPORT_H
#define SERIALPORT_H

#include <string>
#include <vector>

/// Platform layer for talking to a serial port.
///
/// Deliberately free of Qt: this is the only part of UartX that differs
/// between operating systems, so keeping it to plain C++ lets each backend be
/// compiled and exercised on its own. Everything above it -- the reader
/// thread, the UI, the configuration -- is shared, unmodified, by every
/// platform.
///
/// Implementations:
///   serialport_win.cpp    Win32  (CreateFile / DCB / COMMTIMEOUTS)
///   serialport_posix.cpp  POSIX  (open / termios), used on Linux
namespace serial {

struct Config
{
    std::string port;                  ///< "COM5" or "/dev/ttyUSB0"
    int         baud     = 115200;
    int         dataBits = 8;          ///< 5..8
    std::string parity   = "None";     ///< None / Even / Odd / Mark / Space
    std::string stopBits = "1";        ///< 1 / 1.5 / 2
    std::string flow     = "None";     ///< None / XON/XOFF / RTS/CTS
};

/// One open serial port. Not copyable; safe to use from a single thread.
class Port
{
public:
    Port() = default;
    ~Port();
    Port(const Port &) = delete;
    Port &operator=(const Port &) = delete;

    /// Opens the port for exclusive use. `readTimeoutMs` bounds how long
    /// read() blocks when no data arrives. Returns false and fills `error`
    /// with a message fit to show the user.
    bool open(const Config &cfg, int readTimeoutMs, std::string *error);
    void close();
    bool isOpen() const;

    /// Reads whatever is available, waiting at most the configured timeout.
    /// Returns the byte count, 0 when nothing arrived in time, or -1 when the
    /// port has gone away (device unplugged, adapter reset).
    int read(char *buffer, int size);

    /// Returns bytes written, or -1 on failure.
    int write(const char *data, int size);

    /// Every serial port present on the machine, in a natural order.
    /// Windows: COM1, COM2, ... Linux: /dev/ttyUSB0, /dev/ttyACM0, ...
    static std::vector<std::string> available();

    /// A short, human-readable name for a port, for use in lists.
    /// Windows returns the port unchanged; Linux strips the "/dev/" prefix.
    static std::string displayName(const std::string &port);

private:
    // A raw OS handle: HANDLE on Windows, a file descriptor on POSIX.
    // Stored as an intptr-sized value so this header needs no platform
    // headers of its own.
    long long m_handle = -1;

    // Windows folds the timeout into COMMTIMEOUTS at open; POSIX passes it to
    // select() on every read.
    int m_readTimeoutMs = 50;
};

} // namespace serial

#endif // SERIALPORT_H
