#include "mainwindow.h"
#include "terminalview.h"
#include "filterwindow.h"
#include "colorrulesdialog.h"
#include "ribbon.h"
#include "dialogs.h"
#include "lineformat.h"
#include "theme.h"

#include <QMenuBar>
#include <QMenu>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QLabel>
#include <QFrame>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QCloseEvent>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QIcon>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QKeyEvent>
#include <QFileInfo>
#include <QFontMetrics>

namespace {

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("fieldLabel"));
    return l;
}

/// Gives a button a stable width that is guaranteed to fit its text.
///
/// `alternatives` lists every caption the button can ever show, so a control
/// that swaps labels (Connect / Disconnect) is sized for the longest one and
/// neither clips nor resizes when its state changes.
void sizeButton(QPushButton *button, int minimum, const QStringList &alternatives = {})
{
    const QFontMetrics fm(button->font());
    int needed = fm.horizontalAdvance(button->text());
    for (const QString &caption : alternatives)
        needed = qMax(needed, fm.horizontalAdvance(caption));

    // Room for the stylesheet padding (10 px a side) plus the border.
    button->setFixedWidth(qMax(minimum, needed + 28));
}

} // namespace

// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("%1 - %2").arg(App::NAME, App::TAGLINE));
    setWindowIcon(Theme::appIcon());
    resize(1120, 740);

    m_currentStatus = tr("Not connected");

    // Start from the saved configuration; a fresh installation gets the
    // built-in defaults plus a small starter set of colour rules.
    m_state.colorRules = ColorRuleSet::defaults();
    const bool hadConfig = ConfigStore::load(&m_state, &m_activeConfig);

    // The timers are created before the widgets, not after: building the UI
    // connects signals that end in scheduleConfigSave() and showNotice(), and
    // restoring the saved state below makes them fire. A timer constructed
    // afterwards is still null when that happens, and the app dies in the
    // constructor before its window is ever shown.
    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    connect(m_noticeTimer, &QTimer::timeout, this, [this] {
        m_notificationActive = false;
        m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        updateStatusLine();
    });

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    connect(m_saveTimer, &QTimer::timeout, this, &MainWindow::saveConfigNow);

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPoll);
    m_pollTimer->start(App::POLL_INTERVAL_MS);

    buildMenuBar();
    buildUi();
    refreshPorts(false);        // silent: nothing to notify about at startup
    syncFromSettings();
    refreshConfigCombo();

    m_ribbon->setExpanded(m_state.settings.ribbonExpanded);
    m_ribbonAction->setChecked(m_state.settings.ribbonExpanded);

    // First run: put the defaults on disk straight away, so the file exists
    // and can be inspected even if the user changes nothing.
    if (!hadConfig)
        saveConfigNow();

    updateStatusLine();
    updateRibbonSummary();
}

MainWindow::~MainWindow()
{
    if (m_worker) {
        m_worker->stop();
        m_worker->wait(2000);
        delete m_worker;
        m_worker = nullptr;
    }
    closeLog();
}

// ------------------------------------------------------------------ menu bar

void MainWindow::buildMenuBar()
{
    QMenu *session = menuBar()->addMenu(tr("&Session"));
    QAction *connectAction = session->addAction(tr("Connect / Disconnect"),
                                                this, &MainWindow::onConnectClicked);
    connectAction->setShortcut(QKeySequence(Qt::Key_F2));
    connectAction->setShortcutContext(Qt::ApplicationShortcut);

    QAction *clearAction = session->addAction(tr("Clear screen"),
                                              this, &MainWindow::onClearScreen);
    clearAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    clearAction->setShortcutContext(Qt::ApplicationShortcut);

    session->addAction(tr("Open log folder"), this, &MainWindow::onOpenLogFolder);
    session->addSeparator();
    session->addAction(tr("Exit"), this, &QWidget::close);

    QMenu *settings = menuBar()->addMenu(tr("Se&ttings"));
    settings->addAction(tr("Serial port..."),   this, &MainWindow::dlgSerialPort);
    settings->addAction(tr("Log saving..."),    this, &MainWindow::dlgLogSaving);
    settings->addAction(tr("Terminal..."),      this, &MainWindow::dlgTerminalOptions);
    settings->addSeparator();
    settings->addAction(tr("Configurations..."), this, &MainWindow::dlgConfigurations);

    QMenu *tools = menuBar()->addMenu(tr("T&ools"));
    QAction *rulesAction = tools->addAction(tr("Color rules..."),
                                            this, &MainWindow::openColorRulesDialog);
    rulesAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    rulesAction->setShortcutContext(Qt::ApplicationShortcut);

    QAction *filterAction = tools->addAction(tr("New filter window..."),
                                             this, &MainWindow::openFilterWindow);
    filterAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    filterAction->setShortcutContext(Qt::ApplicationShortcut);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    m_ribbonAction = view->addAction(tr("Show ribbon"));
    m_ribbonAction->setCheckable(true);
    m_ribbonAction->setChecked(true);
    m_ribbonAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    m_ribbonAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_ribbonAction, &QAction::toggled, this, [this](bool on) {
        m_ribbon->setExpanded(on);
    });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("About %1").arg(App::NAME), this, &MainWindow::dlgAbout);
}

