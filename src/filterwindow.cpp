#include "filterwindow.h"
#include "mainwindow.h"
#include "terminalview.h"
#include "appconstants.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QSaveFile>
#include <QDir>
#include <QDateTime>
#include <QTimer>
#include <QCloseEvent>
#include <QApplication>
#include <QClipboard>
#include <QIcon>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QPixmap>

namespace {

/// A scrollable column of checkboxes that stays a sensible height however
/// many rules or filters the user has defined.
QScrollArea *makeCheckList(QWidget *parent, QVBoxLayout **layoutOut)
{
    auto *area = new QScrollArea(parent);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setMinimumWidth(180);
    area->setMaximumHeight(140);
    area->viewport()->setAutoFillBackground(false);

    auto *host = new QWidget;
    host->setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    layout->addStretch(1);
    area->setWidget(host);

    *layoutOut = layout;
    return area;
}

void clearCheckList(QVBoxLayout *layout, QHash<QString, QCheckBox *> *boxes, QLabel **hint)
{
    for (QCheckBox *box : std::as_const(*boxes)) {
        layout->removeWidget(box);
        box->deleteLater();
    }
    boxes->clear();
    if (*hint) {
        layout->removeWidget(*hint);
        (*hint)->deleteLater();
        *hint = nullptr;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Single text-filter editor
// ---------------------------------------------------------------------------

TextFilterEditDialog::TextFilterEditDialog(const TextFilter &filter, QWidget *parent,
                                           const QString &title)
    : QDialog(parent)
    , m_filter(filter)
{
    setWindowTitle(title);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    root->addLayout(form);

    m_pattern = new QLineEdit(m_filter.pattern, this);
    m_pattern->setPlaceholderText(tr("e.g. /6/"));
    m_pattern->setMinimumWidth(240);
    form->addRow(tr("Show lines containing:"), m_pattern);

    m_name = new QLineEdit(m_filter.name, this);
    m_name->setPlaceholderText(tr("optional, defaults to the text above"));
    form->addRow(tr("Label:"), m_name);

    m_matchCase = new QCheckBox(tr("Match case"), this);
    m_matchCase->setChecked(m_filter.matchCase);
    form->addRow(QString(), m_matchCase);

    auto *hint = new QLabel(
        tr("Only lines containing this text are shown in the Filter window.\n"
           "The text is matched literally, not as a pattern."), this);
    hint->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    root->addSpacing(4);
    root->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &TextFilterEditDialog::acceptIfValid);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addSpacing(6);
    root->addWidget(buttons);
}

void TextFilterEditDialog::acceptIfValid()
{
    if (m_pattern->text().isEmpty()) {
        QMessageBox::warning(this, tr("Filter text"),
                             tr("Enter the text a line must contain to be shown."));
        m_pattern->setFocus();
        return;
    }
    m_filter.pattern   = m_pattern->text();
    m_filter.name      = m_name->text().trimmed();
    m_filter.matchCase = m_matchCase->isChecked();
    accept();
}

// ---------------------------------------------------------------------------
// Filter window
// ---------------------------------------------------------------------------

FilterWindow::FilterWindow(MainWindow *app, const QString &title)
    : QWidget(nullptr)
    , m_app(app)
{
    setWindowTitle(QStringLiteral("%1 - %2").arg(title, App::NAME));
    setWindowIcon(Theme::appIcon());
    resize(1000, 620);
    setAttribute(Qt::WA_DeleteOnClose);

    m_baseCount = tr("0 lines");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(10);
    root->addLayout(bar);

    // ------------------------------------------------------- colour rules
    auto *ruleBox = new QGroupBox(tr("Color rules"), this);
    auto *ruleOuter = new QVBoxLayout(ruleBox);
    ruleOuter->setContentsMargins(10, 6, 10, 8);
    ruleOuter->addWidget(makeCheckList(ruleBox, &m_ruleLayout));
    bar->addWidget(ruleBox, 1);

    // -------------------------------------------------------- text filters
    auto *filterBox = new QGroupBox(tr("Text filters"), this);
    auto *filterOuter = new QHBoxLayout(filterBox);
    filterOuter->setContentsMargins(10, 6, 10, 8);
    filterOuter->setSpacing(10);
    filterOuter->addWidget(makeCheckList(filterBox, &m_filterLayout), 1);

    auto *filterButtons = new QVBoxLayout;
    auto *addBtn = new QPushButton(tr("Add..."), filterBox);
    m_editBtn    = new QPushButton(tr("Edit..."), filterBox);
    m_deleteBtn  = new QPushButton(tr("Delete"), filterBox);
    for (QPushButton *b : { addBtn, m_editBtn, m_deleteBtn }) {
        b->setFixedWidth(88);
        filterButtons->addWidget(b);
    }
    filterButtons->addStretch(1);
    filterOuter->addLayout(filterButtons);

    connect(addBtn,      &QPushButton::clicked, this, &FilterWindow::addFilter);
    connect(m_editBtn,   &QPushButton::clicked, this, &FilterWindow::editSelectedFilter);
    connect(m_deleteBtn, &QPushButton::clicked, this, &FilterWindow::deleteSelectedFilter);

    bar->addWidget(filterBox, 1);

    // -------------------------------------------------------------- search
    // This searches what is already on screen, unlike the filters above which
    // decide what gets captured in the first place.
    auto *searchBox = new QGroupBox(tr("Search captured lines"), this);
    auto *searchLayout = new QVBoxLayout(searchBox);
    searchLayout->setContentsMargins(10, 6, 10, 8);

    m_search = new QLineEdit(searchBox);
    m_search->setPlaceholderText(tr("Find in the lines below..."));
    m_search->setMinimumWidth(200);
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &FilterWindow::onSearchTextChanged);
    connect(m_search, &QLineEdit::returnPressed, this, &FilterWindow::findNext);
    searchLayout->addWidget(m_search);

    auto *navRow = new QHBoxLayout;
    m_prevBtn = new QPushButton(tr("Previous"), searchBox);
    m_nextBtn = new QPushButton(tr("Next"), searchBox);
    m_prevBtn->setFixedWidth(88);
    m_nextBtn->setFixedWidth(88);
    m_prevBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);
    connect(m_prevBtn, &QPushButton::clicked, this, &FilterWindow::findPrevious);
    connect(m_nextBtn, &QPushButton::clicked, this, &FilterWindow::findNext);
    navRow->addWidget(m_prevBtn);
    navRow->addWidget(m_nextBtn);
    navRow->addStretch(1);
    searchLayout->addLayout(navRow);

