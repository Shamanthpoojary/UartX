#include "ribbon.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QToolButton>
#include <QTransform>

namespace {

/// The ribbon's chevron, drawn from the same asset the combo boxes use so the
/// two read as one control language. QToolButton::setArrowType paints a small
/// filled triangle in the platform style, which matches nothing else here.
QIcon chevronIcon(bool pointingUp)
{
    QPixmap pm(QStringLiteral(":/icons/chevron_down.png"));
    if (pm.isNull())
        return {};
    if (pointingUp) {
        QTransform flip;
        flip.rotate(180);
        pm = pm.transformed(flip, Qt::SmoothTransformation);
    }
    return QIcon(pm);
}

} // namespace

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
    m_chevron->setFixedSize(36, 24);
    m_chevron->setIconSize(QSize(12, 12));
    m_chevron->setCursor(Qt::PointingHandCursor);
    connect(m_chevron, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });
    outer->addWidget(m_chevron, 0, Qt::AlignTop);

    updateChevron();
}

RibbonGroup *RibbonBar::addGroup(const QString &title)
{
    auto *group = new RibbonGroup(title, m_body);
    // Every group carries the same stretch, so spare width is shared between
    // the group boxes themselves rather than pooling into one dead gap at the
    // right-hand end. The 12px spacing between them stays fixed either way,
    // which is what keeps the grouping readable as the window grows.
    m_bodyRow->addWidget(group, 1);
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
    // Points the way the click will take the ribbon: up to fold it away, down
    // to bring it back.
    m_chevron->setIcon(chevronIcon(m_expanded));
    m_chevron->setToolTip(m_expanded ? tr("Collapse the ribbon")
                                     : tr("Expand the ribbon"));
    m_chevron->setAccessibleName(m_expanded ? tr("Collapse the ribbon")
                                            : tr("Expand the ribbon"));
}