// ------------------------------------------------------------------ UI build

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_ribbon = new RibbonBar(central);
    root->addWidget(m_ribbon);
    buildRibbon();

    connect(m_ribbon, &RibbonBar::expandedChanged, this, [this](bool on) {
        m_state.settings.ribbonExpanded = on;
        if (m_ribbonAction->isChecked() != on) {
            QSignalBlocker block(m_ribbonAction);
            m_ribbonAction->setChecked(on);
        }
        scheduleConfigSave();
    });

    auto *terminalWrap = new QWidget(central);
    auto *termLayout = new QVBoxLayout(terminalWrap);
    termLayout->setContentsMargins(8, 8, 8, 8);
    m_terminal = new TerminalView(terminalWrap);
    connect(m_terminal, &TerminalView::keyTyped,      this, &MainWindow::onKeyTyped);
    connect(m_terminal, &TerminalView::copyRequested, this, &MainWindow::onCopySelection);
    termLayout->addWidget(m_terminal, 1);

    // --- transmit row -------------------------------------------------------
    auto *txRow = new QHBoxLayout;
    txRow->setSpacing(6);
    txRow->addWidget(fieldLabel(tr("Send"), terminalWrap));

    m_txInput = new QLineEdit(terminalWrap);
    m_txInput->setObjectName(QStringLiteral("txInput"));
    m_txInput->setPlaceholderText(
        tr("Type a command and press Enter  \u00b7  Up/Down recalls history"));
    m_txInput->setClearButtonEnabled(true);
    // Up/Down have to be caught before QLineEdit consumes them.
    m_txInput->installEventFilter(this);
    connect(m_txInput, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    txRow->addWidget(m_txInput, 1);

    m_sendBtn = new QPushButton(tr("Send"), terminalWrap);
    sizeButton(m_sendBtn, 80);
    connect(m_sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    txRow->addWidget(m_sendBtn);

    termLayout->addLayout(txRow);
    root->addWidget(terminalWrap, 1);

    m_statusLabel = new QLabel(m_currentStatus, this);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->setSizeGripEnabled(true);

    m_terminal->setFocus();
}

void MainWindow::buildRibbon()
{
    // --- Connection ---------------------------------------------------------
    RibbonGroup *conn = m_ribbon->addGroup(tr("Connection"));
    QGridLayout *g = conn->content();

    g->addWidget(fieldLabel(tr("Port"), conn), 0, 0, Qt::AlignRight);
    m_portCombo = new QComboBox(conn);
    m_portCombo->setEditable(true);
    m_portCombo->setMinimumWidth(104);
    g->addWidget(m_portCombo, 0, 1);

    auto *refreshBtn = new QPushButton(tr("Refresh"), conn);
    sizeButton(refreshBtn, 84);
    refreshBtn->setToolTip(tr("Re-scan for serial ports"));
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    g->addWidget(refreshBtn, 0, 2);

    g->addWidget(fieldLabel(tr("Baud"), conn), 1, 0, Qt::AlignRight);
    m_baudCombo = new QComboBox(conn);
    m_baudCombo->setEditable(true);
    m_baudCombo->addItems(App::BAUD_CHOICES);
    m_baudCombo->setMinimumWidth(104);
    g->addWidget(m_baudCombo, 1, 1);

    m_connectBtn = new QPushButton(tr("Connect"), conn);
    m_connectBtn->setObjectName(QStringLiteral("primaryButton"));
    sizeButton(m_connectBtn, 84, { tr("Disconnect") });
    m_connectBtn->setToolTip(tr("Open or close the serial port (F2)"));
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    g->addWidget(m_connectBtn, 1, 2);

    // --- Terminal -----------------------------------------------------------
    RibbonGroup *term = m_ribbon->addGroup(tr("Terminal"));
    g = term->content();

    auto *clearBtn = new QPushButton(tr("Clear"), term);
    sizeButton(clearBtn, 92);
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearScreen);
    g->addWidget(clearBtn, 0, 0);

    m_autoscrollBox = new QCheckBox(tr("Autoscroll"), term);
    g->addWidget(m_autoscrollBox, 0, 1, 1, 2);


    // --- Highlighting & filtering -------------------------------------------
    RibbonGroup *rules = m_ribbon->addGroup(tr("Highlighting and filtering"));
    g = rules->content();

    auto *rulesBtn = new QPushButton(tr("Color rules..."), rules);
    sizeButton(rulesBtn, 124);
    rulesBtn->setToolTip(tr("Define keyword colors for incoming lines (Ctrl+R)"));
    connect(rulesBtn, &QPushButton::clicked, this, &MainWindow::openColorRulesDialog);
    g->addWidget(rulesBtn, 0, 0);

    auto *filterBtn = new QPushButton(tr("Filter window"), rules);
    sizeButton(filterBtn, 124);
    filterBtn->setToolTip(tr("Open a window showing only the lines you filter for (Ctrl+F)"));
    connect(filterBtn, &QPushButton::clicked, this, &MainWindow::openFilterWindow);
    g->addWidget(filterBtn, 1, 0);

    // --- Configuration ------------------------------------------------------
    RibbonGroup *cfg = m_ribbon->addGroup(tr("Configuration"));
    g = cfg->content();

    m_configCombo = new QComboBox(cfg);
    m_configCombo->setMinimumWidth(150);
    m_configCombo->setToolTip(tr("Saved configurations"));
    g->addWidget(m_configCombo, 0, 0);

    auto *loadBtn = new QPushButton(tr("Load"), cfg);
    sizeButton(loadBtn, 72);
    connect(loadBtn, &QPushButton::clicked, this, [this] {
        loadConfigNamed(m_configCombo->currentText().trimmed());
    });
    g->addWidget(loadBtn, 0, 1);

    auto *deleteBtn = new QPushButton(tr("Delete"), cfg);
    sizeButton(deleteBtn, 72);
    connect(deleteBtn, &QPushButton::clicked, this, [this] {
        deleteConfigNamed(m_configCombo->currentText().trimmed());
    });
    g->addWidget(deleteBtn, 0, 2);

    m_configNameEdit = new QLineEdit(cfg);
    m_configNameEdit->setObjectName(QStringLiteral("configNameEdit"));
    m_configNameEdit->setMinimumWidth(150);
    m_configNameEdit->setPlaceholderText(tr("name to save as"));
    m_configNameEdit->setToolTip(tr("Keep the loaded name to update it, "
                                    "or type a new one to create a copy"));
    g->addWidget(m_configNameEdit, 1, 0);

    auto *saveBtn = new QPushButton(tr("Save"), cfg);
    sizeButton(saveBtn, 72);
    auto doSave = [this] { saveConfigNamed(m_configNameEdit->text().trimmed()); };
    connect(saveBtn, &QPushButton::clicked, this, doSave);
    connect(m_configNameEdit, &QLineEdit::returnPressed, this, doSave);
    g->addWidget(saveBtn, 1, 1, 1, 2);

    // --- keep the settings in step with the ribbon --------------------------
    connect(m_portCombo, &QComboBox::currentTextChanged, this, [this](const QString &v) {
        if (m_syncing) return;
        m_state.settings.port = v;
        updateRibbonSummary();
        scheduleConfigSave();
    });
    connect(m_baudCombo, &QComboBox::currentTextChanged, this, [this](const QString &v) {
        if (m_syncing) return;
        m_state.settings.baud = v;
        updateRibbonSummary();
        scheduleConfigSave();
    });
    connect(m_autoscrollBox, &QCheckBox::toggled, this, [this](bool v) {
        m_terminal->setAutoscroll(v);
        if (m_syncing) return;
        m_state.settings.autoscroll = v;
        scheduleConfigSave();
    });

    connect(this, &MainWindow::configListChanged, this, &MainWindow::refreshConfigCombo);
}

