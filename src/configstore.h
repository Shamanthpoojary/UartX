#ifndef CONFIGSTORE_H
#define CONFIGSTORE_H

#include "appconstants.h"
#include "colorrules.h"
#include "textfilters.h"

#include <QString>
#include <QStringList>
#include <QJsonObject>

/// Everything the user can change, in one place.
struct AppSettings
{
    // --- serial line ---
    QString port;                                   // empty = pick the first port found
    QString baud     = App::DEFAULT_BAUD;
    QString databits = QStringLiteral("8");
    QString parity   = QStringLiteral("None");
    QString stopbits = QStringLiteral("1");
    QString flow     = QStringLiteral("None");

    // --- log saving (off until the user turns it on) ---
    bool    logEnabled  = false;
    QString logDir;                                 // empty = App::defaultLogDir()
    QString logTemplate = App::DEFAULT_TEMPLATE;
    bool    logAppend   = true;

    // --- terminal ---
    bool    autoscroll    = true;
    bool    localEcho     = false;
    bool    autoReconnect = true;
    QString eol           = QStringLiteral("CR");   // Line Ending: None/LF/CR/CRLF
    bool    colorize      = true;      // master switch for colour-rule highlighting
    bool    ribbonExpanded = true;

    // --- optional display aids (all off by default, set under Settings) ---
    bool    showTimestamps = false;
    bool    showDirection  = false;
    QString displayMode    = QStringLiteral("ASCII");

    // --- transmit ---
    QStringList txHistory;

    QJsonObject toJson() const;
    static AppSettings fromJson(const QJsonObject &o);
};

/// The complete user state: settings, colour rules and text filters. This is
/// what a named configuration stores, and what is active at runtime.
struct UserState
{
    AppSettings   settings;
    ColorRuleSet  colorRules;
    TextFilterSet textFilters;
};

/// Reads and writes the single configuration file, `config.json`, which holds
/// the active state, plus any named configurations. UartX loads it at startup
/// and saves it whenever something changes.
class ConfigStore
{
public:
    static QString filePath() { return App::configFilePath(); }
    static bool    exists();

    /// Loads the active state. Leaves `state` untouched and returns false when
    /// there is no config file yet (first run).
    static bool load(UserState *state, QString *activeConfigName = nullptr);

    /// Writes the active state, preserving any stored configurations.
    static bool save(const UserState &state, const QString &activeConfigName);

    // --- named configurations (stored in the same file) ---
    static QStringList configNames();
    static bool        configExists(const QString &name);
    static bool        saveNamedConfig(const QString &name, const UserState &state);
    static bool        loadNamedConfig(const QString &name, UserState *state);
    static bool        deleteNamedConfig(const QString &name);

private:
    static QJsonObject readDocument();
    static bool        writeDocument(const QJsonObject &doc);
    static QJsonObject stateToJson(const UserState &state);
    static UserState   stateFromJson(const QJsonObject &o, const UserState &fallback);
};

#endif // CONFIGSTORE_H
