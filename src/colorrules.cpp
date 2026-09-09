#include "colorrules.h"

bool ColorRule::matches(const QString &line) const
{
    if (!enabled || keyword.isEmpty())
        return false;
    return line.contains(keyword, matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive);
}

QJsonObject ColorRule::toJson() const
{
    QJsonObject o;
    o["name"]      = name;
    o["keyword"]   = keyword;
    o["color"]     = color.name(QColor::HexRgb);
    o["enabled"]   = enabled;
    o["matchCase"] = matchCase;
    return o;
}

ColorRule ColorRule::fromJson(const QJsonObject &o)
{
    ColorRule r;
    r.name      = o.value("name").toString();
    r.keyword   = o.value("keyword").toString();
    r.enabled   = o.value("enabled").toBool(true);
    r.matchCase = o.value("matchCase").toBool(false);

    const QColor c(o.value("color").toString());
    if (c.isValid())
        r.color = c;
    return r;
}

// ---------------------------------------------------------------------------

const ColorRule *ColorRuleSet::firstMatch(const QString &line) const
{
    for (const ColorRule &r : rules)
        if (r.isValid() && r.matches(line))
            return &r;
    return nullptr;
}

QStringList ColorRuleSet::names() const
{
    QStringList out;
    out.reserve(rules.size());
    for (const ColorRule &r : rules)
        out.append(r.name);
    return out;
}

QJsonArray ColorRuleSet::toJson() const
{
    QJsonArray a;
    for (const ColorRule &r : rules)
        a.append(r.toJson());
    return a;
}

ColorRuleSet ColorRuleSet::fromJson(const QJsonArray &a)
{
    ColorRuleSet set;
    set.rules.reserve(a.size());
    for (const QJsonValue &v : a) {
        if (!v.isObject())
            continue;
        const ColorRule r = ColorRule::fromJson(v.toObject());
        if (r.isValid())
            set.rules.append(r);
    }
    return set;
}

ColorRuleSet ColorRuleSet::defaults()
{
    ColorRuleSet set;
    const struct { const char *name; const char *keyword; const char *color; } seed[] = {
        { "Error",   "error",   "#ff5555" },
        { "Warning", "warn",    "#e5c07b" },
        { "Info",    "info",    "#61afef" },
        { "Debug",   "debug",   "#8a8f98" },
    };
    for (const auto &s : seed) {
        ColorRule r;
        r.name    = QString::fromLatin1(s.name);
        r.keyword = QString::fromLatin1(s.keyword);
        r.color   = QColor(QString::fromLatin1(s.color));
        set.rules.append(r);
    }
    return set;
}

QVector<QColor> ColorRuleSet::suggestedColors()
{
    return {
        QColor("#ff5555"), QColor("#ff8c42"), QColor("#e5c07b"), QColor("#4ecb71"),
        QColor("#56b6c2"), QColor("#61afef"), QColor("#c678dd"), QColor("#ff79c6"),
        QColor("#cccccc"), QColor("#8a8f98"),
    };
}
