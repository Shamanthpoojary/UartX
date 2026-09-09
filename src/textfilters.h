#ifndef TEXTFILTERS_H
#define TEXTFILTERS_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>

/// One user-defined text filter.
///
/// A filter decides which lines are *shown* in a Filter window. It is a plain
/// substring — a line passes when it contains the text, e.g. a pattern of
/// "/6/" shows only the lines containing "/6/".
///
/// This is separate from a colour rule (see colorrules.h), which only decides
/// what colour a line is drawn in.
struct TextFilter
{
    QString name;                   ///< label shown in the filter list
    QString pattern;                ///< literal substring the line must contain
    bool    enabled = true;         ///< a disabled filter is offered but never matches
    bool    matchCase = false;

    bool isValid() const { return !pattern.isEmpty(); }
    bool matches(const QString &line) const;

    /// The label to show: the name when given, otherwise the pattern itself.
    QString displayName() const;

    QJsonObject toJson() const;
    static TextFilter fromJson(const QJsonObject &o);
};

/// The user's library of text filters, shared by every Filter window. Each
/// window chooses which of them are active for that view.
class TextFilterSet
{
public:
    QVector<TextFilter> filters;

    /// True when the line passes at least one of the named filters.
    /// `selected` holds display names; an empty selection passes everything,
    /// so a fresh window is never mysteriously blank.
    bool passes(const QString &line, const QSet<QString> &selected) const;

    QStringList displayNames() const;
    bool isEmpty() const { return filters.isEmpty(); }

    QJsonArray toJson() const;
    static TextFilterSet fromJson(const QJsonArray &a);
};

#endif // TEXTFILTERS_H
