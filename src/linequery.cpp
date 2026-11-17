#include "linequery.h"

namespace {

/// Firmware logs are short lines; a pattern longer than this is a mistake,
/// and an enormous regex is a way to stall the matcher.
constexpr int MAX_PATTERN = 512;

} // namespace

LineQuery::LineQuery(const QString &pattern, bool regex, bool matchCase)
    : m_pattern(pattern)
    , m_regex(regex)
    , m_case(matchCase)
{
    if (pattern.size() > MAX_PATTERN) {
        m_valid = false;
        m_error = QStringLiteral("pattern longer than %1 characters").arg(MAX_PATTERN);
        return;
    }
    if (regex) {
        QRegularExpression::PatternOptions opts = QRegularExpression::UseUnicodePropertiesOption;
        if (!matchCase)
            opts |= QRegularExpression::CaseInsensitiveOption;
        m_re = QRegularExpression(pattern, opts);
        if (!m_re.isValid()) {
            m_valid = false;
            m_error = m_re.errorString();
            return;
        }
        // Compiled once, here, so a search over fifty thousand lines does not
        // pay for it on every one.
        m_re.optimize();
    }
}

bool LineQuery::matches(const QString &text) const
{
    if (!m_valid)
        return false;
    if (m_pattern.isEmpty())
        return true;
    if (m_regex)
        return m_re.match(text).hasMatch();
    return text.contains(m_pattern, m_case ? Qt::CaseSensitive : Qt::CaseInsensitive);
}

QString LineQuery::describe() const
{
    if (m_pattern.isEmpty())
        return QStringLiteral("every line");
    if (m_regex)
        return QLatin1Char('/') + m_pattern + QLatin1Char('/');
    return QStringLiteral("“%1”").arg(m_pattern);
}
