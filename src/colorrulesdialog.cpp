#include "colorrulesdialog.h"
#include "appconstants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QColorDialog>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QPixmap>
#include <QIcon>

// ---------------------------------------------------------------------------
// Single-filter editor
// ---------------------------------------------------------------------------

ColorRuleEditDialog::ColorRuleEditDialog(const ColorRule &rule, QWidget *parent, const QString &title)
    : QDialog(parent)
    , m_rule(rule)
{
    setWindowTitle(title);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    root->addLayout(form);

    m_name = new QLineEdit(m_rule.name, this);
    m_name->setPlaceholderText(tr("e.g. Error"));
    m_name->setMinimumWidth(240);
    form->addRow(tr("Rule name:"), m_name);

    m_keyword = new QLineEdit(m_rule.keyword, this);
    m_keyword->setPlaceholderText(tr("e.g. |E|"));
    form->addRow(tr("Keyword:"), m_keyword);

    m_colorBtn = new QPushButton(this);
    m_colorBtn->setMinimumWidth(140);
    connect(m_colorBtn, &QPushButton::clicked, this, &ColorRuleEditDialog::pickColor);
    form->addRow(tr("Color:"), m_colorBtn);
    updateColorButton();

    m_matchCase = new QCheckBox(tr("Match case"), this);
    m_matchCase->setChecked(m_rule.matchCase);
    form->addRow(QString(), m_matchCase);

    auto *hint = new QLabel(
        tr("Any line containing the keyword is shown entirely in this colour.\n"
           "The keyword is plain text, not a pattern."), this);
    hint->setStyleSheet(QStringLiteral("color: #777777;"));
    root->addSpacing(4);
    root->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ColorRuleEditDialog::acceptIfValid);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addSpacing(6);
    root->addWidget(buttons);
}

void ColorRuleEditDialog::updateColorButton()
{
    QPixmap swatch(16, 16);
    swatch.fill(m_rule.color);
    m_colorBtn->setIcon(QIcon(swatch));
    m_colorBtn->setText(m_rule.color.name(QColor::HexRgb).toUpper());
}

void ColorRuleEditDialog::pickColor()
{
    const QColor c = QColorDialog::getColor(m_rule.color, this, tr("Rule color"));
    if (c.isValid()) {
        m_rule.color = c;
        updateColorButton();
    }
}

void ColorRuleEditDialog::acceptIfValid()
{
    const QString name = m_name->text().trimmed();
    const QString keyword = m_keyword->text();

    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Filter name"), tr("Please enter a filter name."));
        m_name->setFocus();
        return;
    }
    if (keyword.isEmpty()) {
        QMessageBox::warning(this, tr("Keyword"),
                             tr("Please enter the keyword to look for in incoming lines."));
        m_keyword->setFocus();
        return;
    }

    m_rule.name      = name;
    m_rule.keyword   = keyword;
    m_rule.matchCase = m_matchCase->isChecked();
    accept();
}

// ---------------------------------------------------------------------------
// Filters window
// ---------------------------------------------------------------------------

namespace {
enum Column { ColEnabled = 0, ColName, ColKeyword, ColCase, ColColor, ColumnCount };
}

ColorRulesDialog::ColorRulesDialog(const ColorRuleSet &rules, QWidget *parent)
    : QDialog(parent)
    , m_rules(rules)
{
    setWindowTitle(tr("Color Rules - %1").arg(App::NAME));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(700, 420);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);

    auto *intro = new QLabel(
        tr("Incoming lines are checked against these rules in order, and the first "
           "match decides the color of the whole line. Color rules change how lines "
           "look; they never hide a line — use Filters for that."), this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: #888888;"));
    root->addWidget(intro);
    root->addSpacing(6);

    auto *body = new QHBoxLayout;
    root->addLayout(body, 1);

    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setHorizontalHeaderLabels({ tr("On"), tr("Name"), tr("Keyword"),
                                         tr("Case"), tr("Color") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColKeyword, QHeaderView::Stretch);
    m_table->setColumnWidth(ColEnabled, 42);
    m_table->setColumnWidth(ColCase, 52);
    m_table->setColumnWidth(ColColor, 110);
    connect(m_table, &QTableWidget::cellChanged, this, &ColorRulesDialog::onCellChanged);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this] { editRule(); });
    body->addWidget(m_table, 1);

    auto *side = new QVBoxLayout;
    auto *addBtn = new QPushButton(tr("Add..."), this);
    m_editBtn    = new QPushButton(tr("Edit..."), this);
    m_deleteBtn  = new QPushButton(tr("Delete"), this);
    m_upBtn      = new QPushButton(tr("Move up"), this);
    m_downBtn    = new QPushButton(tr("Move down"), this);
    for (QPushButton *b : { addBtn, m_editBtn, m_deleteBtn, m_upBtn, m_downBtn }) {
        b->setMinimumWidth(110);
        side->addWidget(b);
    }
    side->addStretch(1);
    body->addLayout(side);

    connect(addBtn,      &QPushButton::clicked, this, &ColorRulesDialog::addRule);
    connect(m_editBtn,   &QPushButton::clicked, this, &ColorRulesDialog::editRule);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ColorRulesDialog::deleteRule);
    connect(m_upBtn,     &QPushButton::clicked, this, &ColorRulesDialog::moveUp);
    connect(m_downBtn,   &QPushButton::clicked, this, &ColorRulesDialog::moveDown);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = currentRow();
        m_editBtn->setEnabled(row >= 0);
        m_deleteBtn->setEnabled(row >= 0);
        m_upBtn->setEnabled(row > 0);
        m_downBtn->setEnabled(row >= 0 && row < m_rules.rules.size() - 1);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addSpacing(8);
    root->addWidget(buttons);

    rebuildTable();
}

