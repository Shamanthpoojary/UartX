#include "ribbon.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QToolButton>

// ---------------------------------------------------------------------------
// RibbonGroup
// ---------------------------------------------------------------------------

RibbonGroup::RibbonGroup(const QString &title, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ribbonGroup"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 8, 14, 5);
    outer->setSpacing(4);

    m_content = new QGridLayout;
    m_content->setContentsMargins(0, 0, 0, 0);
    m_content->setHorizontalSpacing(8);
    m_content->setVerticalSpacing(6);
    outer->addLayout(m_content, 0);

    // Every group is stretched to the height of the tallest one, and the
    // spare height has to land somewhere. Give it to an explicit spacer: a
    // grid of fixed-height controls cannot grow, so without this the caption
    // absorbs the slack and floats its text mid-box, leaving the captions of
    // shorter groups sitting higher than the rest.
    outer->addStretch(1);

    auto *caption = new QLabel(title, this);
    caption->setObjectName(QStringLiteral("ribbonGroupTitle"));
    caption->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    caption->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    outer->addWidget(caption, 0);
}

// ---------------------------------------------------------------------------
// RibbonBar
// ---------------------------------------------------------------------------

RibbonBar::RibbonBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ribbonBar"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(12, 6, 6, 6);
    outer->setSpacing(6);

    // The groups and the collapsed summary share one column, with the chevron
    // pinned to its right.
    auto *column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    outer->addLayout(column, 1);

    m_body = new QWidget(this);
    m_bodyRow = new QHBoxLayout(m_body);
    m_bodyRow->setContentsMargins(0, 0, 0, 0);
    m_bodyRow->setSpacing(12);   // the gap between groups, uniform at any width
    m_bodyRow->addStretch(1);    // keeps the groups packed to the left
    column->addWidget(m_body);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("ribbonSummary"));
    // The summary must never be what decides how wide the bar wants to be;
    // that job belongs to the groups alone.
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_summary->setVisible(false);
    column->addWidget(m_summary);

    m_chevron = new QToolButton(this);
    m_chevron->setObjectName(QStringLiteral("ribbonChevron"));
    m_chevron->setAutoRaise(true);
    m_chevron->setFixedSize(22, 22);
    m_chevron->setCursor(Qt::PointingHandCursor);
    connect(m_chevron, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });
    outer->addWidget(m_chevron, 0, Qt::AlignTop);

    updateChevron();
}

RibbonGroup *RibbonBar::addGroup(const QString &title)
{
    auto *group = new RibbonGroup(title, m_body);
    // Insert ahead of the trailing stretch, so the groups stay left-aligned
    // with one consistent gap between them however wide the window gets.
    // Spreading them across the full width instead makes the gaps grow with
    // the window and the grouping harder to read.
    m_bodyRow->insertWidget(m_bodyRow->count() - 1, group);
    ++m_groupCount;
    return group;
}

void RibbonBar::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;

    // The groups are flattened rather than hidden. A hidden widget stops
    // contributing to the layout, so the bar's preferred width would collapse
    // with it and spring back on expand -- which reflows every group and can
    // push the right-hand one past the edge of the window. Keeping the body in
    // the layout at zero height leaves the width demand untouched, so
    // expanding restores the previous arrangement exactly.
    m_body->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 0);
    m_summary->setVisible(!expanded);

    updateChevron();
    updateGeometry();
    emit expandedChanged(expanded);
}

void RibbonBar::setSummary(const QString &text)
{
    m_summary->setText(text);
}

void RibbonBar::updateChevron()
{
    m_chevron->setArrowType(m_expanded ? Qt::UpArrow : Qt::DownArrow);
    m_chevron->setToolTip(m_expanded ? tr("Collapse the ribbon")
                                     : tr("Expand the ribbon"));
    m_chevron->setAccessibleName(m_expanded ? tr("Collapse the ribbon")
                                            : tr("Expand the ribbon"));
}