    m_searchStatus = new QLabel(searchBox);
    m_searchStatus->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    m_searchStatus->setWordWrap(true);
    searchLayout->addWidget(m_searchStatus);
    searchLayout->addStretch(1);
    bar->addWidget(searchBox);

    // ------------------------------------------------------------ text area
    m_text = new TerminalView(this);
    m_text->setForwardKeys(false);
    connect(m_text, &TerminalView::copyRequested, this, &FilterWindow::copySelection);
    root->addWidget(m_text, 1);

    // --------------------------------------------------------- bottom strip
    auto *bottom = new QHBoxLayout;
    m_autoscrollBox = new QCheckBox(tr("Autoscroll"), this);
    m_autoscrollBox->setChecked(true);
    connect(m_autoscrollBox, &QCheckBox::toggled, this, [this](bool on) {
        m_text->setAutoscroll(on);
        // Toggling by hand during a search becomes the new preference.
        if (m_searchActive)
            m_followBeforeSearch = on;
    });
    bottom->addWidget(m_autoscrollBox);

    auto *clearBtn = new QPushButton(tr("Clear"), this);
    clearBtn->setFixedWidth(88);
    connect(clearBtn, &QPushButton::clicked, this, &FilterWindow::clearView);
    bottom->addWidget(clearBtn);

    auto *saveBtn = new QPushButton(tr("Save output..."), this);
    saveBtn->setFixedWidth(124);
    saveBtn->setToolTip(tr("Write the filtered lines shown here to a file, "
                           "with a header recording the filter settings"));
    connect(saveBtn, &QPushButton::clicked, this, &FilterWindow::saveOutput);
    bottom->addWidget(saveBtn);

    bottom->addStretch(1);
    m_countLabel = new QLabel(m_baseCount, this);
    bottom->addWidget(m_countLabel);
    root->addLayout(bottom);

    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    connect(m_noticeTimer, &QTimer::timeout, this, [this] {
        m_countLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_countLabel->setText(m_baseCount);
    });

    refreshSources(m_app->colorRules(), m_app->textFilters());
}

// ---------------------------------------------------------- selection lists

QSet<QString> FilterWindow::checkedNames(const QHash<QString, QCheckBox *> &boxes) const
{
    QSet<QString> names;
    for (auto it = boxes.cbegin(); it != boxes.cend(); ++it)
        if (it.value()->isChecked())
            names.insert(it.key());
    return names;
}