int ColorRulesDialog::currentRow() const
{
    return m_table->currentRow();
}

void ColorRulesDialog::selectRow(int row)
{
    if (row >= 0 && row < m_table->rowCount())
        m_table->selectRow(row);
}

void ColorRulesDialog::rebuildTable()
{
    m_populating = true;
    const int keep = m_table->currentRow();

    m_table->setRowCount(m_rules.rules.size());
    for (int i = 0; i < m_rules.rules.size(); ++i) {
        const ColorRule &r = m_rules.rules.at(i);

        auto *on = new QTableWidgetItem;
        on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        on->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, ColEnabled, on);

        auto *name = new QTableWidgetItem(r.name);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        name->setForeground(r.color);
        m_table->setItem(i, ColName, name);

        auto *kw = new QTableWidgetItem(r.keyword);
        kw->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(i, ColKeyword, kw);

        auto *cs = new QTableWidgetItem(r.matchCase ? tr("Aa") : QStringLiteral("-"));
        cs->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        cs->setTextAlignment(Qt::AlignCenter);
        cs->setToolTip(r.matchCase ? tr("Case sensitive") : tr("Case insensitive"));
        m_table->setItem(i, ColCase, cs);

        QPixmap swatch(14, 14);
        swatch.fill(r.color);
        auto *col = new QTableWidgetItem(QIcon(swatch), r.color.name(QColor::HexRgb).toUpper());
        col->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(i, ColColor, col);
    }

    m_populating = false;
    selectRow(qBound(0, keep, m_table->rowCount() - 1));

    const bool any = m_table->rowCount() > 0;
    const int row = currentRow();
    m_editBtn->setEnabled(any && row >= 0);
    m_deleteBtn->setEnabled(any && row >= 0);
    m_upBtn->setEnabled(row > 0);
    m_downBtn->setEnabled(row >= 0 && row < m_rules.rules.size() - 1);
}

void ColorRulesDialog::commit()
{
    emit rulesChanged(m_rules);
}

void ColorRulesDialog::onCellChanged(int row, int column)
{
    if (m_populating || column != ColEnabled)
        return;
    if (row < 0 || row >= m_rules.rules.size())
        return;

    m_rules.rules[row].enabled = (m_table->item(row, ColEnabled)->checkState() == Qt::Checked);
    commit();
}

void ColorRulesDialog::addRule()
{
    ColorRule fresh;
    // Cycle the suggested palette so consecutive new filters differ.
    const QVector<QColor> palette = ColorRuleSet::suggestedColors();
    fresh.color = palette.at(m_rules.rules.size() % palette.size());

    ColorRuleEditDialog dlg(fresh, this, tr("Add color rule"));
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_rules.rules.append(dlg.rule());
    rebuildTable();
    selectRow(m_rules.rules.size() - 1);
    commit();
}

void ColorRulesDialog::editRule()
{
    const int row = currentRow();
    if (row < 0 || row >= m_rules.rules.size())
        return;

    ColorRuleEditDialog dlg(m_rules.rules.at(row), this, tr("Edit color rule"));
    if (dlg.exec() != QDialog::Accepted)
        return;

    const bool wasEnabled = m_rules.rules.at(row).enabled;
    m_rules.rules[row] = dlg.rule();
    m_rules.rules[row].enabled = wasEnabled;   // the checkbox column owns this
    rebuildTable();
    selectRow(row);
    commit();
}

void ColorRulesDialog::deleteRule()
{
    const int row = currentRow();
    if (row < 0 || row >= m_rules.rules.size())
        return;

    const QString name = m_rules.rules.at(row).name;
    if (QMessageBox::question(this, tr("Delete color rule"),
                              tr("Delete the color rule '%1'?").arg(name)) != QMessageBox::Yes)
        return;

    m_rules.rules.remove(row);
    rebuildTable();
    selectRow(qMin(row, m_rules.rules.size() - 1));
    commit();
}

void ColorRulesDialog::moveUp()
{
    const int row = currentRow();
    if (row <= 0)
        return;
    m_rules.rules.swapItemsAt(row, row - 1);
    rebuildTable();
    selectRow(row - 1);
    commit();
}

void ColorRulesDialog::moveDown()
{
    const int row = currentRow();
    if (row < 0 || row >= m_rules.rules.size() - 1)
        return;
    m_rules.rules.swapItemsAt(row, row + 1);
    rebuildTable();
    selectRow(row + 1);
    commit();
}
