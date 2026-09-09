#ifndef COLORRULES_H
#define COLORRULES_H

#include <QString>
#include <QStringList>
#include <QColor>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>

/// One user-defined highlight rule.
///
/// UartX makes no assumption about the device's message format: a rule is just
/// a name, a literal keyword to look for anywhere in the line, and the colour
/// the whole line takes when that keyword is present.
///
/// Colour rules only affect how lines are *displayed*. Deciding which lines
/// are shown at all is the job of the text filters (see textfilters.h).
struct ColorRule
{
    QString name;                       ///< shown in the rule list, e.g. "Error"
    QString keyword;                    ///< literal substring, e.g. "|E|"
    QColor  color = QColor("#ff5555");
    bool    enabled = true;
    bool    matchCase = false;          ///< off by default: "error" also matches "ERROR"

    bool isValid() const { return !name.trimmed().isEmpty() && !keyword.isEmpty(); }
    bool matches(const QString &line) const;

    QJsonObject toJson() const;
    static ColorRule fromJson(const QJsonObject &o);
};

/// An ordered list of rules. Order is meaningful: the first enabled rule whose
/// keyword occurs in the line wins, so the user can put specific rules above
/// general ones.
class ColorRuleSet
{
public:
    QVector<ColorRule> rules;

    /// The first enabled, valid rule matching the line, or nullptr.
    const ColorRule *firstMatch(const QString &line) const;

    QStringList names() const;
    bool isEmpty() const { return rules.isEmpty(); }

    QJsonArray toJson() const;
    static ColorRuleSet fromJson(const QJsonArray &a);

    /// Starter rules for a fresh installation. Deliberately plain English
    /// words that suit most devices; every one can be edited or deleted.
    static ColorRuleSet defaults();

    /// Palette offered when creating a new rule.
    static QVector<QColor> suggestedColors();
};

#endif // COLORRULES_H
