#include "serialport.h"

#include <algorithm>
#include <cctype>
#include <windows.h>

namespace serial {
namespace {

HANDLE toHandle(long long v)
{
    return v < 0 ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(static_cast<intptr_t>(v));
}

std::string lastErrorText()
{
    const DWORD err = ::GetLastError();
    LPWSTR msg = nullptr;
    const DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&msg), 0, nullptr);

    std::string text;
    if (len && msg) {
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, msg, static_cast<int>(len),
                                                nullptr, 0, nullptr, nullptr);
        text.resize(static_cast<size_t>(bytes));
        ::WideCharToMultiByte(CP_UTF8, 0, msg, static_cast<int>(len), text.data(), bytes,
                              nullptr, nullptr);
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' '))
            text.pop_back();
    } else {
        text = "error " + std::to_string(err);
    }
    if (msg)
        ::LocalFree(msg);
    return text;
}

std::wstring widen(const std::string &s)
{
    if (s.empty())
        return {};
    const int chars = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                            nullptr, 0);
    std::wstring w(static_cast<size_t>(chars), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), chars);
    return w;
}

int trailingNumber(const std::string &s)
{
    size_t i = s.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1])))
        --i;
    return (i == s.size()) ? -1 : std::stoi(s.substr(i));
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

    // The \\.\COMx form also reaches ports numbered above 9.
    const std::wstring path = widen("\\\\.\\" + cfg.port);

    HANDLE h = ::CreateFileW(path.c_str(),
                             GENERIC_READ | GENERIC_WRITE,
                             0,              // exclusive access
                             nullptr,
                             OPEN_EXISTING,
                             0,              // synchronous I/O
                             nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error)
            *error = "could not open " + cfg.port + ": " + lastErrorText();
        return false;
    }

    DCB dcb;
    ::ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!::GetCommState(h, &dcb)) {
        if (error)
            *error = "GetCommState failed on " + cfg.port + ": " + lastErrorText();
        ::CloseHandle(h);
        return false;
    }

    dcb.BaudRate = static_cast<DWORD>(cfg.baud);
    dcb.ByteSize = static_cast<BYTE>(cfg.dataBits);

    if      (cfg.parity == "Even")  dcb.Parity = EVENPARITY;
    else if (cfg.parity == "Odd")   dcb.Parity = ODDPARITY;
    else if (cfg.parity == "Mark")  dcb.Parity = MARKPARITY;
    else if (cfg.parity == "Space") dcb.Parity = SPACEPARITY;
    else                            dcb.Parity = NOPARITY;
    dcb.fParity = (dcb.Parity != NOPARITY);

    if      (cfg.stopBits == "1.5") dcb.StopBits = ONE5STOPBITS;
    else if (cfg.stopBits == "2")   dcb.StopBits = TWOSTOPBITS;
    else                            dcb.StopBits = ONESTOPBIT;

    const bool xonxoff = (cfg.flow == "XON/XOFF");
    const bool rtscts  = (cfg.flow == "RTS/CTS");

    dcb.fBinary         = TRUE;
    dcb.fOutX           = xonxoff;
    dcb.fInX            = xonxoff;
    dcb.fOutxCtsFlow    = rtscts;
    dcb.fRtsControl     = rtscts ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
    dcb.fDtrControl     = DTR_CONTROL_ENABLE;
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fAbortOnError   = FALSE;
    dcb.XonChar         = 0x11;
    dcb.XoffChar        = 0x13;

    if (!::SetCommState(h, &dcb)) {
        if (error)
            *error = "invalid serial settings for " + cfg.port + ": " + lastErrorText();
        ::CloseHandle(h);
        return false;
    }

    // Return as soon as any byte is available, or after the timeout, so the
    // reader loop stays responsive on an idle line.
    COMMTIMEOUTS to;
    to.ReadIntervalTimeout         = MAXDWORD;
    to.ReadTotalTimeoutMultiplier  = MAXDWORD;
    to.ReadTotalTimeoutConstant    = static_cast<DWORD>(readTimeoutMs);
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 1000;
    ::SetCommTimeouts(h, &to);

    ::SetupComm(h, 1 << 16, 1 << 16);
    ::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);

    m_handle = static_cast<long long>(reinterpret_cast<intptr_t>(h));
    return true;
}

