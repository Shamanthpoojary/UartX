#include "configstore.h"
#include "lineformat.h"

#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>

namespace {
constexpr int CONFIG_VERSION = 2;
const QString KEY_VERSION      = QStringLiteral("version");
const QString KEY_SETTINGS     = QStringLiteral("settings");
const QString KEY_COLOR_RULES  = QStringLiteral("colorRules");
const QString KEY_TEXT_FILTERS = QStringLiteral("textFilters");
const QString KEY_CONFIGS      = QStringLiteral("configurations");
const QString KEY_ACTIVE       = QStringLiteral("activeConfiguration");
} // namespace

// ---------------------------------------------------------------------------
// AppSettings <-> JSON
// ---------------------------------------------------------------------------

QJsonObject AppSettings::toJson() const
{
    QJsonObject o;
    o["port"]           = port;
    o["baud"]           = baud;
    o["databits"]       = databits;
    o["parity"]         = parity;
    o["stopbits"]       = stopbits;
    o["flow"]           = flow;
    o["logEnabled"]     = logEnabled;
    o["logDir"]         = logDir;
    o["logTemplate"]    = logTemplate;
    o["logAppend"]      = logAppend;
    o["autoscroll"]     = autoscroll;
    o["localEcho"]      = localEcho;
    o["autoReconnect"]  = autoReconnect;
    o["eol"]            = eol;
    o["colorize"]       = colorize;
    o["ribbonExpanded"] = ribbonExpanded;
    o["showTimestamps"] = showTimestamps;
    o["showDirection"]  = showDirection;
    o["displayMode"]    = displayMode;
    o["txHistory"]      = QJsonArray::fromStringList(txHistory);
    return o;
}

AppSettings AppSettings::fromJson(const QJsonObject &o)
{
    AppSettings s;   // the defaults stand in for anything the file omits
    s.port           = o.value("port").toString(s.port);
    s.baud           = o.value("baud").toString(s.baud);
    s.databits       = o.value("databits").toString(s.databits);
    s.parity         = o.value("parity").toString(s.parity);
    s.stopbits       = o.value("stopbits").toString(s.stopbits);
    s.flow           = o.value("flow").toString(s.flow);
    s.logEnabled     = o.value("logEnabled").toBool(s.logEnabled);
    s.logDir         = o.value("logDir").toString(s.logDir);
    s.logTemplate    = o.value("logTemplate").toString(s.logTemplate);
    s.logAppend      = o.value("logAppend").toBool(s.logAppend);
    s.autoscroll     = o.value("autoscroll").toBool(s.autoscroll);
    s.localEcho      = o.value("localEcho").toBool(s.localEcho);
    s.autoReconnect  = o.value("autoReconnect").toBool(s.autoReconnect);
    s.eol            = o.value("eol").toString(s.eol);
    s.colorize       = o.value("colorize").toBool(s.colorize);
    s.ribbonExpanded = o.value("ribbonExpanded").toBool(s.ribbonExpanded);
    s.showTimestamps = o.value("showTimestamps").toBool(s.showTimestamps);
    s.showDirection  = o.value("showDirection").toBool(s.showDirection);
    s.displayMode    = o.value("displayMode").toString(s.displayMode);
    if (!LineFormat::modes().contains(s.displayMode))
        s.displayMode = QStringLiteral("ASCII");

    s.txHistory.clear();
    const QJsonArray history = o.value("txHistory").toArray();
    for (const QJsonValue &v : history) {
        if (v.isString())
            s.txHistory.append(v.toString());
    }
    while (s.txHistory.size() > App::MAX_TX_HISTORY)
        s.txHistory.removeFirst();

    // Accept the older "CR+LF" spelling.
    s.eol = App::normaliseLineEnding(s.eol);
    return s;
}

// ---------------------------------------------------------------------------
// UserState <-> JSON
// ---------------------------------------------------------------------------

QJsonObject ConfigStore::stateToJson(const UserState &state)
{
    QJsonObject o;
    o[KEY_SETTINGS]     = state.settings.toJson();
    o[KEY_COLOR_RULES]  = state.colorRules.toJson();
    o[KEY_TEXT_FILTERS] = state.textFilters.toJson();
    return o;
}