void MainWindow::syncFromSettings()
{
    m_syncing = true;
    m_portCombo->setCurrentText(m_state.settings.port);
    m_baudCombo->setCurrentText(m_state.settings.baud);
    m_autoscrollBox->setChecked(m_state.settings.autoscroll);
    m_syncing = false;
    m_terminal->setAutoscroll(m_state.settings.autoscroll);
    updateRibbonSummary();
}

void MainWindow::updateRibbonSummary()
{
    if (!m_ribbon)
        return;
    const QString port = m_state.settings.port.isEmpty() ? tr("no port")
                                                         : m_state.settings.port;
    QString text = tr("%1 · %2 baud · %3").arg(port, m_state.settings.baud, m_currentStatus);
    if (!m_activeConfig.isEmpty())
        text += tr("  ·  %1").arg(m_activeConfig);
    m_ribbon->setSummary(text);
}

void MainWindow::refreshConfigCombo()
{
    const QStringList names = ConfigStore::configNames();

    QSignalBlocker block(m_configCombo);
    const QString current = m_configCombo->currentText();
    m_configCombo->clear();
    m_configCombo->addItems(names);
    if (!m_activeConfig.isEmpty() && names.contains(m_activeConfig))
        m_configCombo->setCurrentText(m_activeConfig);
    else if (names.contains(current))
        m_configCombo->setCurrentText(current);
    else if (!names.isEmpty())
        m_configCombo->setCurrentIndex(0);
}

