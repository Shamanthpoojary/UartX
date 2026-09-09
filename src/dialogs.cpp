#include "dialogs.h"
#include "mainwindow.h"
#include "appconstants.h"
#include "configstore.h"
#include "lineformat.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTimer>
#include <QPixmap>
#include <QIcon>
#include <QTextBrowser>
#include <QSignalBlocker>

namespace {

QLabel *hintLabel(const QString &text, QWidget *parent = nullptr)
{
    auto *l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("color: #777777;"));
    return l;
}

QFrame *hSeparator(QWidget *parent = nullptr)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

} // namespace

// ---------------------------------------------------------------------------
// Log folder validation
// ---------------------------------------------------------------------------

bool validateLogDirectory(const QString &configured, QString *resolved, QString *error)
{
    const QString dir = App::effectiveLogDir(configured);
    if (resolved)
        *resolved = dir;

    const QString shown = App::displayPath(dir);

    const QFileInfo info(dir);
    if (info.exists() && !info.isDir()) {
        if (error)
            *error = QObject::tr("%1\n\nA file with that name already exists, "
                                 "so it cannot be used as a folder.").arg(shown);
        return false;
    }

    if (!info.exists() && !QDir().mkpath(dir)) {
        if (error)
            *error = QObject::tr("%1\n\nThe folder does not exist and could not be created. "
                                 "Check that the drive exists and that you have permission "
                                 "to write there.").arg(shown);
        return false;
    }

    // Existing and creatable is not the same as writable -- prove it.
    QTemporaryFile probe(QDir(dir).filePath(QStringLiteral(".uartx-write-test-XXXXXX")));
    if (!probe.open()) {
        if (error)
            *error = QObject::tr("%1\n\nThe folder cannot be written to. "
                                 "Choose a different location, or check the folder's "
                                 "permissions.").arg(shown);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Base
// ---------------------------------------------------------------------------

SettingsDialogBase::SettingsDialogBase(MainWindow *app, const QString &title)
    : QDialog(app)
    , m_app(app)
{
    setWindowTitle(title);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setModal(false);
}

// ---------------------------------------------------------------------------
// Settings > Serial port...
// ---------------------------------------------------------------------------

SerialPortDialog::SerialPortDialog(MainWindow *app)
    : SettingsDialogBase(app, QStringLiteral("Serial port"))
{
    AppSettings &s = app->settings();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    root->addLayout(form);

    m_port = new QComboBox(this);
    m_port->setEditable(true);
    m_port->addItems(app->knownPorts());
    m_port->setCurrentText(s.port);
    m_port->setMinimumWidth(130);

    m_baud = new QComboBox(this);
    m_baud->setEditable(true);
    m_baud->addItems(App::BAUD_CHOICES);
    m_baud->setCurrentText(s.baud);

    m_databits = new QComboBox(this);
    m_databits->addItems({ QStringLiteral("5"), QStringLiteral("6"),
                           QStringLiteral("7"), QStringLiteral("8") });
    m_databits->setCurrentText(s.databits);

    m_parity = new QComboBox(this);
    m_parity->addItems({ QStringLiteral("None"), QStringLiteral("Even"), QStringLiteral("Odd"),
                         QStringLiteral("Mark"), QStringLiteral("Space") });
    m_parity->setCurrentText(s.parity);

    m_stopbits = new QComboBox(this);
    m_stopbits->addItems({ QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2") });
    m_stopbits->setCurrentText(s.stopbits);

    m_flow = new QComboBox(this);
    m_flow->addItems({ QStringLiteral("None"), QStringLiteral("XON/XOFF"), QStringLiteral("RTS/CTS") });
    m_flow->setCurrentText(s.flow);

    form->addRow(tr("Port:"),         m_port);
    form->addRow(tr("Baud rate:"),    m_baud);
    form->addRow(tr("Data bits:"),    m_databits);
    form->addRow(tr("Parity:"),       m_parity);
    form->addRow(tr("Stop bits:"),    m_stopbits);
    form->addRow(tr("Flow control:"), m_flow);

    auto bind = [app](QComboBox *combo, QString AppSettings::*field) {
        QObject::connect(combo, &QComboBox::currentTextChanged, app, [app, field](const QString &v) {
            app->settings().*field = v;
            app->syncFromSettings();
            app->scheduleConfigSave();
        });
    };
    bind(m_port,     &AppSettings::port);
    bind(m_baud,     &AppSettings::baud);
    bind(m_databits, &AppSettings::databits);
    bind(m_parity,   &AppSettings::parity);
    bind(m_stopbits, &AppSettings::stopbits);
    bind(m_flow,     &AppSettings::flow);

    root->addSpacing(6);
    root->addWidget(hintLabel(tr("Applied the next time the port is opened."), this),
                    0, Qt::AlignHCenter);

    auto *close = new QPushButton(tr("Close"), this);
    close->setFixedWidth(90);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    root->addWidget(close, 0, Qt::AlignHCenter);

    setFixedSize(sizeHint());
}

// ---------------------------------------------------------------------------
// Settings > Log saving...
// ---------------------------------------------------------------------------

LogSavingDialog::LogSavingDialog(MainWindow *app)
    : SettingsDialogBase(app, QStringLiteral("Log saving"))
{
    AppSettings &s = app->settings();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);

    m_enable = new QCheckBox(tr("Save incoming UART data to a log file"), this);
    m_enable->setChecked(s.logEnabled);
    root->addWidget(m_enable);
    root->addWidget(hintLabel(tr("Off by default. Nothing is written to disk until you turn this on."), this));
    root->addSpacing(10);

    m_detail = new QWidget(this);
    auto *grid = new QGridLayout(m_detail);
    grid->setContentsMargins(0, 0, 0, 0);
    root->addWidget(m_detail);

    grid->addWidget(new QLabel(tr("Save to folder:"), m_detail), 0, 0, Qt::AlignRight);
    m_folder = new QLineEdit(s.logDir, m_detail);
    m_folder->setMinimumWidth(340);
    // Leaving the field empty means "use the default folder"; the placeholder
    // shows that default in a generic form rather than a real account path.
    m_folder->setPlaceholderText(App::displayPath(App::defaultLogDir()));
    grid->addWidget(m_folder, 0, 1);

    m_browse = new QPushButton(tr("Browse..."), m_detail);
    grid->addWidget(m_browse, 0, 2);

    m_status = new QLabel(m_detail);
    m_status->setWordWrap(true);
    grid->addWidget(m_status, 1, 1, 1, 2);

    grid->addWidget(new QLabel(tr("File name:"), m_detail), 2, 0, Qt::AlignRight);
    m_template = new QLineEdit(s.logTemplate, m_detail);
    grid->addWidget(m_template, 2, 1);

    // A QLabel without a buddy does no mnemonic processing, so single
    // ampersands show up literally -- which is what we want here.
    grid->addWidget(hintLabel(tr("Placeholders: &Y year  &M month  &D day  &T time  &H port"), m_detail),
                    3, 1);

    m_append = new QCheckBox(tr("Append if the file already exists (otherwise overwrite)"), m_detail);
    m_append->setChecked(s.logAppend);
    grid->addWidget(m_append, 4, 1);

    m_checkTimer = new QTimer(this);
    m_checkTimer->setSingleShot(true);
    connect(m_checkTimer, &QTimer::timeout, this, &LogSavingDialog::revalidate);

    connect(m_enable, &QCheckBox::toggled, this, [this, app](bool v) {
        app->settings().logEnabled = v;
        setControlsEnabled(v);
        app->scheduleConfigSave();
        revalidate();
    });
    connect(m_folder, &QLineEdit::textChanged, this, [this, app](const QString &v) {
        app->settings().logDir = v;
        app->scheduleConfigSave();
        m_checkTimer->start(400);          // debounce while typing
    });
    connect(m_template, &QLineEdit::textChanged, app, [app](const QString &v) {
        app->settings().logTemplate = v;
        app->scheduleConfigSave();
    });
    connect(m_append, &QCheckBox::toggled, app, [app](bool v) {
        app->settings().logAppend = v;
        app->scheduleConfigSave();
    });
    connect(m_browse, &QPushButton::clicked, this, [this, app] {
        const QString start = App::effectiveLogDir(app->settings().logDir);
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose log folder"), start);
        if (!dir.isEmpty())
            m_folder->setText(QDir::toNativeSeparators(dir));
    });

    root->addSpacing(8);
    auto *note = hintLabel(tr("A new log file is started each time the port is opened. "
                              "Log files are plain text; colors are never written to them."), this);
    note->setWordWrap(true);
    root->addWidget(note);

    auto *close = new QPushButton(tr("Close"), this);
    close->setFixedWidth(90);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    root->addSpacing(8);
    root->addWidget(close, 0, Qt::AlignHCenter);

    setControlsEnabled(s.logEnabled);
    revalidate();
    setMinimumWidth(560);
}

void LogSavingDialog::setControlsEnabled(bool on)
{
    m_detail->setEnabled(on);
}

void LogSavingDialog::revalidate()
{
    if (!m_enable->isChecked()) {
        m_status->clear();
        return;
    }

    QString resolved, error;
    if (validateLogDirectory(m_folder->text(), &resolved, &error)) {
        m_status->setStyleSheet(QStringLiteral("color: #4ecb71;"));
        m_status->setText(tr("Ready — logs will be written to %1")
                              .arg(App::displayPath(resolved)));
    } else {
        m_status->setStyleSheet(QStringLiteral("color: #ff6b6b;"));
        m_status->setText(tr("This folder cannot be used. %1")
                              .arg(error.section(QStringLiteral("\n\n"), 1)));
    }
}

// ---------------------------------------------------------------------------
// Settings > Terminal...
// ---------------------------------------------------------------------------

TerminalOptionsDialog::TerminalOptionsDialog(MainWindow *app)
    : SettingsDialogBase(app, QStringLiteral("Terminal"))
{
    AppSettings &s = app->settings();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);

    m_echo = new QCheckBox(tr("Local echo (show the characters you type)"), this);
    m_echo->setChecked(s.localEcho);
    connect(m_echo, &QCheckBox::toggled, app, [app](bool v) {
        app->settings().localEcho = v;
        app->scheduleConfigSave();
    });
    root->addWidget(m_echo);

    m_reconnect = new QCheckBox(tr("Reconnect automatically if the connection drops"), this);
    m_reconnect->setChecked(s.autoReconnect);
    connect(m_reconnect, &QCheckBox::toggled, app, [app](bool v) {
        app->settings().autoReconnect = v;
        app->scheduleConfigSave();
    });
    root->addWidget(m_reconnect);

    auto *eolRow = new QHBoxLayout;
    eolRow->addWidget(new QLabel(tr("Line Ending:"), this));
    m_eol = new QComboBox(this);
    m_eol->addItems(App::EOL_CHOICES);
    m_eol->setCurrentText(App::normaliseLineEnding(s.eol));
    m_eol->setToolTip(tr("Appended to everything you send. None sends the text as typed."));
    connect(m_eol, &QComboBox::currentTextChanged, app, [app](const QString &v) {
        app->settings().eol = v;
        app->scheduleConfigSave();
    });
    eolRow->addWidget(m_eol);
    eolRow->addStretch(1);
    root->addLayout(eolRow);

    root->addSpacing(6);
    root->addWidget(hSeparator(this));
    root->addSpacing(6);

    m_colorize = new QCheckBox(tr("Apply color rules to incoming lines"), this);
    m_colorize->setChecked(s.colorize);
    connect(m_colorize, &QCheckBox::toggled, app, [app](bool v) {
        app->settings().colorize = v;
        app->scheduleConfigSave();
    });
    root->addWidget(m_colorize);
    root->addWidget(hintLabel(tr("Turn this off to see every line in the plain terminal color.\n"
                                 "Filter windows keep working either way."), this));

    // --- optional debugging aids -------------------------------------------
    root->addSpacing(10);
    root->addWidget(hSeparator(this));
    root->addSpacing(6);

    auto *aidsTitle = new QLabel(tr("Debugging aids"), this);
    QFont aidsFont = aidsTitle->font();
    aidsFont.setBold(true);
    aidsTitle->setFont(aidsFont);
    root->addWidget(aidsTitle);
    root->addWidget(hintLabel(tr("All off by default. These change the display only \u2014 the\n"
                                 "session log always records the raw UART data."), this));
    root->addSpacing(4);

    m_timestamps = new QCheckBox(tr("Show timestamps (millisecond precision)"), this);
    m_timestamps->setChecked(s.showTimestamps);
    connect(m_timestamps, &QCheckBox::toggled, app, [app](bool v) {
        app->settings().showTimestamps = v;
        app->scheduleConfigSave();
    });
    root->addWidget(m_timestamps);

    m_direction = new QCheckBox(tr("Show RX / TX indicators"), this);
    m_direction->setChecked(s.showDirection);
    connect(m_direction, &QCheckBox::toggled, app, [app](bool v) {
        app->settings().showDirection = v;
        app->scheduleConfigSave();
    });
    root->addWidget(m_direction);

    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(tr("Display data as:"), this));
    m_displayMode = new QComboBox(this);
    m_displayMode->addItems(LineFormat::modes());
    m_displayMode->setCurrentText(s.displayMode);
    connect(m_displayMode, &QComboBox::currentTextChanged, app, [app](const QString &v) {
        app->settings().displayMode = v;
        app->scheduleConfigSave();
    });
    modeRow->addWidget(m_displayMode);
    modeRow->addStretch(1);
    root->addLayout(modeRow);
    root->addWidget(hintLabel(tr("Applies to lines received from here on."), this));

    root->addSpacing(10);
    auto *close = new QPushButton(tr("Close"), this);
    close->setFixedWidth(90);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    root->addWidget(close, 0, Qt::AlignHCenter);

    setFixedSize(sizeHint());
}

// ---------------------------------------------------------------------------
// Settings > Configurations...
// ---------------------------------------------------------------------------

ConfigurationsDialog::ConfigurationsDialog(MainWindow *app)
    : SettingsDialogBase(app, QStringLiteral("Configurations"))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);

    auto *title = new QLabel(tr("A configuration stores the serial settings, terminal options, "
                                "log-saving setup, color rules and filters under one name."), this);
    title->setWordWrap(true);
    root->addWidget(title);
    root->addSpacing(10);

    auto *selectRow = new QHBoxLayout;
    selectRow->addWidget(new QLabel(tr("Configuration:"), this));
    m_configs = new QComboBox(this);
    m_configs->setMinimumWidth(220);
    selectRow->addWidget(m_configs, 1);
    auto *loadBtn = new QPushButton(tr("Load"), this);
    auto *delBtn  = new QPushButton(tr("Delete"), this);
    loadBtn->setFixedWidth(90);
    delBtn->setFixedWidth(90);
    selectRow->addWidget(loadBtn);
    selectRow->addWidget(delBtn);
    root->addLayout(selectRow);

    m_active = hintLabel(QString(), this);
    root->addWidget(m_active);

    connect(loadBtn, &QPushButton::clicked, this, [this, app] {
        app->loadConfigNamed(m_configs->currentText().trimmed());
    });
    connect(delBtn, &QPushButton::clicked, this, [this, app] {
        app->deleteConfigNamed(m_configs->currentText().trimmed());
    });

    root->addSpacing(10);
    root->addWidget(hSeparator(this));
    root->addSpacing(10);

    auto *saveRow = new QHBoxLayout;
    saveRow->addWidget(new QLabel(tr("Save as:"), this));
    m_name = new QLineEdit(this);
    m_name->setMinimumWidth(170);
    m_name->setPlaceholderText(tr("configuration name"));
    saveRow->addWidget(m_name, 1);
    auto *saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setFixedWidth(90);
    saveRow->addWidget(saveBtn);
    root->addLayout(saveRow);

    root->addWidget(hintLabel(tr("Keep the name to update the loaded configuration, "
                                 "or type a new one to create a copy."), this));

    connect(saveBtn, &QPushButton::clicked, this, [this, app] {
        app->saveConfigNamed(m_name->text().trimmed());
    });
    connect(m_name, &QLineEdit::returnPressed, this, [this, app] {
        app->saveConfigNamed(m_name->text().trimmed());
    });

    root->addSpacing(10);
    root->addWidget(hSeparator(this));
    root->addSpacing(10);

    auto *info = hintLabel(tr("Everything is stored in:\n%1")
                               .arg(App::displayPath(ConfigStore::filePath())), this);
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(info);

    root->addStretch(1);
    auto *close = new QPushButton(tr("Close"), this);
    close->setFixedWidth(90);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    root->addWidget(close, 0, Qt::AlignHCenter);

    connect(app, &MainWindow::configListChanged, this, &ConfigurationsDialog::refreshList);
    refreshList();
    resize(520, 400);
}

