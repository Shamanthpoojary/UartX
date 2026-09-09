#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "configstore.h"
#include "colorrules.h"
#include "textfilters.h"
#include "lineformat.h"
#include "lineassembler.h"
#include "serialworker.h"

#include <QMainWindow>
#include <QByteArray>
#include <QStringList>
#include <QVector>
#include <QDateTime>

class TerminalView;
class FilterWindow;
class RibbonBar;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLineEdit;
class QLabel;
class QWidget;
class QTimer;
class QFile;
class QAction;

/// The application window: menu bar, collapsible ribbon, terminal and status
/// line, plus the serial session, the optional log file, and the live feed
/// into any open Filter windows.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // --- shared state used by the dialogs and the Filter windows ---
    AppSettings &settings()                { return m_state.settings; }
    const AppSettings &settings() const    { return m_state.settings; }
    const ColorRuleSet &colorRules() const { return m_state.colorRules; }
    /// How lines should be rendered, shared with the Filter windows so every
    /// view agrees.
    DisplayOptions displayOptions() const;
    const TextFilterSet &textFilters() const { return m_state.textFilters; }
    QString activeConfigName() const       { return m_activeConfig; }

    /// Pushes the settings back into the ribbon widgets. Call after any
    /// programmatic change (configuration load, dialog edit).
    void syncFromSettings();

    /// Queues a debounced write of config.json.
    void scheduleConfigSave();

    /// Replace the colour rules or the filter library; both refresh every
    /// open Filter window, which offers each of them as a filter source.
    void applyColorRules(const ColorRuleSet &rules);
    void applyTextFilters(const TextFilterSet &filters);

    // --- session log lifecycle ---
    // Arms logging for a session; the file itself is not created until the
    // first byte arrives, so an idle session leaves nothing behind.
    bool openLog(const QString &port);
    void logWrite(const QString &text, bool newline);
    void closeLog();

    void unregisterFilterWindow(FilterWindow *win);
    QStringList knownPorts() const { return m_knownPorts; }

    // --- named configurations ---
    void saveConfigNamed(const QString &name);
    void loadConfigNamed(const QString &name);
    void deleteConfigNamed(const QString &name);

signals:
    /// Emitted whenever the stored configuration list or the active
    /// configuration changes, so open dialogs can refresh themselves.
    void configListChanged();

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onConnectClicked();
    void onRefreshPorts();
    void onClearScreen();
    void onOpenLogFolder();
    void onCopySelection();
    void onKeyTyped(bool isReturn, const QString &text);
    void onSendClicked();

    void onSerialData(const QByteArray &data);
    void onSerialStatus(const QString &status);
    void onSerialFatal(const QString &message);
    void onPoll();

    void openColorRulesDialog();
    void openFilterWindow();

    void dlgSerialPort();
    void dlgLogSaving();
    void dlgTerminalOptions();
    void dlgConfigurations();
    void dlgAbout();

private:
    void buildMenuBar();
    void buildUi();
    void buildRibbon();
    void refreshConfigCombo();
    void refreshPorts(bool announce);
    void saveConfigNow();
    void setActiveConfig(const QString &name);
    void updateRibbonSummary();

    void handleCompleteLine(const QString &line);
    void appendSystemMessage(const QString &message);
    /// Builds the display form of a line and pushes it to the terminal, the
    /// log (raw only) and every Filter window.
    LineRecord makeRecord(const QString &raw, bool isTx) const;
    void publish(const LineRecord &record, bool newline = true);
    void sendPayload(const QString &text);
    void recallHistory(int direction);

    bool createLogFile();
    static QString expandTemplate(const QString &tmpl, const QString &port);

    void updateStatusLine();
    void showTemporaryNotice(const QString &message);

    // ---- persisted state ----
    UserState m_state;
    QString   m_activeConfig;

    // ---- serial session ----
    SerialWorker  *m_worker = nullptr;
    LineAssembler  m_assembler;

    /// One ordered queue for both kinds of worker event, so a status line
    /// never jumps ahead of the data that arrived before it.
    struct Event
    {
        bool       isStatus;
        QByteArray data;
        QString    status;
    };
    QVector<Event> m_pending;            // drained by the 40 ms poll timer

    qint64  m_rxBytes = 0;
    QString m_currentStatus;

    // ---- logging ----
    // The file is not created until the first byte actually arrives, so a
    // session that receives nothing leaves no empty log behind.
    QFile    *m_logFile = nullptr;
    QString   m_logPath;
    bool      m_logArmed = false;      // logging wanted, file not created yet
    QDateTime m_logSessionStart;

    // ---- filter windows ----
    QVector<FilterWindow *> m_filterWindows;

    // ---- widgets ----
    RibbonBar    *m_ribbon         = nullptr;
    QAction      *m_ribbonAction   = nullptr;
    QComboBox    *m_portCombo      = nullptr;
    QComboBox    *m_baudCombo      = nullptr;
    QPushButton  *m_connectBtn     = nullptr;
    QCheckBox    *m_autoscrollBox  = nullptr;
    QComboBox    *m_configCombo    = nullptr;
    QLineEdit    *m_configNameEdit = nullptr;
    TerminalView *m_terminal       = nullptr;
    QLineEdit    *m_txInput        = nullptr;
    QPushButton  *m_sendBtn        = nullptr;
    QLabel       *m_statusLabel    = nullptr;

    // -1 means "not currently walking the history"; the draft holds whatever
    // was typed before the first Up press so it can be restored.
    int     m_historyIndex = -1;
    QString m_historyDraft;

    QTimer *m_pollTimer   = nullptr;
    QTimer *m_noticeTimer = nullptr;
    QTimer *m_saveTimer   = nullptr;

    QStringList m_knownPorts;
    bool m_notificationActive = false;
    bool m_syncing            = false;   // guards ribbon <-> settings echo
};

#endif // MAINWINDOW_H