void FilterWindow::refreshSources(const ColorRuleSet &rules, const TextFilterSet &filters)
{
    // Remember what is ticked, so editing a rule or filter does not change
    // what this window is showing.
    if (!m_ruleBoxes.isEmpty())
        m_selectedRules = checkedNames(m_ruleBoxes);
    if (!m_filterBoxes.isEmpty())
        m_selectedFilters = checkedNames(m_filterBoxes);

    clearCheckList(m_ruleLayout, &m_ruleBoxes, &m_ruleHint);
    clearCheckList(m_filterLayout, &m_filterBoxes, &m_filterHint);

    // --- colour rules ---
    if (rules.isEmpty()) {
        m_ruleHint = new QLabel(tr("No color rules defined.\nAdd them under Tools > Color rules."));
        m_ruleHint->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
        m_ruleLayout->insertWidget(m_ruleLayout->count() - 1, m_ruleHint);
    }
    for (const ColorRule &r : rules.rules) {
        auto *box = new QCheckBox(r.name);
        box->setChecked(m_selectedRules.contains(r.name));
        QPixmap swatch(10, 10);
        swatch.fill(r.color);
        box->setIcon(QIcon(swatch));
        box->setToolTip(r.enabled
                            ? tr("Show lines matching the color rule \"%1\" (keyword \"%2\")")
                                  .arg(r.name, r.keyword)
                            : tr("This color rule is switched off, so it matches nothing."));
        connect(box, &QCheckBox::toggled, this, &FilterWindow::applySelection);
        m_ruleLayout->insertWidget(m_ruleLayout->count() - 1, box);
        m_ruleBoxes.insert(r.name, box);
    }

    // --- text filters ---
    if (filters.isEmpty()) {
        m_filterHint = new QLabel(tr("No text filters yet.\nChoose Add... to create one,\n"
                                     "for example lines containing \"/6/\"."));
        m_filterHint->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
        m_filterLayout->insertWidget(m_filterLayout->count() - 1, m_filterHint);
    }
    for (const TextFilter &f : filters.filters) {
        const QString label = f.displayName();
        auto *box = new QCheckBox(label);
        box->setChecked(m_selectedFilters.contains(label));
        box->setToolTip(f.enabled
                            ? tr("Show lines containing \"%1\"").arg(f.pattern)
                            : tr("This filter is switched off, so it matches nothing."));
        connect(box, &QCheckBox::toggled, this, &FilterWindow::applySelection);
        m_filterLayout->insertWidget(m_filterLayout->count() - 1, box);
        m_filterBoxes.insert(label, box);
    }

    const bool anyFilters = !filters.isEmpty();
    m_editBtn->setEnabled(anyFilters);
    m_deleteBtn->setEnabled(anyFilters);
}

int FilterWindow::checkedFilterIndex() const
{
    const TextFilterSet &lib = m_app->textFilters();
    const QSet<QString> sel = checkedNames(m_filterBoxes);
    if (sel.size() != 1)
        return -1;
    const QString wanted = *sel.cbegin();
    for (int i = 0; i < lib.filters.size(); ++i)
        if (lib.filters.at(i).displayName() == wanted)
            return i;
    return -1;
}

// --------------------------------------------------------------- filtering

bool FilterWindow::matches(const LineRecord &record) const
{
    const QSet<QString> rules   = checkedNames(m_ruleBoxes);
    const QSet<QString> filters = checkedNames(m_filterBoxes);
    if (rules.isEmpty() && filters.isEmpty())
        return true;   // nothing selected means "no filtering", not "show nothing"

    if (!record.colorRule.isEmpty() && rules.contains(record.colorRule))
        return true;

    // Always match the raw text, so a filter written for the ASCII view keeps
    // working when the display mode is hex.
    for (const TextFilter &f : m_app->textFilters().filters) {
        if (!f.isValid() || !filters.contains(f.displayName()))
            continue;
        if (f.matches(record.raw))
            return true;
    }
    return false;
}

void FilterWindow::onLine(const LineRecord &record)
{
    if (matches(record))
        append(record);
}

void FilterWindow::append(const LineRecord &record)
{
    m_text->appendLine(record.prefix, QColor(App::TERM_META),
                       record.body, record.color, true);
    ++m_shown;
    m_baseCount = tr("%1 lines").arg(m_shown);
    setCount(m_baseCount);
    if (m_searchActive)
        updateSearchStatus();
}

void FilterWindow::applySelection()
{
    // Selection change: clear the view and apply from here on. History is
    // intentionally NOT re-scanned, so this is instant even on a huge capture.
    clearView();
}

