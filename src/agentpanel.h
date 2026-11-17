#ifndef AGENTPANEL_H
#define AGENTPANEL_H

#include <QDateTime>
#include <QVector>
#include <QWidget>
#include <functional>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTextBrowser;

/// The "Claude" tab: everything an AI agent looked at in this session and
/// everything it concluded, in words a person who has never read a UART log
/// can follow.
///
///   Findings              the agent's plain-language notes, in the order written
///   What Claude looked at every request, as a sentence, with its line numbers
///   Lines Claude was sent the exact lines behind the selected request
///
/// Nothing here is hidden from the user or summarised away: the bottom pane
/// shows precisely the lines the agent received, so its conclusions can be
/// checked against the evidence.
class AgentPanel : public QWidget
{
    Q_OBJECT

public:
    /// Fetches the lines with these seq numbers as display text, in order.
    using LineFetcher = std::function<QStringList(const QVector<quint64> &seqs)>;

    explicit AgentPanel(QWidget *parent = nullptr);

    void setAgentName(const QString &displayName, bool tookOver);
    void setLineFetcher(LineFetcher fetch) { m_fetch = std::move(fetch); }

    void addActivity(const QString &sentence, const QVector<quint64> &seqs);
    void addFinding(const QString &text);

    bool isEmpty() const { return m_activity.isEmpty() && m_findings.isEmpty(); }

    /// The whole panel as a Markdown report: findings, then every request
    /// with the lines it returned.
    QString reportMarkdown(const QString &sessionLine) const;

signals:
    void saveReportRequested();

private:
    struct Activity
    {
        QDateTime        when;
        QString          sentence;
        QVector<quint64> seqs;
    };
    struct Finding
    {
        QDateTime when;
        QString   text;
    };

    void showLinesFor(int row);
    void renderFindings();

    QLabel         *m_header   = nullptr;
    QTextBrowser   *m_findingView = nullptr;
    QListWidget    *m_list     = nullptr;
    QPlainTextEdit *m_lines    = nullptr;
    QString         m_agent    = QStringLiteral("Claude");
    QVector<Activity> m_activity;
    QVector<Finding>  m_findings;
    LineFetcher     m_fetch;
};

#endif // AGENTPANEL_H
