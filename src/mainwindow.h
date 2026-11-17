#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "configstore.h"
#include "colorrules.h"
#include "textfilters.h"
#include "lineformat.h"
#include "logretention.h"
#include "pipeline.h"
#include "serialworker.h"

#include <QMainWindow>
#include <QByteArray>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <QThreadPool>

class TerminalView;
class FilterWindow;
class QSplitter;
class RibbonBar;
class QComboBox;
class QCheckBox;
class QPushButton;
class QLineEdit;
class QLabel;
class QWidget;
class QTimer;
class QAction;
class Session;

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

    /// Queues a debounced write of config.json, and passes the live display
    /// settings to the open session. Every settings control already ends in
    /// this call, which makes it the one place a change cannot be missed.
    void scheduleConfigSave();

    /// Replace the colour rules or the filter library; both refresh every
    /// open Filter window, which offers each of them as a filter source.
    void applyColorRules(const ColorRuleSet &rules);
    void applyTextFilters(const TextFilterSet &filters);

    /// Closes the session, if one is open: the reader stops, the open line is
    /// flushed and the log closed before this returns.
    void closeSession();

    void unregisterFilterWindow(FilterWindow *win);

    /// Moves a filter view between the splitter and its own window.
    ///
    /// The splitter holds at most one filter view: several side by side leave
    /// every one of them too narrow to read, which is the opposite of what
    /// docking is for. Docking a second view pops the first one out.
    void setFilterWindowDocked(FilterWindow *win, bool docked);
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

    void onBatch(DisplayBatchPtr batch);
    void onSerialStatus(const QString &status);
    void onSerialFatal(const QString &message);
    void onLogState(const LogState &state);
    void runRetention();
    void onRetentionDone(const RetentionReport &report);

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

    void appendSystemMessage(const QString &message, const QColor &color = QColor());
    void sendPayload(const QString &text);
    void recallHistory(int direction);
    PipelineConfig pipelineConfig(const QString &port, const QString &logDir) const;
    RetentionPolicy retentionPolicy() const;

    static QString expandTemplate(const QString &tmpl, const QString &port);

    void updateStatusLine();
    void showTemporaryNotice(const QString &message);

    // ---- persisted state ----
    UserState m_state;
    QString   m_activeConfig;

    // ---- serial session ----
    // The reader, the pipeline and the log all live in the Session; this
    // window only draws the batches it hands over.
    Session *m_session = nullptr;
    qint64   m_rxBytes = 0;
    quint64  m_totalLines = 0;
    QString  m_currentStatus;
    LogState m_logState;

    // ---- log retention ----
    // Cleanup does file I/O across the whole log folder, so it runs on a
    // pool thread; its result comes back as a queued call.
    QThreadPool m_ioPool;
    QTimer     *m_retentionTimer = nullptr;
    bool        m_retentionRunning = false;

    // ---- filter windows ----
    QSplitter *m_splitter = nullptr;
    QVector<FilterWindow *> m_filterWindows;

    /// The filter view currently sitting in the splitter, or nullptr.
    /// Read from the splitter itself rather than from a flag, so it cannot
    /// drift out of step with where the widgets actually are.
    FilterWindow *dockedFilterWindow() const;

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

    QTimer *m_noticeTimer = nullptr;
    QTimer *m_saveTimer   = nullptr;

    QStringList m_knownPorts;
    bool m_notificationActive = false;
    bool m_syncing            = false;   // guards ribbon <-> settings echo
};

#endif // MAINWINDOW_H