void FilterWindow::clearView()
{
    m_text->clearScreen();
    m_shown = 0;
    m_baseCount = tr("0 lines");
    setCount(m_baseCount);
}

void FilterWindow::setCount(const QString &text)
{
    if (m_noticeTimer->isActive())
        return;   // a notification currently owns the label
    m_countLabel->setText(text);
}

// ------------------------------------------------------------------ search

void FilterWindow::setFollowLive(bool on)
{
    QSignalBlocker block(m_autoscrollBox);
    m_autoscrollBox->setChecked(on);
    m_text->setAutoscroll(on);
}

void FilterWindow::onSearchTextChanged(const QString &text)
{
    const bool active = !text.isEmpty();

    if (active && !m_searchActive) {
        // Entering a search: stop following the live stream so results do not
        // scroll away while they are being read.
        m_followBeforeSearch = m_autoscrollBox->isChecked();
        setFollowLive(false);
    } else if (!active && m_searchActive) {
        // Cleared: go back to whatever follow-live was before.
        setFollowLive(m_followBeforeSearch);
        QTextCursor cur = m_text->textCursor();
        cur.clearSelection();
        m_text->setTextCursor(cur);
    }
    m_searchActive = active;

    m_prevBtn->setEnabled(active);
    m_nextBtn->setEnabled(active);

    if (active)
        runSearch(false, false);      // jump to the first match from the top
    else
        m_searchStatus->clear();
}

void FilterWindow::runSearch(bool backwards, bool fromCursor)
{
    const QString needle = m_search->text();
    if (needle.isEmpty())
        return;

    QTextDocument::FindFlags flags;
    if (backwards)
        flags |= QTextDocument::FindBackward;

    if (!fromCursor) {
        QTextCursor start = m_text->textCursor();
        start.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        m_text->setTextCursor(start);
    }

    if (!m_text->find(needle, flags)) {
        // Wrap around and try once more from the far end.
        QTextCursor wrapped = m_text->textCursor();
        wrapped.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        m_text->setTextCursor(wrapped);
        m_text->find(needle, flags);
    }
    updateSearchStatus();
}

void FilterWindow::findNext()      { runSearch(false, true); }
void FilterWindow::findPrevious()  { runSearch(true,  true); }

void FilterWindow::updateSearchStatus()
{
    const QString needle = m_search->text();
    if (needle.isEmpty()) {
        m_searchStatus->clear();
        return;
    }

    const int total = m_text->toPlainText().count(needle, Qt::CaseInsensitive);
    if (total == 0) {
        m_searchStatus->setText(tr("No matches"));
        return;
    }
    m_searchStatus->setText(tr("%1 match%2 - follow-live paused")
                                .arg(total)
                                .arg(total == 1 ? QString() : QStringLiteral("es")));
}

// ------------------------------------------------------- filter library edits

void FilterWindow::addFilter()
{
    TextFilterEditDialog dlg(TextFilter(), this, tr("Add filter"));
    if (dlg.exec() != QDialog::Accepted)
        return;

    TextFilterSet lib = m_app->textFilters();
    lib.filters.append(dlg.filter());
    m_selectedFilters.insert(dlg.filter().displayName());   // tick the new one
    m_app->applyTextFilters(lib);
}

void FilterWindow::editSelectedFilter()
{
    const int index = checkedFilterIndex();
    if (index < 0) {
        QMessageBox::information(this, tr("Edit filter"),
                                 tr("Tick exactly one text filter to edit it."));
        return;
    }

    TextFilterSet lib = m_app->textFilters();
    TextFilterEditDialog dlg(lib.filters.at(index), this, tr("Edit filter"));
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString oldLabel = lib.filters.at(index).displayName();
    lib.filters[index] = dlg.filter();
    m_selectedFilters.remove(oldLabel);
    m_selectedFilters.insert(dlg.filter().displayName());
    m_app->applyTextFilters(lib);
}

void FilterWindow::deleteSelectedFilter()
{
    const int index = checkedFilterIndex();
    if (index < 0) {
        QMessageBox::information(this, tr("Delete filter"),
                                 tr("Tick exactly one text filter to delete it."));
        return;
    }

    TextFilterSet lib = m_app->textFilters();
    const QString label = lib.filters.at(index).displayName();
    if (QMessageBox::question(this, tr("Delete filter"),
                              tr("Delete the filter '%1'?").arg(label)) != QMessageBox::Yes)
        return;

    lib.filters.remove(index);
    m_selectedFilters.remove(label);
    m_app->applyTextFilters(lib);
}

