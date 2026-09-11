#include "serialport.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <set>

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#ifdef __linux__
#  include <linux/serial.h>
// Custom (non-standard) baud rates go through TCSETS2, whose struct lives in
// <asm/termbits.h>. That header cannot be included alongside <termios.h>
// without redefinition errors, so the few pieces needed are declared here.
#  ifndef TCGETS2
#    define TCGETS2 _IOR('T', 0x2A, struct termios2)
#    define TCSETS2 _IOW('T', 0x2B, struct termios2)
#  endif
#  ifndef BOTHER
#    define BOTHER 0010000
#  endif
struct termios2
{
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[19];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};
#endif

// Mark/space parity is a Linux extension and is not always exposed by the
// libc headers.
#ifndef CMSPAR
#  define CMSPAR 010000000000
#endif

namespace serial {
namespace {

int fd(long long v) { return static_cast<int>(v); }

std::string errnoText(int e)
{
    char buf[256] = {0};
    // Portable enough across glibc/musl: strerror_r has two flavours, so use
    // the simple thread-unsafe call only as a fallback.
#if defined(__GLIBC__) && (_GNU_SOURCE)
    return std::string(strerror_r(e, buf, sizeof(buf)));
#else
    if (strerror_r(e, buf, sizeof(buf)) == 0)
        return std::string(buf);
    return "errno " + std::to_string(e);
#endif
}

/// Maps a rate to a Bxxx constant, or B0 when the rate is not one of the
/// standard ones (the caller then falls back to a custom-divisor ioctl).
speed_t standardSpeed(int baud)
{
    switch (baud) {
    case 50:      return B50;
    case 75:      return B75;
    case 110:     return B110;
    case 134:     return B134;
    case 150:     return B150;
    case 200:     return B200;
    case 300:     return B300;
    case 600:     return B600;
    case 1200:    return B1200;
    case 1800:    return B1800;
    case 2400:    return B2400;
    case 4800:    return B4800;
    case 9600:    return B9600;
    case 19200:   return B19200;
    case 38400:   return B38400;
    case 57600:   return B57600;
    case 115200:  return B115200;
    case 230400:  return B230400;
#ifdef B460800
    case 460800:  return B460800;
#endif
#ifdef B500000
    case 500000:  return B500000;
#endif
#ifdef B576000
    case 576000:  return B576000;
#endif
#ifdef B921600
    case 921600:  return B921600;
#endif
#ifdef B1000000
    case 1000000: return B1000000;
#endif
#ifdef B1152000
    case 1152000: return B1152000;
#endif
#ifdef B1500000
    case 1500000: return B1500000;
#endif
#ifdef B2000000
    case 2000000: return B2000000;
#endif
#ifdef B2500000
    case 2500000: return B2500000;
#endif
#ifdef B3000000
    case 3000000: return B3000000;
#endif
    default:      return B0;
    }
}

#ifdef __linux__
/// Applies an arbitrary rate the standard table does not cover.
bool applyCustomBaud(int handle, int baud)
{
    struct termios2 t2;
    if (::ioctl(handle, TCGETS2, &t2) != 0)
        return false;
    t2.c_cflag &= ~static_cast<tcflag_t>(CBAUD);
    t2.c_cflag |= BOTHER;
    t2.c_ispeed = static_cast<speed_t>(baud);
    t2.c_ospeed = static_cast<speed_t>(baud);
    return ::ioctl(handle, TCSETS2, &t2) == 0;
}
#endif

int trailingNumber(const std::string &s)
{
    size_t i = s.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1])))
        --i;
    if (i == s.size())
        return -1;
    return std::stoi(s.substr(i));
}

std::string stem(const std::string &s)
{
    size_t i = s.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1])))
        --i;
    return s.substr(0, i);
}

bool pathExists(const std::string &p)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

/// A legacy 8250 port only exists if the kernel gave it a real type; without
/// this filter every machine reports ttyS0..ttyS31 whether or not the
/// hardware is there.
bool legacyPortIsReal(const std::string &name)
{
    std::ifstream in("/sys/class/tty/" + name + "/type");
    if (!in)
        return false;
    int type = 0;
    in >> type;
    return type != 0;   // 0 == PORT_UNKNOWN
}