void MainWindow::setActiveConfig(const QString &name)
{
    m_activeConfig = name;
    // The save-name box always shows the loaded configuration, so pressing
    // Save writes the changes straight back to it.
    if (m_configNameEdit) {
        QSignalBlocker block(m_configNameEdit);
        m_configNameEdit->setText(name);
    }
    updateRibbonSummary();
    emit configListChanged();
}

// ------------------------------------------------------------- configuration

void MainWindow::scheduleConfigSave()
{
    m_saveTimer->start(App::AUTOSAVE_DELAY_MS);
}

void MainWindow::saveConfigNow()
{
    m_saveTimer->stop();
    ConfigStore::save(m_state, m_activeConfig);
}

// -------------------------------------------------------------------- ports

void MainWindow::onRefreshPorts()
{
    refreshPorts(true);
}

void MainWindow::refreshPorts(bool announce)
{
    m_knownPorts = availableSerialPorts();

    {
        QSignalBlocker block(m_portCombo);
        m_portCombo->clear();
        m_portCombo->addItems(m_knownPorts);

        // Only pick a port for the user when they have not chosen one yet, or
        // when the one they chose is no longer present.
        if (!m_knownPorts.isEmpty()
            && (m_state.settings.port.isEmpty()
                || !m_knownPorts.contains(m_state.settings.port))) {
            m_state.settings.port = m_knownPorts.first();
        }
        m_portCombo->setCurrentText(m_state.settings.port);
    }

    m_portCombo->setToolTip(m_knownPorts.isEmpty()
                                ? tr("No serial ports detected")
                                : tr("Detected: %1").arg(m_knownPorts.join(QStringLiteral(", "))));
    updateRibbonSummary();

    // Say something on an explicit refresh, so "nothing found" is visible
    // rather than an unexplained empty dropdown.
    if (announce)
        showTemporaryNotice(m_knownPorts.isEmpty()
                                ? tr("No serial ports detected")
                                : tr("Ports: %1").arg(m_knownPorts.join(QStringLiteral(", "))));
}

// -------------------------------------------------------------------- screen

void MainWindow::onClearScreen()
{
    m_terminal->clearScreen();
}

