#include "mainwindow.h"
#include "terminalview.h"
#include "filterwindow.h"
#include "colorrulesdialog.h"
#include "ribbon.h"
#include "dialogs.h"
#include "lineformat.h"
#include "theme.h"
#include "session.h"
#include "textdecoder.h"
#include "controlserver.h"
#include "linequery.h"

#include <QMenuBar>
#include <QMenu>
#include <QSplitter>
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
#include <QLocale>
#include <QPointer>
#include <QJsonArray>
#include <QThread>

namespace {

/// Log cleanup runs shortly after startup -- not during it, so a large log
/// folder never delays the window -- and then every hour.
constexpr int RETENTION_FIRST_MS  = 5000;
constexpr int RETENTION_PERIOD_MS = 60 * 60 * 1000;

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

    m_retentionTimer = new QTimer(this);
    connect(m_retentionTimer, &QTimer::timeout, this, &MainWindow::runRetention);
    m_retentionTimer->start(RETENTION_PERIOD_MS);
    QTimer::singleShot(RETENTION_FIRST_MS, this, &MainWindow::runRetention);
    m_ioPool.setMaxThreadCount(1);

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
    startAgentApi();
}

MainWindow::~MainWindow()
{
    stopAgentApi();
    closeSession();
    m_ioPool.waitForDone();
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

    // The terminal and any docked filter views share one horizontal splitter,
    // so a filtered view sits beside the full stream instead of covering it.
    // The splitter is always present, even with nothing docked, so adding the
    // first view does not relayout the whole window.
    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    m_splitter->addWidget(terminalWrap);
    m_splitter->setStretchFactor(0, 3);
    root->addWidget(m_splitter, 1);

    m_statusLabel = new QLabel(m_currentStatus, this);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_statusLabel, 1);

    // Says whether an agent is connected and what it last did, so the user
    // never has to wonder whether something is acting on the session.
    m_agentLabel = new QLabel(this);
    m_agentLabel->setStyleSheet(QStringLiteral("color: %1; padding-right: 8px;").arg(App::TERM_AGENT));
    m_agentLabel->hide();
    statusBar()->addPermanentWidget(m_agentLabel);
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
    // Wide enough for the "no ports" message below to be read rather than
    // elided to "No ports..."; port names themselves are far shorter.
    m_portCombo->setMinimumWidth(148);
    // The box is left empty when nothing is detected, so it needs to say why.
    m_portCombo->lineEdit()->setPlaceholderText(tr("No ports found"));
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
    rulesBtn->setToolTip(App::nativeShortcuts(tr("Define keyword colors for incoming lines (Ctrl+R)")));
    connect(rulesBtn, &QPushButton::clicked, this, &MainWindow::openColorRulesDialog);
    g->addWidget(rulesBtn, 0, 0);

    auto *filterBtn = new QPushButton(tr("Filter window"), rules);
    sizeButton(filterBtn, 124);
    filterBtn->setToolTip(App::nativeShortcuts(tr("Open a window showing only the lines you filter for (Ctrl+F)")));
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
    // A loaded configuration carries the port it was saved with. Honouring it
    // while the machine has no ports at all would put a dead name back in a
    // box that is meant to be empty in exactly that case.
    if (m_knownPorts.isEmpty())
        m_state.settings.port.clear();
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
    if (m_session)
        m_session->setDisplay(displayOptions(), m_state.settings.colorize,
                              m_state.settings.deviceColors);
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

        if (m_knownPorts.isEmpty()) {
            // Nothing is connected. Leaving the previous session's port in the
            // box is a lie the user cannot see through -- an unplugged COM5
            // looks exactly like a present one -- and Connect would then fail
            // with an error about a port the machine never offered. Clear it
            // and let the placeholder say so. A port the scan does not know
            // can still be typed in; this is the same rule already applied
            // when the scan does find something.
            m_state.settings.port.clear();
        } else if (m_state.settings.port.isEmpty()
                   || !m_knownPorts.contains(m_state.settings.port)) {
            // Only pick a port for the user when they have not chosen one yet,
            // or when the one they chose is no longer present.
            m_state.settings.port = m_knownPorts.first();
        }
        m_portCombo->setCurrentText(m_state.settings.port);
    }

    m_portCombo->setToolTip(m_knownPorts.isEmpty()
                                ? tr("No serial ports detected - plug a device in and press Refresh")
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

void MainWindow::closeSession()
{
    if (!m_session)
        return;
    Session *session = m_session;
    m_session = nullptr;       // callers test m_session; nothing may reach it now

    session->close();
    // Batches the pipeline emitted just before it stopped are still queued for
    // this thread; deleteLater lets them arrive and be drawn first.
    session->deleteLater();

    m_bridge->update([](SessionSnapshot &s) {
        s.open = false;
        s.status = QStringLiteral("closed");
    });

    if (m_logState.created && !m_logState.stopped)
        appendSystemMessage(tr("log saved: %1").arg(QFileInfo(m_logState.sessionDir).fileName()));
    else if (m_logState.enabled && !m_logState.created)
        appendSystemMessage(tr("no data received - no log was created"));
}

/// The session folder name: the user's template, without the ".log" it was
/// written for when it named a single file, plus the session id so two
/// sessions can never collide.
static QString sessionFolderName(const QString &expanded, const QString &sessionId)
{
    QString name = expanded;
    if (name.endsWith(QLatin1String(".log"), Qt::CaseInsensitive))
        name.chop(4);
    if (name.trimmed().isEmpty())
        name = QStringLiteral("uartx");
    return name + QLatin1Char('_') + sessionId;
}

PipelineConfig MainWindow::pipelineConfig(const QString &port, const QString &logDir) const
{
    const AppSettings &s = m_state.settings;
    PipelineConfig pc;
    pc.framing.mode         = Framing::modeFromName(s.framing);
    pc.framing.terminator   = Framing::parseTerminator(s.frameTerminator);
    pc.framing.idleGapMs    = s.frameIdleMs;
    pc.framing.fixedLength  = s.frameFixedLength;
    pc.framing.quietFlushMs = int(App::PARTIAL_FLUSH_S * 1000);
    pc.encoding     = s.encoding;
    pc.display      = displayOptions();
    pc.rules        = m_state.colorRules;
    pc.colorize     = s.colorize;
    pc.deviceColors = s.deviceColors;
    // No point holding more undrawn lines than the terminal can show.
    pc.maxPendingDisplay = App::MAX_SCROLLBACK_LINES;
    pc.pollIntervalMs    = App::POLL_INTERVAL_MS;
    pc.logEnabled  = s.logEnabled;
    pc.log.baseDir = logDir;
    pc.log.port    = port;

    // What the session was opened with goes into session.json, so a log read
    // months later still says which baud rate and framing produced it. The
    // command history is the user's, not the session's, and stays out.
    QJsonObject settings = s.toJson();
    settings.remove(QStringLiteral("txHistory"));
    pc.log.settings = settings;
    return pc;
}

void MainWindow::onConnectClicked()
{
    if (m_session) {
        closeSession();
        m_currentStatus = tr("Not connected");
        m_connectBtn->setText(tr("Connect"));
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

    // The folder is checked now so the user hears about a bad path before the
    // session starts; the files themselves wait for the first byte.
    QString logDir;
    if (m_state.settings.logEnabled) {
        QString error;
        if (!validateLogDirectory(m_state.settings.logDir, &logDir, &error)) {
            QMessageBox::critical(
                this, tr("Log folder unavailable"),
                tr("UartX cannot save logs to the selected folder, so the port was not "
                   "opened.\n\n%1\n\nChange the folder under Settings > Log saving, or "
                   "turn log saving off.").arg(error));
            return;
        }
    }

    m_rxBytes = 0;
    m_totalLines = 0;
    m_logState = LogState();
    m_currentStatus = tr("Not connected");

    m_session = new Session(this);
    PipelineConfig pc = pipelineConfig(cfg.port, logDir);
    pc.log.folderName = sessionFolderName(expandTemplate(m_state.settings.logTemplate, cfg.port),
                                          m_session->id());

    connect(m_session, &Session::batchReady,      this, &MainWindow::onBatch);
    connect(m_session, &Session::statusChanged,   this, &MainWindow::onSerialStatus);
    connect(m_session, &Session::fatalError,      this, &MainWindow::onSerialFatal);
    connect(m_session, &Session::logStateChanged, this, &MainWindow::onLogState);
    m_session->open(cfg, pc);
    publishSnapshot();

    m_connectBtn->setText(tr("Disconnect"));
    m_terminal->setFocus();
    updateStatusLine();
    saveConfigNow();     // the settings that were actually used are worth keeping
}

// --------------------------------------------------------------- serial I/O
// The pipeline thread does the work -- framing, decoding, matching, logging --
// and hands over one batch per poll interval. All that is left here is
// drawing it, so a device spraying at 2 Mbaud cannot starve the UI.

void MainWindow::onBatch(DisplayBatchPtr batch)
{
    // A batch from a session that has since been closed is still worth
    // drawing -- it is the tail of that session's data -- but it must not be
    // acknowledged to, or counted for, whatever session is open now.
    const bool current = m_session && sender() == m_session;

    m_terminal->beginBatch();
    for (const DisplayItem &item : std::as_const(batch->items)) {
        switch (item.kind) {
        case DisplayItem::Kind::System:
            appendSystemMessage(item.line.body);
            break;
        case DisplayItem::Kind::Gap:
            appendSystemMessage(item.line.body, QColor(App::TERM_WARN));
            break;
        case DisplayItem::Kind::Line: {
            const DisplayLine &line = item.line;
            if (!line.spans.isEmpty())
                m_terminal->appendSpans(line.prefix, QColor(App::TERM_META),
                                        line.spans, line.color, item.newline);
            else
                m_terminal->appendLine(line.prefix, QColor(App::TERM_META),
                                       line.body, line.color, item.newline);
            // Filter views see each line once, complete: a line the quiet
            // timer showed early reaches them when its end arrives.
            if (item.wholeValid) {
                for (FilterWindow *win : std::as_const(m_filterWindows))
                    win->onLine(item.whole);
            }
            break;
        }
        }
    }
    m_terminal->endBatch();

    if (current) {
        m_rxBytes = batch->rxBytes;
        m_totalLines = batch->totalLines;
        m_bridge->update([&batch](SessionSnapshot &s) {
            s.totalLines = batch->totalLines;
            s.rxBytes = batch->rxBytes;
        });
        m_session->batchRendered();
        if (!m_notificationActive)
            updateStatusLine();
    }
}

void MainWindow::onSerialStatus(const QString &status)
{
    // The status line itself reaches the screen through the pipeline, in
    // order with the data; this only keeps the status bar and ribbon current.
    m_currentStatus = status;
    m_bridge->update([&status](SessionSnapshot &s) { s.status = status; });
    updateRibbonSummary();
    updateStatusLine();
}

void MainWindow::onSerialFatal(const QString &message)
{
    appendSystemMessage(tr("error: ") + message);
    closeSession();
    m_connectBtn->setText(tr("Connect"));
    m_currentStatus = tr("Not connected");
    updateStatusLine();
    updateRibbonSummary();
    QMessageBox::critical(this, tr("Serial port"), message);
}

void MainWindow::onLogState(const LogState &state)
{
    if (sender() != m_session)
        return;
    const bool newlyStopped = state.stopped && !m_logState.stopped;
    m_logState = state;
    m_bridge->update([&state](SessionSnapshot &s) { s.log = state; });
    if (newlyStopped)
        appendSystemMessage(tr("LOG STOPPED: %1 - nothing more is being saved")
                                .arg(state.reason),
                            QColor(App::TERM_ERROR));
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

void MainWindow::appendSystemMessage(const QString &message, const QColor &color)
{
    m_terminal->appendLine(QStringLiteral("--- %1 ---").arg(message),
                           color.isValid() ? color : QColor(App::TERM_SYSMSG), true);
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
    if (!m_session)
        return;

    TextDecoder codec;
    codec.setEncoding(m_state.settings.encoding);
    QByteArray payload;
    if (isReturn)
        payload = lineEndingBytes(m_state.settings.eol);
    else
        payload = codec.encode(text);

    if (payload.isEmpty())
        return;

    m_session->sendTyped(payload);

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
    if (!m_session) {
        showTemporaryNotice(tr("Not connected - press Connect first"));
        return;
    }
    if (text.isEmpty())
        return;

    // Encoded the way the session decodes, so text that is not plain ASCII
    // reaches the device as the same characters it would print back. The
    // pipeline records it and draws it in the TX shade, in order with the
    // device's output.
    TextDecoder codec;
    codec.setEncoding(m_state.settings.encoding);
    m_session->send(codec.encode(text) + lineEndingBytes(m_state.settings.eol));

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

/// The port name, reduced to something that can sit inside a file name.
///
/// A Windows port is already safe ("COM5"), but a POSIX one is a path
/// ("/dev/ttyUSB0"). Substituted verbatim it turns the log file name into a
/// nested path whose directories do not exist, and the file silently fails to
/// open -- so the separators have to go before the name is built.
static QString portFileToken(const QString &port)
{
    QString token = serialPortDisplayName(port);   // drops the "/dev/" prefix

    // Whatever is left must be legal in a file name on either platform: a
    // sub-path such as /dev/serial/by-id/... still carries separators, and the
    // Windows-reserved characters are excluded so one config file can be
    // carried between machines.
    for (QChar &c : token) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
            || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('"')
            || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('|')
            || c.unicode() < 0x20) {
            c = QLatin1Char('_');
        }
    }
    return token;
}

QString MainWindow::expandTemplate(const QString &tmpl, const QString &port)
{
    const QDateTime now = QDateTime::currentDateTime();
    QString out = tmpl;
    out.replace(QStringLiteral("&Y"), now.toString(QStringLiteral("yyyy")));
    out.replace(QStringLiteral("&M"), now.toString(QStringLiteral("MM")));
    out.replace(QStringLiteral("&D"), now.toString(QStringLiteral("dd")));
    out.replace(QStringLiteral("&T"), now.toString(QStringLiteral("HHmmss")));
    out.replace(QStringLiteral("&H"), portFileToken(port));
    if (out.trimmed().isEmpty())
        out = QStringLiteral("uartx.log");
    return out;
}

// ----------------------------------------------------------------- retention

RetentionPolicy MainWindow::retentionPolicy() const
{
    RetentionPolicy policy;
    policy.maxAgeSecs = m_state.settings.retentionSecs;
    policy.maxBytes   = m_state.settings.retentionBytes;
    return policy;
}

void MainWindow::runRetention()
{
    if (m_retentionRunning)
        return;
    const QString base = App::effectiveLogDir(m_state.settings.logDir);
    if (!QDir(base).exists())
        return;

    QSet<QString> active;
    if (m_session && m_logState.created)
        active.insert(QDir::cleanPath(QDir::fromNativeSeparators(m_logState.runDir)));

    m_retentionRunning = true;
    const RetentionPolicy policy = retentionPolicy();
    QPointer<MainWindow> self(this);
    m_ioPool.start([self, base, policy, active] {
        const RetentionReport report =
            applyRetention(base, policy, active, QDateTime::currentDateTime());
        // The window waits for this pool before it is destroyed, so it is
        // still there to receive the result; the guard is belt and braces.
        QMetaObject::invokeMethod(qApp, [self, report] {
            if (self)
                self->onRetentionDone(report);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onRetentionDone(const RetentionReport &report)
{
    m_retentionRunning = false;
    if (!report.deletedRuns.isEmpty())
        showTemporaryNotice(tr("Log cleanup: removed %1 old run(s), %2 MB freed")
                                .arg(report.deletedRuns.size())
                                .arg(report.freedBytes / (1024 * 1024)));
    if (report.activeOverCap) {
        // Nothing active is ever deleted, so this can only be said, not
        // fixed: the user decides whether to stop the session.
        const QString warning = tr("the current log alone is larger than the log size "
                                   "limit; nothing was deleted");
        appendSystemMessage(warning, QColor(App::TERM_WARN));
        if (m_session)
            m_session->recordSystem(warning);
    }
}

// ------------------------------------------------------------- status + copy

void MainWindow::updateStatusLine()
{
    if (m_notificationActive)
        return;

    const RetentionPolicy policy = retentionPolicy();
    if (!m_session) {
        m_statusLabel->setText(m_currentStatus);
        m_statusLabel->setToolTip(m_state.settings.logEnabled
                                      ? tr("Log saving is on (%1)").arg(policy.summary())
                                      : QString());
        return;
    }

    // The log folder is named, not pathed: the status bar must not put an
    // account-specific location on screen. The full path is in the tooltip.
    QString log;
    if (!m_logState.enabled)
        log = tr("off");
    else if (m_logState.stopped)
        log = tr("STOPPED");
    else if (!m_logState.created)
        log = tr("waiting for data");
    else
        log = QFileInfo(m_logState.sessionDir).fileName();
    if (m_logState.enabled && !m_logState.stopped)
        log += QStringLiteral(" (%1)").arg(policy.summary());

    m_statusLabel->setText(tr("%1  |  %2 @ %3  |  RX %4 bytes  |  %5 lines  |  log: %6")
                               .arg(m_currentStatus,
                                    m_state.settings.port,
                                    m_state.settings.baud,
                                    QString::number(m_rxBytes),
                                    QLocale().toString(qulonglong(m_totalLines)),
                                    log));
    QString tip;
    if (m_logState.stopped)
        tip = tr("Log writing stopped: %1").arg(m_logState.reason);
    else if (m_logState.created)
        tip = App::displayPath(m_logState.sessionDir);
    m_statusLabel->setToolTip(tip);
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
    if (m_session)
        m_session->setRules(m_state.colorRules);
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

FilterWindow *MainWindow::createFilterWindow(const QString &title)
{
    auto *win = new FilterWindow(this, title);
    m_filterWindows.append(win);

    connect(win, &FilterWindow::dockRequested, this, &MainWindow::setFilterWindowDocked);
    connect(win, &FilterWindow::closeRequested, this, [this](FilterWindow *w) {
        // The user always wins: an agent's view closes like any other, and
        // the agent finds out the next time it lists its views.
        if (w->isAgentOwned()) {
            m_agentViews.remove(w->agentId());
            appendAgentLine(tr("You closed %1's view \u201c%2\u201d").arg(w->agentOwner(), w->agentName()),
                            App::TERM_AGENT);
        }
        unregisterFilterWindow(w);
        w->deleteLater();
    });
    return win;
}

void MainWindow::openFilterWindow()
{
    FilterWindow *win = createFilterWindow(tr("Filter"));

    // The first view docks beside the terminal; every one after it opens as a
    // window of its own. Stacking them all into the splitter divided the width
    // again with each new view, until none of them was wide enough to read a
    // log line -- and the terminal they were meant to be read against was the
    // first thing squeezed out.
    if (dockedFilterWindow())
        win->setDocked(false);      // emits dockRequested, which floats it
    else
        setFilterWindowDocked(win, true);
}

FilterWindow *MainWindow::dockedFilterWindow() const
{
    for (int i = 0; i < m_splitter->count(); ++i)
        if (auto *win = qobject_cast<FilterWindow *>(m_splitter->widget(i)))
            return win;
    return nullptr;
}

void MainWindow::setFilterWindowDocked(FilterWindow *win, bool docked)
{
    if (docked) {
        // One view at a time in the dock: whichever is there makes way, rather
        // than the two of them sharing a space sized for one.
        if (FilterWindow *current = dockedFilterWindow(); current && current != win)
            current->setDocked(false);

        // The flag survives reparenting, so clearing it is what actually turns
        // the floating window back into a plain child widget.
        win->setWindowFlag(Qt::Window, false);
        m_splitter->addWidget(win);
        // Two thirds terminal, one third filter: enough of the filtered view
        // to read, without losing sight of the stream it came from.
        const int total = qMax(m_splitter->width(), 600);
        m_splitter->setSizes({ total * 2 / 3, total / 3 });
        win->show();
    } else {
        // Taking it out of the splitter first stops the layout briefly
        // reparenting it back while the window flags change.
        win->setParent(nullptr);
        win->setWindowFlag(Qt::Window, true);
        win->resize(1000, 620);
        win->show();
        win->raise();
    }
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

// ------------------------------------------------------------------ agent API
// An AI agent (Claude Code, through uartx-ctl) asks for what it needs and
// UartX sends back only that. Everything it does lands here, on the GUI
// thread, and is shown to the user as it happens.

namespace {

/// Agent views open as views of their own; past this many the agent has to
/// close one first, so it cannot bury the screen.
constexpr int MAX_AGENT_VIEWS = 8;

QJsonObject agentError(const QString &code, const QString &message)
{
    return QJsonObject{ { QStringLiteral("error"), code }, { QStringLiteral("message"), message } };
}

QString agentDisplayName(const QString &agent)
{
    // "claude#2" -> "Claude #2"
    QString name = agent;
    name.replace(QLatin1Char('#'), QStringLiteral(" #"));
    return name.left(1).toUpper() + name.mid(1);
}

} // namespace

void MainWindow::startAgentApi()
{
    m_controlThread = new QThread(this);
    m_controlThread->setObjectName(QStringLiteral("agent-api"));
    m_control = new ControlServer(m_bridge, this, this);
    m_control->moveToThread(m_controlThread);
    connect(m_controlThread, &QThread::finished, m_control, &QObject::deleteLater);
    m_controlThread->start();
    QMetaObject::invokeMethod(m_control, "start", Qt::QueuedConnection);
}

void MainWindow::stopAgentApi()
{
    if (!m_controlThread)
        return;
    QThread *thread = m_controlThread;
    m_controlThread = nullptr;
    QMetaObject::invokeMethod(m_control, "stop", Qt::QueuedConnection);
    thread->quit();
    // The control thread may be in the middle of a blocking call into this
    // window; keep serving those while it winds down, or each would wait on
    // the other.
    while (!thread->wait(10))
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_control = nullptr;
}

void MainWindow::publishSnapshot()
{
    if (!m_session)
        return;
    const QString port = m_state.settings.port;
    const QString baud = m_state.settings.baud;
    const QString encoding = m_state.settings.encoding;
    const QString id = m_session->id();
    const QString status = m_currentStatus;
    auto ring = m_session->ring();
    m_bridge->update([=](SessionSnapshot &s) {
        s = SessionSnapshot();
        s.open = true;
        s.id = id;
        s.port = port;
        s.baud = baud;
        s.encoding = encoding;
        s.status = status;
        s.ring = ring;
    });
}

void MainWindow::appendAgentLine(const QString &text, const QString &color)
{
    m_terminal->appendLine(text, QColor(color), true);
}

void MainWindow::agentActivity(const QString &agent, const QString &sentence)
{
    const QString who = agentDisplayName(agent);
    appendAgentLine(QStringLiteral("◆ %1 %2").arg(who, sentence), App::TERM_AGENT);
    m_agentLabel->setText(QStringLiteral("● %1 · %2")
                              .arg(who, QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
    m_agentLabel->setToolTip(tr("Last action: %1").arg(sentence));
    m_agentLabel->show();
}

QVector<DisplayLine> MainWindow::recentDisplayLines(int maxRecords) const
{
    QVector<DisplayLine> out;
    if (!m_session)
        return out;
    const auto ring = m_session->ring();
    if (!ring)
        return out;
    TextDecoder decoder;
    decoder.setEncoding(m_state.settings.encoding);
    const DisplayOptions options = displayOptions();

    // A line shown early by the quiet timer is stored in pieces; a view wants
    // it whole, the way it would have arrived had the device not paused.
    LineRecord pending;
    bool havePending = false;
    for (const LineRecord &r : ring->tail(maxRecords)) {
        if (r.dir == Direction::Sys || r.has(LineFlag::Typed))
            continue;
        LineRecord rec = r;
        if (r.has(LineFlag::Partial) || r.has(LineFlag::Continuation)) {
            if (!havePending) {
                pending = r;
                pending.bytes.clear();
                havePending = true;
            }
            pending.bytes += r.bytes;
            if (r.has(LineFlag::Partial))
                continue;
            rec = pending;
            havePending = false;
        }
        out.append(Pipeline::makeDisplayLine(rec, decoder.decode(rec.bytes), options,
                                             m_state.colorRules, m_state.settings.colorize,
                                             m_state.settings.deviceColors));
    }
    return out;
}

QJsonObject MainWindow::agentViewJson(const FilterWindow *win) const
{
    return QJsonObject{ { "id", win->agentId() },
                        { "name", win->agentName() },
                        { "owner", win->agentOwner() },
                        { "pattern", win->agentQuery().pattern() },
                        { "regex", win->agentQuery().isRegex() },
                        { "shown", win->shownCount() },
                        { "docked", win->isDocked() } };
}

QJsonObject MainWindow::agentOpen(const QString &agent, const QJsonObject &p)
{
    const QString port = p.value("port").toString(m_state.settings.port).trimmed();
    const QString baud = p.value("baud").toString(m_state.settings.baud).trimmed();
    if (m_session) {
        if (port.compare(m_state.settings.port, Qt::CaseInsensitive) == 0)
            return QJsonObject{ { "session", m_session->id() }, { "port", m_state.settings.port },
                                { "baud", m_state.settings.baud }, { "already", true } };
        return agentError(QStringLiteral("PORT_BUSY"),
                          tr("%1 is open in UartX; close it first").arg(m_state.settings.port));
    }
    if (port.isEmpty())
        return agentError(QStringLiteral("NOT_FOUND"), tr("no port given and none selected"));

    m_state.settings.port = port;
    m_state.settings.baud = baud;
    syncFromSettings();
    onConnectClicked();
    if (!m_session)
        return agentError(QStringLiteral("PORT_BUSY"), tr("could not open %1").arg(port));

    agentActivity(agent, tr("opened %1 at %2 baud").arg(port, baud));
    return QJsonObject{ { "session", m_session->id() }, { "port", port }, { "baud", baud } };
}

QJsonObject MainWindow::agentFilterAdd(const QString &agent, const QJsonObject &p)
{
    const QString name = p.value("name").toString().trimmed().left(48);
    const LineQuery query(p.value("pattern").toString(), p.value("regex").toBool(),
                          p.value("case").toBool());
    if (name.isEmpty())
        return agentError(QStringLiteral("INVALID_PATTERN"), tr("a view needs a name"));
    if (!query.isValid() || query.pattern().isEmpty())
        return agentError(QStringLiteral("INVALID_PATTERN"),
                          query.isValid() ? tr("a view needs a pattern") : query.error());
    if (m_agentViews.size() >= MAX_AGENT_VIEWS)
        return agentError(QStringLiteral("LIMIT_EXCEEDED"),
                          tr("%1 agent views are open; close one first").arg(MAX_AGENT_VIEWS));

    const QString id = QStringLiteral("v%1").arg(m_nextAgentView++);
    FilterWindow *win = createFilterWindow(name);
    win->makeAgentView(agent, id, name, query);
    m_agentViews.insert(id, win);
    // The newest view goes beside the terminal, where the user is looking;
    // whatever was docked there floats out rather than disappearing.
    setFilterWindowDocked(win, true);
    win->backfill(recentDisplayLines(App::MAX_SCROLLBACK_LINES));

    agentActivity(agent, tr("opened the view “%1” showing lines matching %2 (%3 already)")
                             .arg(name, query.describe())
                             .arg(win->shownCount()));
    return agentViewJson(win);
}

QJsonObject MainWindow::agentAction(const QString &agent, const QString &method, const QJsonObject &p)
{
    if (method == QLatin1String("open"))
        return agentOpen(agent, p);

    if (method == QLatin1String("close")) {
        if (!m_session)
            return agentError(QStringLiteral("NOT_FOUND"), tr("no session is open"));
        const QString port = m_state.settings.port;
        onConnectClicked();      // toggles: closes the open session
        agentActivity(agent, tr("closed %1").arg(port));
        return QJsonObject{ { "closed", port } };
    }

    if (method == QLatin1String("filter.add"))
        return agentFilterAdd(agent, p);

    if (method == QLatin1String("filter.list")) {
        QJsonArray views;
        for (const FilterWindow *win : std::as_const(m_agentViews))
            views.append(agentViewJson(win));
        return QJsonObject{ { "views", views } };
    }

    if (method == QLatin1String("filter.rm")) {
        const QString id = p.value("id").toString();
        FilterWindow *win = m_agentViews.value(id);
        // Only agent views are in this table, so an agent can never close a
        // view the user made.
        if (!win)
            return agentError(QStringLiteral("NOT_FOUND"), tr("no agent view '%1'").arg(id));
        const QString name = win->agentName();
        m_agentViews.remove(id);
        unregisterFilterWindow(win);
        win->hide();
        win->deleteLater();
        agentActivity(agent, tr("closed its view “%1”").arg(name));
        return QJsonObject{ { "removed", id } };
    }

    if (method == QLatin1String("note")) {
        const QString text = p.value("text").toString().trimmed().left(2000);
        if (text.isEmpty())
            return agentError(QStringLiteral("INVALID_PATTERN"), tr("a note needs text"));
        // The explanation is the line a person who relies on the agent reads
        // first, so it stands out -- and it goes into lines.log too, beside
        // the data it explains.
        const QString who = agentDisplayName(agent);
        const QStringList paragraphs = text.split(QLatin1Char('\n'));
        for (int i = 0; i < paragraphs.size(); ++i)
            appendAgentLine((i == 0 ? QStringLiteral("\U0001F4AC %1: ").arg(who) : QStringLiteral("    "))
                                + paragraphs[i],
                            App::TERM_NOTE);
        if (m_session)
            m_session->recordSystem(QStringLiteral("note from %1: %2").arg(agent, text));
        return QJsonObject{ { "shown", true } };
    }

    return agentError(QStringLiteral("NOT_FOUND"), tr("unknown action '%1'").arg(method));
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
    stopAgentApi();
    closeSession();
    m_noticeTimer->stop();
    m_retentionTimer->stop();
    m_currentStatus = tr("Not connected");

    // Docked views are children of the splitter and would go with it, but the
    // floated ones are parentless and have to be taken down by hand.
    const QVector<FilterWindow *> windows = m_filterWindows;
    m_filterWindows.clear();
    for (FilterWindow *win : windows) {
        win->hide();
        win->deleteLater();
    }

    saveConfigNow();
    e->accept();
}
