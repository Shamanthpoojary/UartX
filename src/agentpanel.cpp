#include "agentpanel.h"
#include "appconstants.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QTextCursor>

namespace {

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

QString seqSpan(const QVector<quint64> &seqs)
{
    if (seqs.isEmpty())
        return QString();
    if (seqs.size() == 1)
        return QStringLiteral("#%1").arg(seqs.first());
    return QStringLiteral("#%1–#%2").arg(seqs.first()).arg(seqs.last());
}

} // namespace

AgentPanel::AgentPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(6);

    m_header = new QLabel(this);
    m_header->setWordWrap(true);
    m_header->setTextFormat(Qt::RichText);
    root->addWidget(m_header);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->setChildrenCollapsible(false);

    // --- findings -----------------------------------------------------------
    auto *findBox = new QWidget(split);
    auto *findLayout = new QVBoxLayout(findBox);
    findLayout->setContentsMargins(0, 0, 0, 0);
    findLayout->addWidget(sectionTitle(tr("Findings"), findBox));
    m_findingView = new QTextBrowser(findBox);
    m_findingView->setOpenExternalLinks(false);
    findLayout->addWidget(m_findingView, 1);

    // --- activity -----------------------------------------------------------
    auto *actBox = new QWidget(split);
    auto *actLayout = new QVBoxLayout(actBox);
    actLayout->setContentsMargins(0, 0, 0, 0);
    actLayout->addWidget(sectionTitle(tr("What Claude looked at"), actBox));
    m_list = new QListWidget(actBox);
    m_list->setWordWrap(true);
    m_list->setAlternatingRowColors(true);
    actLayout->addWidget(m_list, 1);

    // --- evidence -----------------------------------------------------------
    auto *lineBox = new QWidget(split);
    auto *lineLayout = new QVBoxLayout(lineBox);
    lineLayout->setContentsMargins(0, 0, 0, 0);
    lineLayout->addWidget(sectionTitle(tr("Lines Claude was sent (select a request above)"), lineBox));
    m_lines = new QPlainTextEdit(lineBox);
    m_lines->setReadOnly(true);
    m_lines->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_lines->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lineLayout->addWidget(m_lines, 1);

    split->addWidget(findBox);
    split->addWidget(actBox);
    split->addWidget(lineBox);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    root->addWidget(split, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *save = new QPushButton(tr("Save report..."), this);
    save->setToolTip(tr("Write the findings and every request, with the lines it returned, to a file"));
    connect(save, &QPushButton::clicked, this, &AgentPanel::saveReportRequested);
    buttons->addWidget(save);
    root->addLayout(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, &AgentPanel::showLinesFor);
    // Selection can change without the current row moving -- keyboard
    // extension, accessibility tools -- and the evidence must follow it.
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        const QList<QListWidgetItem *> sel = m_list->selectedItems();
        if (!sel.isEmpty())
            showLinesFor(m_list->row(sel.first()));
    });

    setAgentName(QStringLiteral("Claude"), false);
    renderFindings();
}

void AgentPanel::setAgentName(const QString &displayName, bool tookOver)
{
    m_agent = displayName;
    m_header->setText(
        tr("<span style='background:#6f42c1; color:white; padding:1px 5px;'>&nbsp;AI&nbsp;</span>"
           "&nbsp; <b>%1 %2 this UartX.</b> It asks UartX for specific lines instead of reading the "
           "whole log. Everything it looked at, and everything it concluded, is listed below.")
            .arg(displayName.toHtmlEscaped(),
                 tookOver ? tr("took over") : tr("opened")));
}

void AgentPanel::renderFindings()
{
    if (m_findings.isEmpty()) {
        m_findingView->setHtml(tr("<p style='color:#8a8a8a;'>No findings yet. When %1 has looked at "
                                  "the log, its explanation of what is happening appears here.</p>")
                                   .arg(m_agent.toHtmlEscaped()));
        return;
    }
    // In the order they were written: an explanation often builds on the one
    // before it. The view scrolls to the newest.
    QString html;
    for (const Finding &f : m_findings) {
        QString body = f.text.toHtmlEscaped();
        body.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        html += QStringLiteral("<p style='margin-bottom:10px;'><span style='color:%1;'>%2</span><br>%3</p>")
                    .arg(App::TERM_META, f.when.toString(QStringLiteral("HH:mm:ss")), body);
    }
    m_findingView->setHtml(html);
    m_findingView->moveCursor(QTextCursor::End);
    m_findingView->ensureCursorVisible();
}

void AgentPanel::addFinding(const QString &text)
{
    m_findings.append({ QDateTime::currentDateTime(), text });
    renderFindings();
}

void AgentPanel::addActivity(const QString &sentence, const QVector<quint64> &seqs)
{
    m_activity.append({ QDateTime::currentDateTime(), sentence, seqs });
    QString label = QStringLiteral("%1  %2 %3")
                        .arg(m_activity.last().when.toString(QStringLiteral("HH:mm:ss")), m_agent, sentence);
    auto *item = new QListWidgetItem(label, m_list);
    if (seqs.isEmpty())
        item->setToolTip(tr("No log lines were sent for this request"));
    else
        item->setToolTip(tr("Click to see the %1 line(s) sent (%2)").arg(seqs.size()).arg(seqSpan(seqs)));
    m_list->scrollToBottom();
}

void AgentPanel::showLinesFor(int row)
{
    if (row < 0 || row >= m_activity.size()) {
        m_lines->clear();
        return;
    }
    const Activity &a = m_activity[row];
    if (a.seqs.isEmpty()) {
        m_lines->setPlainText(tr("No log lines were sent for this request: %1 %2")
                                  .arg(m_agent, a.sentence));
        return;
    }
    const QStringList lines = m_fetch ? m_fetch(a.seqs) : QStringList();
    m_lines->setPlainText(lines.isEmpty()
                              ? tr("These lines are no longer in memory; they are in lines.log (%1).")
                                    .arg(seqSpan(a.seqs))
                              : lines.join(QLatin1Char('\n')));
}

QString AgentPanel::reportMarkdown(const QString &sessionLine) const
{
    QString md;
    md += QStringLiteral("# UartX session report\n\n");
    md += QStringLiteral("%1\n\n").arg(sessionLine);
    md += QStringLiteral("Written %1. %2 asked UartX for specific lines rather than reading the "
                         "whole log; this report lists what it concluded and exactly what it was sent.\n\n")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), m_agent);

    md += QStringLiteral("## Findings\n\n");
    if (m_findings.isEmpty())
        md += QStringLiteral("_None recorded._\n\n");
    for (const Finding &f : m_findings)
        md += QStringLiteral("**%1**\n\n%2\n\n").arg(f.when.toString(QStringLiteral("HH:mm:ss")), f.text);

    md += QStringLiteral("## What %1 looked at\n\n").arg(m_agent);
    for (const Activity &a : m_activity) {
        md += QStringLiteral("- %1 — %2 %3\n").arg(a.when.toString(QStringLiteral("HH:mm:ss")), m_agent, a.sentence);
        if (a.seqs.isEmpty() || !m_fetch)
            continue;
        const QStringList lines = m_fetch(a.seqs);
        if (lines.isEmpty())
            continue;
        md += QStringLiteral("\n  ```\n");
        for (const QString &l : lines)
            md += QStringLiteral("  %1\n").arg(l);
        md += QStringLiteral("  ```\n");
    }
    return md;
}