void MainWindow::onOpenLogFolder()
{
    const QString dir = App::effectiveLogDir(m_state.settings.logDir);
    if (!QDir(dir).exists()) {
        QMessageBox::information(
            this, tr("Log folder"),
            tr("The log folder does not exist yet:\n\n%1\n\n"
               "Turn on log saving under Settings > Log saving, then connect once "
               "to create it.").arg(App::displayPath(dir)));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

// -------------------------------------------------------------- connection

void MainWindow::onConnectClicked()
{
    if (m_worker) {
        m_worker->stop();
        m_worker->wait(2000);
        m_worker->deleteLater();
        m_worker = nullptr;
        m_currentStatus = tr("Not connected");
        m_connectBtn->setText(tr("Connect"));
        closeLog();
        updateStatusLine();
        updateRibbonSummary();
        return;
    }

    SerialWorker::Config cfg;
    bool baudOk = false;
    cfg.port      = m_state.settings.port.trimmed();
    cfg.baud      = m_state.settings.baud.trimmed().toInt(&baudOk);
    cfg.databits  = m_state.settings.databits.toInt();
    cfg.parity    = m_state.settings.parity;
    cfg.stopbits  = m_state.settings.stopbits;
    cfg.flow      = m_state.settings.flow;
    cfg.reconnect = m_state.settings.autoReconnect;

    if (cfg.port.isEmpty()) {
        QMessageBox::warning(this, tr("No port selected"),
                             tr("Choose a serial port first, then press Connect."));
        return;
    }
    if (!baudOk || cfg.baud <= 0 || cfg.databits < 5 || cfg.databits > 8) {
        QMessageBox::critical(this, tr("Invalid settings"),
                              tr("Check the serial settings: baud '%1', data bits '%2'.")
                                  .arg(m_state.settings.baud, m_state.settings.databits));
        return;
    }

    if (m_state.settings.logEnabled && !openLog(cfg.port))
        return;

    m_assembler.reset();
    m_rxBytes = 0;
    m_currentStatus = tr("Not connected");
    m_pending.clear();

    m_worker = new SerialWorker(cfg);
    connect(m_worker, &SerialWorker::dataReceived,  this, &MainWindow::onSerialData);
    connect(m_worker, &SerialWorker::statusChanged, this, &MainWindow::onSerialStatus);
    connect(m_worker, &SerialWorker::fatalError,    this, &MainWindow::onSerialFatal);
    m_worker->start();

    m_connectBtn->setText(tr("Disconnect"));
    m_terminal->setFocus();
    saveConfigNow();     // the settings that were actually used are worth keeping
}

// --------------------------------------------------------------- serial I/O
// Events are queued and drained on the 40 ms poll timer, so a device
// spraying at 2 Mbaud cannot starve the UI.

void MainWindow::onSerialData(const QByteArray &data)
{
    m_pending.append({ false, data, QString() });
}

void MainWindow::onSerialStatus(const QString &status)
{
    m_pending.append({ true, QByteArray(), status });
}

void MainWindow::onSerialFatal(const QString &message)
{
    appendSystemMessage(tr("error: ") + message);
    if (m_worker) {
        m_worker->stop();
        m_worker->wait(2000);   // run() has already returned; never delete a live QThread
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    m_connectBtn->setText(tr("Connect"));
    closeLog();
    m_currentStatus = tr("Not connected");
    updateStatusLine();
    updateRibbonSummary();
    QMessageBox::critical(this, tr("Serial port"), message);
}

void MainWindow::onPoll()
{
    if (!m_pending.isEmpty()) {
        const QVector<Event> events = std::move(m_pending);
        m_pending.clear();

        bool statusChanged = false;
        m_terminal->beginBatch();
        for (const Event &ev : events) {
            if (ev.isStatus) {
                appendSystemMessage(ev.status);
                m_currentStatus = ev.status;
                statusChanged = true;
                continue;
            }
            m_rxBytes += ev.data.size();
            const QStringList lines = m_assembler.feed(ev.data);
            for (const QString &line : lines)
                handleCompleteLine(line);
        }
        m_terminal->endBatch();
        if (statusChanged)
            updateRibbonSummary();
    }

    // An unterminated line that has gone quiet is shown as-is, so prompts
    // without a newline still appear.
    if (m_worker && m_assembler.hasPartial()
        && m_assembler.msSinceLastRx() > static_cast<qint64>(App::PARTIAL_FLUSH_S * 1000)) {
        const QString partial = m_assembler.takePartial();
        publish(makeRecord(partial, false), false);
    }

    if (m_logFile)
        m_logFile->flush();

    if (m_worker && !m_notificationActive)
        updateStatusLine();
}

DisplayOptions MainWindow::displayOptions() const
{
    DisplayOptions options;
    options.timestamps = m_state.settings.showTimestamps;
    options.direction  = m_state.settings.showDirection;
    options.mode       = m_state.settings.displayMode;
    return options;
}

LineRecord MainWindow::makeRecord(const QString &raw, bool isTx) const
{
    const DisplayOptions options = displayOptions();

    LineRecord record;
    record.raw    = raw;
    record.isTx   = isTx;
    record.prefix = LineFormat::prefix(options, isTx, QDateTime::currentDateTime());
    record.body   = LineFormat::body(options, raw);

    // Colour rules are matched against the raw text, so a rule keeps working
    // when the view is switched to hex.
    const ColorRule *rule = m_state.colorRules.firstMatch(raw);
    record.colorRule = rule ? rule->name : QString();

    if (isTx)
        record.color = QColor(App::TERM_TX);
    else if (rule && m_state.settings.colorize)
        record.color = rule->color;
    else
        record.color = QColor(App::TERM_FG);

    return record;
}

void MainWindow::publish(const LineRecord &record, bool newline)
{
    m_terminal->appendLine(record.prefix, QColor(App::TERM_META),
                           record.body, record.color, newline);

    // Only the raw bytes are logged: timestamps, direction markers, hex
    // rendering and colours are display concerns and never reach the file.
    if (!record.isTx)
        logWrite(record.raw, newline);

    if (newline) {
        for (FilterWindow *win : std::as_const(m_filterWindows))
            win->onLine(record);
    }
}

void MainWindow::handleCompleteLine(const QString &line)
{
    publish(makeRecord(line, false));
}

void MainWindow::appendSystemMessage(const QString &message)
{
    m_terminal->appendLine(QStringLiteral("--- %1 ---").arg(message),
                           QColor(App::TERM_SYSMSG), true);
}

// ------------------------------------------------------------------ keyboard

namespace {

/// The bytes the configured Line Ending appends. "None" adds nothing.
QByteArray lineEndingBytes(const QString &setting)
{
    if (setting == QLatin1String("LF"))   return QByteArrayLiteral("\n");
    if (setting == QLatin1String("CRLF")) return QByteArrayLiteral("\r\n");
    if (setting == QLatin1String("CR"))   return QByteArrayLiteral("\r");
    return {};   // None
}

} // namespace

void MainWindow::onKeyTyped(bool isReturn, const QString &text)
{
    // Typing straight into the terminal still works; the Send box is the
    // deliberate route for whole commands.
    if (!m_worker)
        return;

    QByteArray payload;
    if (isReturn)
        payload = lineEndingBytes(m_state.settings.eol);
    else
        payload = text.toLatin1();

    if (payload.isEmpty())
        return;

    m_worker->send(payload);

    if (m_state.settings.localEcho)
        m_terminal->appendLine(isReturn ? QString() : text,
                               QColor(App::TERM_TX), isReturn);
}

void MainWindow::onSendClicked()
{
    sendPayload(m_txInput->text());
}

void MainWindow::sendPayload(const QString &text)
{
    if (!m_worker) {
        showTemporaryNotice(tr("Not connected - press Connect first"));
        return;
    }
    if (text.isEmpty())
        return;

    m_worker->send(text.toLatin1() + lineEndingBytes(m_state.settings.eol));

    // Show what was sent in its own shade, so TX is distinguishable from the
    // device's own output.
    publish(makeRecord(text, true));

    // Remember it, most recent last, without consecutive duplicates.
    QStringList &history = m_state.settings.txHistory;
    if (history.isEmpty() || history.last() != text)
        history.append(text);
    while (history.size() > App::MAX_TX_HISTORY)
        history.removeFirst();

    m_historyIndex = -1;
    m_historyDraft.clear();
    m_txInput->clear();
    scheduleConfigSave();
}

void MainWindow::recallHistory(int direction)
{
    const QStringList &history = m_state.settings.txHistory;
    if (history.isEmpty())
        return;

    if (m_historyIndex < 0) {
        if (direction > 0)
            return;                       // already at the newest entry
        m_historyDraft = m_txInput->text();
        m_historyIndex = history.size();
    }

    const int next = m_historyIndex + direction;
    if (next < 0)
        return;                           // hold at the oldest entry

    if (next >= history.size()) {         // walked back past the newest
        m_historyIndex = -1;
        m_txInput->setText(m_historyDraft);
    } else {
        m_historyIndex = next;
        m_txInput->setText(history.at(next));
    }
    // Recalled text stays editable, with the caret ready at the end.
    m_txInput->setCursorPosition(m_txInput->text().size());
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_txInput && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Up) {
            recallHistory(-1);
            return true;
        }
        if (key->key() == Qt::Key_Down) {
            recallHistory(+1);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// -------------------------------------------------------------------- logging

QString MainWindow::expandTemplate(const QString &tmpl, const QString &port)
{
    const QDateTime now = QDateTime::currentDateTime();
    QString out = tmpl;
    out.replace(QStringLiteral("&Y"), now.toString(QStringLiteral("yyyy")));
    out.replace(QStringLiteral("&M"), now.toString(QStringLiteral("MM")));
    out.replace(QStringLiteral("&D"), now.toString(QStringLiteral("dd")));
    out.replace(QStringLiteral("&T"), now.toString(QStringLiteral("HHmmss")));
    out.replace(QStringLiteral("&H"), port);
    if (out.trimmed().isEmpty())
        out = QStringLiteral("uartx.log");
    return out;
}

bool MainWindow::openLog(const QString &port)
{
    QString dir, error;
    if (!validateLogDirectory(m_state.settings.logDir, &dir, &error)) {
        QMessageBox::critical(
            this, tr("Log folder unavailable"),
            tr("UartX cannot save logs to the selected folder, so the port was not "
               "opened.\n\n%1\n\nChange the folder under Settings > Log saving, or "
               "turn log saving off.").arg(error));
        return false;
    }

    // The folder is checked now so the user hears about a bad path before the
    // session starts, but the file itself waits for the first byte -- a
    // session that receives nothing should leave nothing behind.
    m_logPath = QDir(dir).filePath(expandTemplate(m_state.settings.logTemplate, port));
    m_logSessionStart = QDateTime::currentDateTime();
    m_logArmed = true;
    return true;
}

bool MainWindow::createLogFile()
{
    auto *file = new QFile(m_logPath);
    const QIODevice::OpenMode mode =
        QIODevice::WriteOnly | (m_state.settings.logAppend ? QIODevice::Append
                                                           : QIODevice::Truncate);
    if (!file->open(mode)) {
        // Mid-session, so report it in the terminal rather than with a modal
        // dialog, and stop trying for the rest of the session.
        appendSystemMessage(tr("log disabled: cannot open %1 (%2)")
                                .arg(App::displayPath(m_logPath), file->errorString()));
        delete file;
        m_logArmed = false;
        m_logPath.clear();
        return false;
    }

    m_logFile = file;
    const QString header = QStringLiteral("===== %1 log  %2  %3 @ %4 =====\n")
                               .arg(App::NAME,
                                    m_logSessionStart.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                                    m_state.settings.port, m_state.settings.baud);
    m_logFile->write(header.toUtf8());
    return true;
}

void MainWindow::closeLog()
{
    if (m_logFile) {
        m_logFile->flush();
        m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
        appendSystemMessage(tr("log saved: %1").arg(QFileInfo(m_logPath).fileName()));
    } else if (m_logArmed) {
        // Armed but never written to: no data arrived, so no file was ever
        // created and there is nothing to clean up.
        appendSystemMessage(tr("no data received - no log file was created"));
    }
    m_logArmed = false;
    m_logPath.clear();
}

void MainWindow::logWrite(const QString &text, bool newline)
{
    if (!m_logFile) {
        if (!m_logArmed)
            return;
        if (!createLogFile())
            return;
    }
    QString out = sanitizeForLog(text);
    if (newline)
        out.append(QLatin1Char('\n'));
    m_logFile->write(out.toUtf8());
}

// ------------------------------------------------------------- status + copy

void MainWindow::updateStatusLine()
{
    if (m_notificationActive)
        return;

    if (!m_worker) {
        m_statusLabel->setText(m_currentStatus);
        m_statusLabel->setToolTip(QString());
        return;
    }

    // The log file is named, not pathed: the status bar must not put an
    // account-specific location on screen. The full path is in the tooltip.
    const QString logName = m_logFile   ? QFileInfo(m_logPath).fileName()
                          : m_logArmed  ? tr("waiting for data")
                                        : tr("off");
    m_statusLabel->setText(tr("%1  |  %2 @ %3  |  RX %4 bytes  |  log: %5")
                               .arg(m_currentStatus,
                                    m_state.settings.port,
                                    m_state.settings.baud,
                                    QString::number(m_rxBytes),
                                    logName));
    m_statusLabel->setToolTip(m_logFile ? App::displayPath(m_logPath) : QString());
}

void MainWindow::onCopySelection()
{
    const QString selected = m_terminal->textCursor().selectedText();
    if (selected.isEmpty())
        return;

    QString plain = selected;
    plain.replace(QChar(0x2029), QLatin1Char('\n'));   // paragraph separator -> newline
    QApplication::clipboard()->setText(plain);

    QTextCursor cur = m_terminal->textCursor();
    cur.clearSelection();
    m_terminal->setTextCursor(cur);

    const int n = plain.size();
    showTemporaryNotice(tr("%1 character%2 copied")
                            .arg(n).arg(n != 1 ? QStringLiteral("s") : QString()));
}

void MainWindow::showTemporaryNotice(const QString &message)
{
    m_noticeTimer->stop();
    m_notificationActive = true;
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusLabel->setText(message);
    m_noticeTimer->start(App::NOTIFICATION_MS);
}

// --------------------------------------------------------- rules and filters

void MainWindow::openColorRulesDialog()
{
    ColorRulesDialog dlg(m_state.colorRules, this);
    connect(&dlg, &ColorRulesDialog::rulesChanged, this, &MainWindow::applyColorRules);
    dlg.exec();
}

void MainWindow::applyColorRules(const ColorRuleSet &rules)
{
    m_state.colorRules = rules;
    // The rules double as filter sources, so every open Filter window has to
    // pick up the change.
    for (FilterWindow *win : std::as_const(m_filterWindows))
        win->refreshSources(m_state.colorRules, m_state.textFilters);
    scheduleConfigSave();
}

void MainWindow::applyTextFilters(const TextFilterSet &filters)
{
    m_state.textFilters = filters;
    for (FilterWindow *win : std::as_const(m_filterWindows))
        win->refreshSources(m_state.colorRules, m_state.textFilters);
    scheduleConfigSave();
}

void MainWindow::openFilterWindow()
{
    auto *win = new FilterWindow(this, tr("Filter"));
    m_filterWindows.append(win);
    win->show();
}

void MainWindow::unregisterFilterWindow(FilterWindow *win)
{
    m_filterWindows.removeAll(win);
}

// --------------------------------------------------------- named configurations

void MainWindow::saveConfigNamed(const QString &name)
{
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Configuration name"),
                             tr("Enter a name to save this configuration under."));
        return;
    }

    // Saving under the loaded name is "save my changes" and needs no prompt;
    // overwriting a *different* existing configuration does.
    if (name != m_activeConfig && ConfigStore::configExists(name)) {
        const auto answer = QMessageBox::question(
            this, tr("Overwrite configuration"),
            tr("A configuration named '%1' already exists. Replace it?").arg(name));
        if (answer != QMessageBox::Yes)
            return;
    }

    const bool updating = (name == m_activeConfig);
    if (!ConfigStore::saveNamedConfig(name, m_state)) {
        QMessageBox::critical(this, tr("Configurations"),
                              tr("Could not save configuration '%1'.").arg(name));
        return;
    }

    setActiveConfig(name);
    saveConfigNow();
    showTemporaryNotice(updating ? tr("Configuration '%1' updated").arg(name)
                                 : tr("Configuration '%1' saved").arg(name));
}

void MainWindow::loadConfigNamed(const QString &name)
{
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Configurations"),
                             tr("Select a configuration to load."));
        return;
    }
    if (!ConfigStore::loadNamedConfig(name, &m_state)) {
        QMessageBox::critical(this, tr("Configurations"),
                              tr("Could not load configuration '%1'.").arg(name));
        return;
    }

    syncFromSettings();
    m_ribbon->setExpanded(m_state.settings.ribbonExpanded);
    for (FilterWindow *win : std::as_const(m_filterWindows))
        win->refreshSources(m_state.colorRules, m_state.textFilters);

    setActiveConfig(name);
    saveConfigNow();
    showTemporaryNotice(tr("Configuration '%1' loaded").arg(name));
}

