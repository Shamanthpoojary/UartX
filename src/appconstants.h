#ifndef APPCONSTANTS_H
#define APPCONSTANTS_H

#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QStandardPaths>

// Version comes from the CMake project() declaration so the executable's
// Windows version resource, the installer and the About box can never drift
// apart. The fallback only matters for hand-rolled builds.
#ifndef APP_VERSION_STRING
#  define APP_VERSION_STRING "1.0.0"
#endif

// ---------------------------------------------------------------------------
// Application identity and the defaults a fresh installation starts from.
//
// Everything here is only a starting point: every value below is exposed in
// the UI and persisted to the configuration file, so UartX makes no
// assumption about any particular device, firmware or log format.
// ---------------------------------------------------------------------------

namespace App {

inline const QString NAME      = QStringLiteral("UartX");
inline const QString TAGLINE   = QStringLiteral("UART Terminal & Log Filter");
inline const QString VERSION   = QStringLiteral(APP_VERSION_STRING);
inline const QString DEVELOPER = QStringLiteral("Shamanth");
inline const QString YEAR      = QStringLiteral("2026");
inline const QString SUPPORT_EMAIL = QStringLiteral("shamanth25402@gmail.com");

// Common defaults for a new installation -- all user-configurable.
inline const QString DEFAULT_BAUD     = QStringLiteral("115200");
inline const QString DEFAULT_TEMPLATE = QStringLiteral("uartx_&Y-&M-&D_&H_&T.log");

inline const QStringList BAUD_CHOICES = {
    QStringLiteral("9600"),   QStringLiteral("19200"),   QStringLiteral("38400"),
    QStringLiteral("57600"),  QStringLiteral("115200"),  QStringLiteral("230400"),
    QStringLiteral("460800"), QStringLiteral("921600"),  QStringLiteral("1500000"),
    QStringLiteral("2000000")
};

// "Line Ending" in the UI. None sends the typed text with nothing appended.
inline const QStringList EOL_CHOICES = {
    QStringLiteral("None"), QStringLiteral("LF"),
    QStringLiteral("CR"),   QStringLiteral("CRLF")
};

/// Configurations written before CRLF was spelled without a plus still say
/// "CR+LF"; fold the old spelling onto the new one on load.
inline QString normaliseLineEnding(const QString &value)
{
    if (value == QLatin1String("CR+LF"))
        return QStringLiteral("CRLF");
    return EOL_CHOICES.contains(value) ? value : QStringLiteral("CR");
}

/// Directory holding config.json.
inline QString configDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return base + QLatin1Char('/') + NAME;
}

inline QString configFilePath()
{
    return configDir() + QStringLiteral("/config.json");
}

/// Where log files go until the user picks somewhere else. Resolved at
/// runtime so an installed copy writes to the current user's Documents
/// folder rather than the path of the machine it was built on.
inline QString defaultLogDir()
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty())
        docs = QDir::homePath() + QStringLiteral("/Documents");
    return QDir::toNativeSeparators(docs + QLatin1Char('/') + NAME + QStringLiteral("_Logs"));
}

/// An empty setting means "use the default folder".
inline QString effectiveLogDir(const QString &configured)
{
    return configured.trimmed().isEmpty() ? defaultLogDir() : configured.trimmed();
}

/// A path fit to put on screen: the location of the user's home directory is
/// folded away, so no account name is ever displayed. Windows uses the
/// environment-variable form it is used to; elsewhere the usual "~" applies.
inline QString displayPath(const QString &path)
{
    if (path.isEmpty())
        return path;

    const QString p = QDir::toNativeSeparators(path);
    const QString home = QDir::toNativeSeparators(QDir::homePath());

#ifdef Q_OS_WIN
    const QString localAppData =
        QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    // LOCALAPPDATA lives under the home directory, so it has to be tried first.
    if (!localAppData.isEmpty() && p.startsWith(localAppData, Qt::CaseInsensitive))
        return QStringLiteral("%LOCALAPPDATA%") + p.mid(localAppData.size());
    if (!home.isEmpty() && p.startsWith(home, Qt::CaseInsensitive))
        return QStringLiteral("%USERPROFILE%") + p.mid(home.size());
#else
    if (!home.isEmpty() && p.startsWith(home))
        return QStringLiteral("~") + p.mid(home.size());
#endif
    return p;
}

// Terminal look
inline const QString TERM_BG        = QStringLiteral("#0c0c0c");
inline const QString TERM_FG        = QStringLiteral("#cccccc");
inline const QString TERM_SYSMSG    = QStringLiteral("#5c6370");
// Transmitted data gets its own subtle shade so it reads as distinct from
// device output without shouting.
inline const QString TERM_TX        = QStringLiteral("#7f93a8");
// Timestamps and RX/TX markers sit behind the content they annotate.
inline const QString TERM_META      = QStringLiteral("#5f666f");
inline const QString TERM_SEL_BG    = QStringLiteral("#404040");
inline const QString TERM_SEL_FG    = QStringLiteral("#cccccc");
inline constexpr int TERM_FONT_SIZE = 10;

inline constexpr int    MAX_SCROLLBACK_LINES = 8000;
inline constexpr double PARTIAL_FLUSH_S      = 0.25;   // flush an unterminated line after this
inline constexpr int    POLL_INTERVAL_MS     = 40;     // UI drain cadence
inline constexpr int    NOTIFICATION_MS      = 2000;   // "N characters copied" lifetime
inline constexpr int    AUTOSAVE_DELAY_MS    = 800;    // debounce for writing config.json
inline constexpr int    MAX_TX_HISTORY       = 50;     // remembered sent commands

} // namespace App

#endif // APPCONSTANTS_H