void Port::close()
{
    if (m_handle >= 0) {
        ::CloseHandle(toHandle(m_handle));
        m_handle = -1;
    }
}

int Port::read(char *buffer, int size)
{
    if (!isOpen())
        return -1;

    DWORD got = 0;
    if (!::ReadFile(toHandle(m_handle), buffer, static_cast<DWORD>(size), &got, nullptr))
        return -1;      // device unplugged or reset
    return static_cast<int>(got);
}

int Port::write(const char *data, int size)
{
    if (!isOpen())
        return -1;

    DWORD written = 0;
    if (!::WriteFile(toHandle(m_handle), data, static_cast<DWORD>(size), &written, nullptr))
        return -1;
    return static_cast<int>(written);
}

std::vector<std::string> Port::available()
{
    // HKLM\HARDWARE\DEVICEMAP\SERIALCOMM lists every port currently present,
    // USB-serial adapters included. Each value is named after the device
    // (e.g. "\Device\Silabser0") and holds the port name ("COM5").
    //
    // This uses the raw registry API rather than QSettings: QSettings rewrites
    // the backslashes in a value name as forward slashes, and feeding that
    // back to QSettings::value() makes it parse them as a group path, so every
    // lookup comes back empty and no port is ever found.
    std::vector<std::string> ports;

    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM",
                        0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return ports;   // the key is absent when the machine has no ports at all
    }

    DWORD valueCount = 0, maxNameLen = 0, maxDataLen = 0;
    if (::RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &valueCount, &maxNameLen, &maxDataLen, nullptr, nullptr)
        != ERROR_SUCCESS) {
        ::RegCloseKey(key);
        return ports;
    }

    std::vector<wchar_t> nameBuf(maxNameLen + 2);
    std::vector<BYTE>    dataBuf(maxDataLen + sizeof(wchar_t) * 2);

    for (DWORD i = 0; i < valueCount; ++i) {
        DWORD nameLen = static_cast<DWORD>(nameBuf.size());
        DWORD dataLen = static_cast<DWORD>(dataBuf.size());
        DWORD type = 0;

        const LONG rc = ::RegEnumValueW(key, i, nameBuf.data(), &nameLen, nullptr,
                                        &type, dataBuf.data(), &dataLen);
        if (rc == ERROR_NO_MORE_ITEMS)
            break;
        if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
            continue;

        // The reported length includes the terminator when the value has one,
        // so cut at the first NUL before trimming.
        const wchar_t *w = reinterpret_cast<const wchar_t *>(dataBuf.data());
        int chars = static_cast<int>(dataLen / sizeof(wchar_t));
        for (int c = 0; c < chars; ++c) {
            if (w[c] == L'\0') { chars = c; break; }
        }
        if (chars <= 0)
            continue;

        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, w, chars, nullptr, 0, nullptr, nullptr);
        std::string port(static_cast<size_t>(bytes), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w, chars, port.data(), bytes, nullptr, nullptr);

        while (!port.empty() && std::isspace(static_cast<unsigned char>(port.back())))
            port.pop_back();

        if (!port.empty() && std::find(ports.begin(), ports.end(), port) == ports.end())
            ports.push_back(port);
    }
    ::RegCloseKey(key);

    // COM2 before COM10
    std::sort(ports.begin(), ports.end(), [](const std::string &a, const std::string &b) {
        const int na = trailingNumber(a);
        const int nb = trailingNumber(b);
        if (na >= 0 && nb >= 0)
            return na < nb;
        return a < b;
    });
    return ports;
}

std::string Port::displayName(const std::string &port)
{
    return port;
}

} // namespace serial