UserState ConfigStore::stateFromJson(const QJsonObject &o, const UserState &fallback)
{
    UserState state = fallback;
    if (o.value(KEY_SETTINGS).isObject())
        state.settings = AppSettings::fromJson(o.value(KEY_SETTINGS).toObject());

    // An explicitly empty array is a real choice -- the user deleted every
    // entry -- so it must not be replaced by the fallback.
    if (o.value(KEY_COLOR_RULES).isArray())
        state.colorRules = ColorRuleSet::fromJson(o.value(KEY_COLOR_RULES).toArray());
    if (o.value(KEY_TEXT_FILTERS).isArray())
        state.textFilters = TextFilterSet::fromJson(o.value(KEY_TEXT_FILTERS).toArray());
    return state;
}

// ---------------------------------------------------------------------------
// File access
// ---------------------------------------------------------------------------

bool ConfigStore::exists()
{
    return QFile::exists(filePath());
}

QJsonObject ConfigStore::readDocument()
{
    QFile f(filePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    return doc.isObject() ? doc.object() : QJsonObject();
}

bool ConfigStore::writeDocument(const QJsonObject &doc)
{
    if (!QDir().mkpath(App::configDir()))
        return false;

    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write(QJsonDocument(doc).toJson(QJsonDocument::Indented));
    return f.commit();
}

// ---------------------------------------------------------------------------
// Active state
// ---------------------------------------------------------------------------

bool ConfigStore::load(UserState *state, QString *activeConfigName)
{
    const QJsonObject doc = readDocument();
    if (doc.isEmpty())
        return false;

    if (state)
        *state = stateFromJson(doc, *state);
    if (activeConfigName)
        *activeConfigName = doc.value(KEY_ACTIVE).toString();
    return true;
}

bool ConfigStore::save(const UserState &state, const QString &activeConfigName)
{
    QJsonObject doc = readDocument();     // keep the stored configurations
    const QJsonObject s = stateToJson(state);
    doc[KEY_VERSION]      = CONFIG_VERSION;
    doc[KEY_SETTINGS]     = s.value(KEY_SETTINGS);
    doc[KEY_COLOR_RULES]  = s.value(KEY_COLOR_RULES);
    doc[KEY_TEXT_FILTERS] = s.value(KEY_TEXT_FILTERS);
    doc[KEY_ACTIVE]       = activeConfigName;
    return writeDocument(doc);
}

// ---------------------------------------------------------------------------
// Named configurations
// ---------------------------------------------------------------------------

QStringList ConfigStore::configNames()
{
    const QJsonObject doc = readDocument();
    if (!doc.value(KEY_CONFIGS).isObject())
        return {};
    QStringList names = doc.value(KEY_CONFIGS).toObject().keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool ConfigStore::configExists(const QString &name)
{
    const QJsonObject doc = readDocument();
    return doc.value(KEY_CONFIGS).toObject().contains(name);
}

bool ConfigStore::saveNamedConfig(const QString &name, const UserState &state)
{
    if (name.trimmed().isEmpty())
        return false;

    QJsonObject doc = readDocument();
    QJsonObject configs = doc.value(KEY_CONFIGS).toObject();
    configs[name] = stateToJson(state);

    doc[KEY_VERSION] = CONFIG_VERSION;
    doc[KEY_CONFIGS] = configs;
    return writeDocument(doc);
}

bool ConfigStore::loadNamedConfig(const QString &name, UserState *state)
{
    const QJsonObject doc = readDocument();
    const QJsonObject configs = doc.value(KEY_CONFIGS).toObject();
    if (!configs.value(name).isObject())
        return false;

    if (state)
        *state = stateFromJson(configs.value(name).toObject(), *state);
    return true;
}

bool ConfigStore::deleteNamedConfig(const QString &name)
{
    QJsonObject doc = readDocument();
    QJsonObject configs = doc.value(KEY_CONFIGS).toObject();
    if (!configs.contains(name))
        return false;

    configs.remove(name);
    doc[KEY_CONFIGS] = configs;
    if (doc.value(KEY_ACTIVE).toString() == name)
        doc[KEY_ACTIVE] = QString();
    return writeDocument(doc);
}