/// True for the tty names that represent a serial device a user can open.
bool looksLikeSerial(const std::string &name)
{
    static const char *prefixes[] = {
        "ttyUSB",   // USB-serial bridges (FTDI, CP210x, CH34x, PL2303)
        "ttyACM",   // USB CDC-ACM devices (Arduino, ST-Link, many MCUs)
        "ttyAMA",   // PL011 on Raspberry Pi and other ARM boards
        "ttyXRUSB", // Exar USB serial
        "rfcomm",   // Bluetooth serial
        "ttyMI",
    };
    for (const char *p : prefixes) {
        if (name.rfind(p, 0) == 0)
            return true;
    }
    if (name.rfind("ttyS", 0) == 0)
        return legacyPortIsReal(name);
    return false;
}

} // namespace

Port::~Port()
{
    close();
}

bool Port::isOpen() const
{
    return m_handle >= 0;
}

bool Port::open(const Config &cfg, int readTimeoutMs, std::string *error)
{
    close();
    m_readTimeoutMs = readTimeoutMs;

    // O_NONBLOCK keeps the open from hanging on a port whose modem lines are
    // not asserted; blocking behaviour is restored immediately afterwards
    // because reads are driven by select().
    int handle = ::open(cfg.port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (handle < 0) {
        const int e = errno;
        if (error) {
            *error = "could not open " + cfg.port + ": " + errnoText(e);
            if (e == EACCES) {
                *error += "\n\nYou do not have permission to use this port. "
                          "Add your user to the group that owns it (usually "
                          "'dialout' on Debian/Ubuntu, 'uucp' on Arch/Fedora):\n"
                          "    sudo usermod -a -G dialout $USER\n"
                          "then log out and back in.";
            } else if (e == EBUSY) {
                *error += "\n\nAnother program is already using this port.";
            }
        }
        return false;
    }

    // Ask the kernel to refuse further opens, matching the exclusive access
    // the Windows backend gets from CreateFile with no sharing.
    ::ioctl(handle, TIOCEXCL);

    int flags = ::fcntl(handle, F_GETFL, 0);
    ::fcntl(handle, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tio;
    if (::tcgetattr(handle, &tio) != 0) {
        if (error)
            *error = "not a serial device: " + cfg.port + " (" + errnoText(errno) + ")";
        ::close(handle);
        return false;
    }

    cfmakeraw(&tio);                       // 8-bit clean, no echo, no translation
    tio.c_cflag |= (CLOCAL | CREAD);       // ignore modem lines, enable receive

    // --- data bits ---
    tio.c_cflag &= ~static_cast<tcflag_t>(CSIZE);
    switch (cfg.dataBits) {
    case 5:  tio.c_cflag |= CS5; break;
    case 6:  tio.c_cflag |= CS6; break;
    case 7:  tio.c_cflag |= CS7; break;
    default: tio.c_cflag |= CS8; break;
    }

    // --- parity ---
    tio.c_cflag &= ~static_cast<tcflag_t>(PARENB | PARODD | CMSPAR);
    if (cfg.parity == "Even") {
        tio.c_cflag |= PARENB;
    } else if (cfg.parity == "Odd") {
        tio.c_cflag |= PARENB | PARODD;
    } else if (cfg.parity == "Mark") {
        tio.c_cflag |= PARENB | CMSPAR | PARODD;
    } else if (cfg.parity == "Space") {
        tio.c_cflag |= PARENB | CMSPAR;
    }

    // --- stop bits ---
    if (cfg.stopBits == "1.5") {
        // POSIX has no 1.5-stop-bit setting; only 1 and 2 can be expressed.
        if (error) {
            *error = "1.5 stop bits is not supported on Linux. "
                     "Choose 1 or 2 under Settings > Serial port.";
        }
        ::close(handle);
        return false;
    }
    if (cfg.stopBits == "2")
        tio.c_cflag |= CSTOPB;
    else
        tio.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);

    // --- flow control ---
    tio.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);
#ifdef CRTSCTS
    tio.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
#endif
    if (cfg.flow == "XON/XOFF") {
        tio.c_iflag |= (IXON | IXOFF);
        tio.c_cc[VSTART] = 0x11;
        tio.c_cc[VSTOP]  = 0x13;
    }
#ifdef CRTSCTS
    else if (cfg.flow == "RTS/CTS") {
        tio.c_cflag |= CRTSCTS;
    }
#endif

    // Reads are governed by select(), so the driver itself never blocks.
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    // --- baud rate ---
    const speed_t speed = standardSpeed(cfg.baud);
    bool needCustomBaud = false;
    if (speed != B0) {
        cfsetispeed(&tio, speed);
        cfsetospeed(&tio, speed);
    } else {
        needCustomBaud = true;               // applied after tcsetattr
        cfsetispeed(&tio, B38400);
        cfsetospeed(&tio, B38400);
    }

    if (::tcsetattr(handle, TCSANOW, &tio) != 0) {
        if (error)
            *error = "invalid serial settings for " + cfg.port + ": " + errnoText(errno);
        ::close(handle);
        return false;
    }

    if (needCustomBaud) {
#ifdef __linux__
        if (!applyCustomBaud(handle, cfg.baud)) {
            if (error) {
                *error = "the driver for " + cfg.port + " does not support "
                         + std::to_string(cfg.baud) + " baud.";
            }
            ::close(handle);
            return false;
        }
#else
        if (error)
            *error = std::to_string(cfg.baud) + " baud is not a supported rate.";
        ::close(handle);
        return false;
#endif
    }

    ::tcflush(handle, TCIOFLUSH);
    m_handle = handle;
    return true;
}

