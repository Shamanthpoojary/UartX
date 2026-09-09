#ifndef RIBBON_H
#define RIBBON_H

#include <QFrame>
#include <QVector>

class QHBoxLayout;
class QGridLayout;
class QLabel;
class QToolButton;

/// One titled group of controls inside the ribbon, e.g. "Connection".
class RibbonGroup : public QFrame
{
    Q_OBJECT

public:
    explicit RibbonGroup(const QString &title, QWidget *parent = nullptr);

    /// Grid the caller fills with controls. Row 0 is the top row of the group.
    QGridLayout *content() const { return m_content; }

private:
    QGridLayout *m_content = nullptr;
};

/// A collapsible ribbon: a row of titled groups with a small chevron at the
/// right-hand edge that folds the whole thing down to a single summary strip.
///
/// This replaces the old "Hide toolbar" push button; the chevron is the only
/// affordance, matching the pattern used by mainstream desktop applications.
class RibbonBar : public QFrame
{
    Q_OBJECT

public:
    explicit RibbonBar(QWidget *parent = nullptr);

    RibbonGroup *addGroup(const QString &title);

    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded);

    /// One-line description shown in place of the groups while collapsed.
    void setSummary(const QString &text);

signals:
    void expandedChanged(bool expanded);

private:
    void updateChevron();

    QWidget     *m_body     = nullptr;
    QHBoxLayout *m_bodyRow  = nullptr;
    QLabel      *m_summary  = nullptr;
    QToolButton *m_chevron  = nullptr;
    bool         m_expanded = true;
    int          m_groupCount = 0;
};

#endif // RIBBON_H
