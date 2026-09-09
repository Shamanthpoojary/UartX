#ifndef FILTERWINDOW_H
#define FILTERWINDOW_H

#include "colorrules.h"
#include "textfilters.h"
#include "lineformat.h"

#include <QWidget>
#include <QDialog>
#include <QHash>
#include <QSet>
#include <QColor>

class MainWindow;
class TerminalView;
class QCheckBox;
class QLineEdit;
class QLabel;
class QTimer;
class QVBoxLayout;
class QGroupBox;
class QPushButton;

/// Editor for a single text filter: label, the text to match, match-case.
class TextFilterEditDialog : public QDialog
{
    Q_OBJECT

public:
    TextFilterEditDialog(const TextFilter &filter, QWidget *parent, const QString &title);

    TextFilter filter() const { return m_filter; }

private slots:
    void acceptIfValid();

private:
    TextFilter m_filter;
    QLineEdit *m_name;
    QLineEdit *m_pattern;
    QCheckBox *m_matchCase;
};

/// A Filter window: a live view showing only the lines the user selects.
///
/// Lines can be selected two ways, and the two combine:
///   * by colour rule  -- show every line a given highlight rule matched, so
///                        the coloured categories double as filters;
///   * by text filter  -- show every line containing a given piece of text.
/// A line is shown when it matches ANY ticked source. With nothing ticked the
/// window shows everything, so it is never mysteriously blank.
///
/// Filters apply to UPCOMING data only -- the window starts empty and follows
/// the live stream, so it never re-scans buffered history and stays
/// responsive no matter how large the capture is.
///
/// The visible output can be saved to its own file, with a header recording
/// exactly which filter settings produced it.
class FilterWindow : public QWidget
{
    Q_OBJECT

public:
    FilterWindow(MainWindow *app, const QString &title);

    /// Live feed from the main window (upcoming data only). Matching uses the
    /// record's raw text, so filters keep working in any display mode.
    void onLine(const LineRecord &record);

    /// Rebuilds both selection lists after the rules or filters change.
    void refreshSources(const ColorRuleSet &rules, const TextFilterSet &filters);

    /// Writes the visible lines, preceded by a header recording the filter
    /// settings that produced them. Separated from the Save action so the
    /// file format can be exercised without a file dialog.
    bool writeOutputTo(const QString &path, QString *error) const;

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void applySelection();     // clears the view, then filters from here on
    void clearView();
    void copySelection();
    void saveOutput();
    void addFilter();
    void editSelectedFilter();
    void deleteSelectedFilter();
    void onSearchTextChanged(const QString &text);
    void findNext();
    void findPrevious();

private:
    bool matches(const LineRecord &record) const;
    void append(const LineRecord &record);
    void runSearch(bool backwards, bool fromCursor);
    void updateSearchStatus();
    void setFollowLive(bool on);
    void setCount(const QString &text);
    void showTemporaryNotice(const QString &message);
    QString buildHeader() const;
    int  checkedFilterIndex() const;   // the single ticked text filter, or -1
    QSet<QString> checkedNames(const QHash<QString, QCheckBox *> &boxes) const;

    MainWindow   *m_app = nullptr;
    TerminalView *m_text = nullptr;

    QVBoxLayout *m_ruleLayout   = nullptr;
    QVBoxLayout *m_filterLayout = nullptr;
    QHash<QString, QCheckBox *> m_ruleBoxes;     // colour-rule name -> checkbox
    QHash<QString, QCheckBox *> m_filterBoxes;   // filter label     -> checkbox
    QLabel      *m_ruleHint     = nullptr;
    QLabel      *m_filterHint   = nullptr;
    QLineEdit   *m_search       = nullptr;
    QPushButton *m_prevBtn      = nullptr;
    QPushButton *m_nextBtn      = nullptr;
    QLabel      *m_searchStatus = nullptr;
    QCheckBox   *m_autoscrollBox = nullptr;
    QLabel      *m_countLabel   = nullptr;
    QPushButton *m_editBtn      = nullptr;
    QPushButton *m_deleteBtn    = nullptr;
    QTimer      *m_noticeTimer  = nullptr;

    /// Remembered across rebuilds, so editing a rule or filter does not
    /// silently change what an open window is showing.
    QSet<QString> m_selectedRules;
    QSet<QString> m_selectedFilters;
    int     m_shown = 0;
    QString m_baseCount;

    /// Follow-live is suspended while a search is active so results stay put;
    /// this remembers what it was so clearing the box restores it.
    bool m_followBeforeSearch = true;
    bool m_searchActive = false;
};

#endif // FILTERWINDOW_H