void Port::close()
{
    if (m_handle >= 0) {
        ::close(fd(m_handle));
        m_handle = -1;
    }
}

int Port::read(char *buffer, int size)
{
    if (!isOpen())
        return -1;

    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd(m_handle), &set);

    struct timeval tv;
    tv.tv_sec  = m_readTimeoutMs / 1000;
    tv.tv_usec = (m_readTimeoutMs % 1000) * 1000;

    const int ready = ::select(fd(m_handle) + 1, &set, nullptr, nullptr, &tv);
    if (ready < 0)
        return (errno == EINTR) ? 0 : -1;
    if (ready == 0)
        return 0;                       // nothing arrived in time

    const ssize_t got = ::read(fd(m_handle), buffer, static_cast<size_t>(size));
    if (got > 0)
        return static_cast<int>(got);
    if (got == 0)
        return -1;                      // hangup: the device went away
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return 0;
    return -1;                          // EIO / ENXIO: unplugged
}

int Port::write(const char *data, int size)
{
    if (!isOpen())
        return -1;

    // The descriptor is in blocking mode, so write() on a device that has
    // stopped accepting data -- hardware flow control deasserted, or an
    // adapter pulled mid-write -- parks this thread indefinitely, with no way
    // for a stop request to reach the reader loop. Waiting for writability
    // first bounds that: select() reports when the driver has room, and the
    // deadline gives up instead of blocking forever.
    constexpr int WAIT_SLICE_MS   = 100;
    constexpr int STALL_LIMIT_MS  = 2000;

    int total     = 0;
    int stalledMs = 0;

    while (total < size) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd(m_handle), &writable);

        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = WAIT_SLICE_MS * 1000;

        const int ready = ::select(fd(m_handle) + 1, nullptr, &writable, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ready == 0) {
            stalledMs += WAIT_SLICE_MS;
            if (stalledMs >= STALL_LIMIT_MS)
                return total;          // waited long enough; report what went out
            continue;
        }

        const ssize_t n = ::write(fd(m_handle), data + total, static_cast<size_t>(size - total));
        if (n > 0) {
            total += static_cast<int>(n);
            stalledMs = 0;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            continue;
        return -1;                     // EIO / ENXIO: the device is gone
    }
    return total;
}

std::vector<std::string> Port::available()
{
    std::set<std::string> found;

    // /sys/class/tty lists every tty the kernel knows about, which is the
    // reliable way to tell a real serial device from a console or pty.
    if (DIR *dir = ::opendir("/sys/class/tty")) {
        while (struct dirent *entry = ::readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..")
                continue;
            if (!looksLikeSerial(name))
                continue;
            const std::string devPath = "/dev/" + name;
            if (pathExists(devPath))
                found.insert(devPath);
        }
        ::closedir(dir);
    }

    // Some containers and minimal images have no populated /sys; fall back to
    // scanning /dev directly for the usual USB-serial names.
    if (found.empty()) {
        if (DIR *dir = ::opendir("/dev")) {
            while (struct dirent *entry = ::readdir(dir)) {
                const std::string name = entry->d_name;
                if (name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0
                    || name.rfind("ttyAMA", 0) == 0) {
                    found.insert("/dev/" + name);
                }
            }
            ::closedir(dir);
        }
    }

    std::vector<std::string> ports(found.begin(), found.end());

    // Group by device family, then by number, so ttyUSB2 sorts before
    // ttyUSB10 and the USB adapters stay together.
    std::sort(ports.begin(), ports.end(), [](const std::string &a, const std::string &b) {
        const std::string sa = stem(a);
        const std::string sb = stem(b);
        if (sa != sb)
            return sa < sb;
        return trailingNumber(a) < trailingNumber(b);
    });
    return ports;
}

std::string Port::displayName(const std::string &port)
{
    const std::string prefix = "/dev/";
    if (port.rfind(prefix, 0) == 0)
        return port.substr(prefix.size());
    return port;
}

} // namespace serial