void MainWindow::deleteConfigNamed(const QString &name)
{
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Configurations"),
                             tr("Select a configuration to delete."));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Delete configuration"),
        tr("Delete the configuration '%1'? This cannot be undone.").arg(name));
    if (answer != QMessageBox::Yes)
        return;

    if (!ConfigStore::deleteNamedConfig(name)) {
        QMessageBox::critical(this, tr("Configurations"),
                              tr("Could not delete configuration '%1'.").arg(name));
        return;
    }

    if (m_activeConfig == name)
        setActiveConfig(QString());
    else
        emit configListChanged();
    saveConfigNow();
    showTemporaryNotice(tr("Configuration '%1' deleted").arg(name));
}

// ------------------------------------------------------------------- dialogs

void MainWindow::dlgSerialPort()      { (new SerialPortDialog(this))->show(); }
void MainWindow::dlgLogSaving()       { (new LogSavingDialog(this))->show(); }
void MainWindow::dlgTerminalOptions() { (new TerminalOptionsDialog(this))->show(); }
void MainWindow::dlgConfigurations()  { (new ConfigurationsDialog(this))->show(); }
void MainWindow::dlgAbout()           { (new AboutDialog(this))->show(); }

// --------------------------------------------------------------------- close

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_worker) {
        m_worker->stop();
        m_worker->wait(2000);
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    m_noticeTimer->stop();
    m_pollTimer->stop();
    m_currentStatus = tr("Not connected");
    closeLog();

    const QVector<FilterWindow *> windows = m_filterWindows;   // close() unregisters
    for (FilterWindow *win : windows)
        win->close();
    m_filterWindows.clear();

    saveConfigNow();
    e->accept();
}