// ------------------------------------------------------------- save output

QString FilterWindow::buildHeader() const
{
    const AppSettings &s = m_app->settings();

    QStringList rules = QStringList(checkedNames(m_ruleBoxes).values());
    rules.sort(Qt::CaseInsensitive);

    // List the filters with their patterns, so the file records exactly what
    // was matched rather than just a label.
    const QSet<QString> chosen = checkedNames(m_filterBoxes);
    QStringList filters;
    for (const TextFilter &f : m_app->textFilters().filters) {
        if (!chosen.contains(f.displayName()))
            continue;
        filters.append(QStringLiteral("%1 (\"%2\"%3)")
                           .arg(f.displayName(), f.pattern,
                                f.matchCase ? QStringLiteral(", match case") : QString()));
    }

    const QString none = tr("(none)");
    const QString all  = tr("(none selected - all lines shown)");
    const bool nothingSelected = rules.isEmpty() && filters.isEmpty();

    QString h;
    h += QStringLiteral("===== %1 %2 - filtered output =====\n").arg(App::NAME, App::VERSION);
    h += QStringLiteral("Saved         : %1\n")
             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    h += QStringLiteral("Source        : %1 @ %2 baud\n")
             .arg(s.port.isEmpty() ? tr("(no port)") : s.port, s.baud);
    h += QStringLiteral("Color rules   : %1\n")
             .arg(rules.isEmpty() ? (nothingSelected ? all : none)
                                  : rules.join(QStringLiteral(", ")));
    h += QStringLiteral("Text filters  : %1\n")
             .arg(filters.isEmpty() ? (nothingSelected ? all : none)
                                    : filters.join(QStringLiteral(", ")));
    const DisplayOptions display = m_app->displayOptions();
    h += QStringLiteral("Display       : %1%2%3\n")
             .arg(display.mode,
                  display.timestamps ? tr(", timestamps") : QString(),
                  display.direction  ? tr(", RX/TX markers") : QString());
    h += QStringLiteral("Lines         : %1\n").arg(m_shown);
    h += QStringLiteral("=========================================\n");
    return h;
}

void FilterWindow::saveOutput()
{
    const QString content = m_text->toPlainText();
    if (content.isEmpty()) {
        QMessageBox::information(this, tr("Save output"),
                                 tr("There is nothing to save yet - this window is empty."));
        return;
    }

    const QString suggested =
        QDir(App::effectiveLogDir(m_app->settings().logDir))
            .filePath(QStringLiteral("uartx_filtered_%1.log")
                          .arg(QDateTime::currentDateTime()
                                   .toString(QStringLiteral("yyyy-MM-dd_HHmmss"))));

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save filtered output"), suggested,
        tr("Log files (*.log);;Text files (*.txt);;All files (*)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!writeOutputTo(path, &error)) {
        QMessageBox::critical(this, tr("Save output"),
                              tr("Could not write to:\n%1\n\n%2")
                                  .arg(QDir::toNativeSeparators(path), error));
        return;
    }
    showTemporaryNotice(tr("Saved %1 lines").arg(m_shown));
}

bool FilterWindow::writeOutputTo(const QString &path, QString *error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QString out = buildHeader();
    out += m_text->toPlainText();
    if (!out.endsWith(QLatin1Char('\n')))
        out.append(QLatin1Char('\n'));
    file.write(out.toUtf8());

    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

// ------------------------------------------------------------ copy + notice

void FilterWindow::copySelection()
{
    const QString selected = m_text->textCursor().selectedText();
    if (selected.isEmpty())
        return;

    QString plain = selected;
    plain.replace(QChar(0x2029), QLatin1Char('\n'));   // paragraph separator -> newline
    QApplication::clipboard()->setText(plain);

    QTextCursor cur = m_text->textCursor();
    cur.clearSelection();
    m_text->setTextCursor(cur);

    const int n = plain.size();
    showTemporaryNotice(tr("%1 character%2 copied")
                            .arg(n).arg(n != 1 ? QStringLiteral("s") : QString()));
}

void FilterWindow::showTemporaryNotice(const QString &message)
{
    m_noticeTimer->stop();
    m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_countLabel->setText(message);
    m_noticeTimer->start(App::NOTIFICATION_MS);
}

void FilterWindow::closeEvent(QCloseEvent *e)
{
    m_noticeTimer->stop();
    m_app->unregisterFilterWindow(this);
    e->accept();
}
