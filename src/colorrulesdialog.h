#ifndef COLORRULESDIALOG_H
#define COLORRULESDIALOG_H

#include "colorrules.h"

#include <QDialog>

class QTableWidget;
class QPushButton;
class QCheckBox;

/// Editor for a single colour rule: name, keyword, colour, match-case.
class ColorRuleEditDialog : public QDialog
{
    Q_OBJECT

public:
    ColorRuleEditDialog(const ColorRule &rule, QWidget *parent, const QString &title);

    ColorRule rule() const { return m_rule; }

private slots:
    void pickColor();
    void acceptIfValid();

private:
    void updateColorButton();

    ColorRule   m_rule;
    class QLineEdit *m_name;
    class QLineEdit *m_keyword;
    QPushButton     *m_colorBtn;
    QCheckBox       *m_matchCase;
};

/// The Color Rules window: add, edit, delete, reorder, enable and disable
/// the rules that colour incoming UART lines. Changes are applied live.
class ColorRulesDialog : public QDialog
{
    Q_OBJECT

public:
    ColorRulesDialog(const ColorRuleSet &rules, QWidget *parent);

    ColorRuleSet rules() const { return m_rules; }

signals:
    /// Emitted whenever the rule list changes, so the main window can
    /// recolour and persist immediately.
    void rulesChanged(const ColorRuleSet &rules);

private slots:
    void addRule();
    void editRule();
    void deleteRule();
    void moveUp();
    void moveDown();
    void onCellChanged(int row, int column);

private:
    void rebuildTable();
    void commit();
    int  currentRow() const;
    void selectRow(int row);

    ColorRuleSet  m_rules;
    QTableWidget *m_table;
    QPushButton  *m_editBtn;
    QPushButton  *m_deleteBtn;
    QPushButton  *m_upBtn;
    QPushButton  *m_downBtn;
    bool          m_populating = false;
};

#endif // COLORRULESDIALOG_H
