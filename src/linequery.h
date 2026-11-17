#ifndef LINEQUERY_H
#define LINEQUERY_H

#include <QRegularExpression>
#include <QString>

/// A pattern a line is tested against: a literal substring by default, or a
/// regular expression. Used by the agent API's searches and by agent-owned
/// filter views, so "what Claude asked for" and "what the view shows" can
/// never disagree.
class LineQuery
{
public:
    LineQuery() = default;
    LineQuery(const QString &pattern, bool regex, bool matchCase);

    bool isValid() const { return m_valid; }
    QString error() const { return m_error; }
    QString pattern() const { return m_pattern; }
    bool isRegex() const { return m_regex; }
    bool matchCase() const { return m_case; }

    /// An empty pattern matches every line.
    bool matches(const QString &text) const;

    /// How the query reads in a sentence: "BLE HCI" or /status=[1-9]/.
    QString describe() const;

private:
    QString            m_pattern;
    bool               m_regex = false;
    bool               m_case  = false;
    bool               m_valid = true;
    QString            m_error;
    QRegularExpression m_re;
};

#endif // LINEQUERY_H