void ConfigurationsDialog::refreshList()
{
    const QStringList names = ConfigStore::configNames();
    const QString active = m_app->activeConfigName();

    QSignalBlocker block(m_configs);
    const QString current = m_configs->currentText();
    m_configs->clear();
    m_configs->addItems(names);
    if (!active.isEmpty() && names.contains(active))
        m_configs->setCurrentText(active);
    else if (names.contains(current))
        m_configs->setCurrentText(current);
    else if (!names.isEmpty())
        m_configs->setCurrentIndex(0);

    m_name->setText(active);
    m_active->setText(active.isEmpty()
                          ? tr("No configuration loaded — the current settings are unsaved.")
                          : tr("Currently loaded: %1").arg(active));
}

// ---------------------------------------------------------------------------
// Help > About UartX
// ---------------------------------------------------------------------------

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About ") + App::NAME);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(680, 660);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 12);

    auto *header = new QHBoxLayout;
    header->setSpacing(14);

    // The wordmark is the product name, so no separate title label beside it.
    // It is tinted to whatever contrasts with the dialog, so the same
    // transparent asset works on a dark or a light surface.
    const QPixmap logo = Theme::wordmark(
        Theme::contrastingInk(palette().color(QPalette::Window)), 190);
    if (!logo.isNull()) {
        auto *logoLabel = new QLabel(this);
        logoLabel->setPixmap(logo);
        header->addWidget(logoLabel, 0, Qt::AlignVCenter);
    }

    auto *titleCol = new QVBoxLayout;
    titleCol->addStretch(1);
    auto *sub = new QLabel(App::TAGLINE, this);
    QFont subFont = sub->font();
    subFont.setPointSize(11);
    sub->setFont(subFont);
    titleCol->addWidget(sub);
    auto *ver = new QLabel(tr("Version %1").arg(App::VERSION), this);
    ver->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    titleCol->addWidget(ver);
    titleCol->addStretch(1);

    header->addLayout(titleCol, 1);
    root->addLayout(header);
    root->addSpacing(14);

    auto *body = new QTextBrowser(this);
    body->setOpenExternalLinks(true);
    body->setFrameShape(QFrame::NoFrame);
    body->setHtml(tr(
        "<style>"
        "  h3 { margin-top: 16px; margin-bottom: 4px; }"
        "  p, li { line-height: 145%; }"
        "  code { color: #7fd3ff; }"
        "  td { padding-right: 14px; }"
        "</style>"

        "<p>%1 is a UART debugging and log analysis tool for embedded and firmware "
        "developers. It turns thousands of lines of firmware output into the "
        "information you actually need.</p>"

        "<p>When a log is too busy to read, the usual fix is to remove prints from the "
        "firmware &mdash; which changes the timing and throws away the context that "
        "would have explained the failure. %1 removes that trade. Leave every print "
        "where it is, define a rule for what you are hunting, and watch only those "
        "lines live in a Filter window, while the complete stream keeps being recorded "
        "to disk exactly as the device sent it. You filter the view, never the data.</p>"

        "<p>%1 makes no assumption about how your device formats its messages. Every "
        "color rule and every filter is defined by you, so the same tool suits any "
        "firmware, board or module.</p>"

        "<h3>1. Configure the serial port</h3>"
        "<p>Set the <b>Port</b> and <b>Baud</b> rate in the Connection group of the "
        "ribbon. Use <b>Refresh</b> after plugging in a device to re-scan for ports. "
        "For data bits, parity, stop bits and flow control, open "
        "<i>Settings &rsaquo; Serial port</i>. Settings take effect the next time the "
        "port is opened.</p>"

        "<h3>2. Connect to the device</h3>"
        "<p>Press <b>Connect</b>, or <b>F2</b>. Incoming lines appear immediately and "
        "the status bar shows the connection state and the number of bytes received. "
        "Press <b>Disconnect</b> or <b>F2</b> again to close the port. If the "
        "connection drops, %1 reconnects automatically unless you switch that off.</p>"

        "<h3>3. Send data to the device</h3>"
        "<p>Use the <b>Send</b> box below the terminal: type a command and press "
        "<b>Enter</b> or click <b>Send</b>. What you send is echoed into the terminal "
        "in its own shade so it is easy to pick out from the device's output.</p>"
        "<ul>"
        "<li><b>Up</b> and <b>Down</b> in the Send box recall previously sent "
        "commands. A recalled command stays editable, so you can adjust it before "
        "sending. The history is remembered between runs.</li>"
        "<li>The <b>Line Ending</b> appended to everything you send is chosen under "
        "<i>Settings &rsaquo; Terminal</i>: <b>None</b>, <b>LF</b>, <b>CR</b> or "
        "<b>CRLF</b>.</li>"
        "<li>Typing directly into the terminal still sends individual characters, for "
        "devices that expect keystrokes rather than whole lines.</li>"
        "</ul>"

        "<h3>4. Color rules — highlight important lines</h3>"
        "<p>Open <i>Tools &rsaquo; Color rules</i> (<b>Ctrl+R</b>). A rule has a "
        "<b>name</b>, a <b>keyword</b> and a <b>color</b>. Any line containing the "
        "keyword is displayed entirely in that color — for example a rule named "
        "<i>Error</i> with the keyword <code>|E|</code> shown in red.</p>"
        "<ul>"
        "<li>Rules are evaluated top to bottom and the first match wins, so place "
        "specific rules above general ones. Use <b>Move up</b> and <b>Move down</b> to "
        "reorder them.</li>"
        "<li>Clear the <b>On</b> checkbox to switch a rule off without deleting it.</li>"
        "<li>Enable <b>Match case</b> when a keyword such as <code>|E|</code> must not "
        "match <code>|e|</code>.</li>"
        "</ul>"
        "<p>Color rules only change how lines look. They never hide anything.</p>"

        "<h3>5. Filters — show only the lines you want</h3>"
        "<p>Open <i>Tools &rsaquo; Filter window</i> (<b>Ctrl+F</b>). A Filter window "
        "shows a subset of the traffic, chosen two ways:</p>"
        "<ul>"
        "<li><b>Color rules</b> &mdash; tick a rule to show every line it highlights. "
        "The categories you already defined for colouring double as filters, and the "
        "lines keep their colour.</li>"
        "<li><b>Text filters</b> &mdash; tick a filter to show every line containing "
        "its text. A filter of <code>/6/</code> shows only the lines containing "
        "<code>/6/</code>. Use <b>Add</b> to create one; it joins your filter library "
        "and is available in every Filter window.</li>"
        "</ul>"
        "<p>Ticking several sources combines them: a line is shown if it matches "
        "<i>any</i> ticked rule or filter. With nothing ticked, every line is shown.</p>"
        "<p>Open several Filter windows at once to watch different subsets side by "
        "side; each keeps its own selection. Filters apply to incoming lines only: a "
        "window starts empty and follows the live stream, which keeps it responsive on "
        "long captures.</p>"
        "<p><b>Search captured lines</b> looks through what a Filter window has already "
        "collected, rather than changing what it collects. Typing in the box jumps to "
        "the first match and pauses follow-live so the results stay put; "
        "<b>Previous</b> and <b>Next</b> step through them, wrapping around at the "
        "ends. Clearing the box resumes following the live stream.</p>"

        "<h3>Optional debugging aids</h3>"
        "<p>These live under <i>Settings &rsaquo; Terminal</i> rather than the ribbon, "
        "and are all off until you turn them on:</p>"
        "<ul>"
        "<li><b>Timestamps</b> — prefix each line with the time it arrived, to the "
        "millisecond.</li>"
        "<li><b>RX / TX indicators</b> — label each line with its direction, so "
        "sent and received data are unambiguous.</li>"
        "<li><b>Display data as</b> — show the traffic as <b>ASCII</b>, as "
        "<b>HEX</b>, or as <b>HEX + ASCII</b> side by side for inspecting binary "
        "protocols.</li>"
        "</ul>"
        "<p>All three affect the display only. The session log always records the raw "
        "UART data, and colour rules and filters always match against the raw text, so "
        "switching to hex never changes which lines match.</p>"

        "<h3>6. Save UART logs</h3>"
        "<p>Log saving is off until you enable it. Open "
        "<i>Settings &rsaquo; Log saving</i>, tick <b>Save incoming UART data to a log "
        "file</b> and choose a destination folder — leave it blank to use the default "
        "location. The dialog confirms whether the folder can be written to, and %1 "
        "reports a clear error if the location becomes unusable when you connect.</p>"
        "<p>A new file is created each time the port is opened. The file name may "
        "include <code>&amp;Y</code> year, <code>&amp;M</code> month, <code>&amp;D</code> "
        "day, <code>&amp;T</code> time and <code>&amp;H</code> port. Use "
        "<i>Session &rsaquo; Open log folder</i> to reach them.</p>"
        "<p>The log holds the data received from the device and nothing else: no "
        "timestamps, direction markers, hex formatting or colours, and no data you "
        "sent. That keeps a capture faithful to what came down the wire and readable "
        "by other tools.</p>"

        "<h3>7. Save filtered output</h3>"
        "<p>Each Filter window has its own <b>Save output</b> button, which writes "
        "everything currently shown in that window to a file of your choosing. This is "
        "independent of the session log, so you can capture just the lines matching a "
        "filter without recording the full stream.</p>"
        "<p>The file opens with a header recording exactly which settings produced it "
        "&mdash; the port and baud rate, the colour rules and text filters that were "
        "ticked, any quick-find text, and the line count &mdash; so a saved extract "
        "stays meaningful on its own.</p>"

        "<h3>8. Manage configurations</h3>"
        "<p>A configuration is a named snapshot of the serial settings, terminal "
        "options, log-saving setup, color rules and filters.</p>"
        "<ul>"
        "<li>Choose a configuration in the ribbon and press <b>Load</b> to apply it. "
        "The name box is filled in with the loaded name.</li>"
        "<li>Change anything you like, then press <b>Save</b> to write the changes back "
        "to that same configuration.</li>"
        "<li>To keep the original and branch off, type a different name before pressing "
        "<b>Save</b>.</li>"
        "<li><b>Delete</b> removes the selected configuration.</li>"
        "</ul>"
        "<p>Your current settings are also saved automatically and restored the next "
        "time %1 starts, whether or not they belong to a named configuration.</p>"

        "<h3>Keyboard shortcuts</h3>"
        "<table>"
        "<tr><td><b>F2</b></td><td>Connect or disconnect</td></tr>"
        "<tr><td><b>Ctrl+R</b></td><td>Color rules</td></tr>"
        "<tr><td><b>Ctrl+F</b></td><td>New filter window</td></tr>"
        "<tr><td><b>Ctrl+L</b></td><td>Clear the screen</td></tr>"
        "<tr><td><b>Ctrl+C</b></td><td>Copy the selected text</td></tr>"
        "</table>"
        "<p>Right-clicking the terminal also copies the selection. The ribbon can be "
        "collapsed with the small arrow at its right-hand edge.</p>"

        "<h3>Support</h3>"
        "<p>For questions, problems or feature requests, contact "
        "<a href=\"mailto:%2\">%2</a>.</p>"
        "<p style=\"color:#8a8f98;\">%1 %3 &middot; &copy; %4 %5 &middot; Built with Qt %6</p>"
        ).arg(App::NAME, App::SUPPORT_EMAIL, App::VERSION, App::YEAR, App::DEVELOPER,
              QStringLiteral(QT_VERSION_STR)));
    root->addWidget(body, 1);

    auto *close = new QPushButton(tr("Close"), this);
    close->setFixedWidth(90);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    root->addSpacing(8);
    root->addWidget(close, 0, Qt::AlignHCenter);
}
