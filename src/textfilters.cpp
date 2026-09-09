#include "textfilters.h"

bool TextFilter::matches(const QString &line) const
{
    if (!enabled || pattern.isEmpty())
        return false;
    return line.contains(pattern, matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive);
}

QString TextFilter::displayName() const
{
    return name.trimmed().isEmpty() ? pattern : name.trimmed();
}

QJsonObject TextFilter::toJson() const
{
    QJsonObject o;
    o["name"]      = name;
    o["pattern"]   = pattern;
    o["enabled"]   = enabled;
    o["matchCase"] = matchCase;
    return o;
}

TextFilter TextFilter::fromJson(const QJsonObject &o)
{
    TextFilter f;
    f.name      = o.value("name").toString();
    f.pattern   = o.value("pattern").toString();
    f.enabled   = o.value("enabled").toBool(true);
    f.matchCase = o.value("matchCase").toBool(false);
    return f;
}

// ---------------------------------------------------------------------------

bool TextFilterSet::passes(const QString &line, const QSet<QString> &selected) const
{
    if (selected.isEmpty())
        return true;   // nothing selected means "no filtering", not "show nothing"

    for (const TextFilter &f : filters) {
        if (!f.isValid() || !selected.contains(f.displayName()))
            continue;
        if (f.matches(line))
            return true;
    }
    return false;
}

QStringList TextFilterSet::displayNames() const
{
    QStringList out;
    out.reserve(filters.size());
    for (const TextFilter &f : filters)
        out.append(f.displayName());
    return out;
}

QJsonArray TextFilterSet::toJson() const
{
    QJsonArray a;
    for (const TextFilter &f : filters)
        a.append(f.toJson());
    return a;
}

TextFilterSet TextFilterSet::fromJson(const QJsonArray &a)
{
    TextFilterSet set;
    set.filters.reserve(a.size());
    for (const QJsonValue &v : a) {
        if (!v.isObject())
            continue;
        const TextFilter f = TextFilter::fromJson(v.toObject());
        if (f.isValid())
            set.filters.append(f);
    }
    return set;
}
