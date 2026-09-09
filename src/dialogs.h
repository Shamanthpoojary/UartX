#ifndef DIALOGS_H
#define DIALOGS_H

#include <QDialog>
#include <QString>

class MainWindow;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;
class QWidget;
class QTimer;

/// Validates a log folder: resolves an empty setting to the default, creates
/// the folder if needed, and proves it is writable. Returns false and fills
/// `error` with a message fit to show the user when it cannot be used.
bool validateLogDirectory(const QString &configured, QString *resolved, QString *error);

/// Common shell for the small settings dialogs: modeless, non-resizable, and
/// editing the live settings so changes take effect as you make them. Serial
/// line and log-file values are consumed the next time the port is opened.
class SettingsDialogBase : public QDialog
{
    Q_OBJECT
public:
    SettingsDialogBase(MainWindow *app, const QString &title);

protected:
    MainWindow *m_app = nullptr;
};

/// Settings > Serial port...
class SerialPortDialog : public SettingsDialogBase
{
    Q_OBJECT
public:
    explicit SerialPortDialog(MainWindow *app);

private:
    QComboBox *m_port;
    QComboBox *m_baud;
    QComboBox *m_databits;
    QComboBox *m_parity;
    QComboBox *m_stopbits;
    QComboBox *m_flow;
};

/// Settings > Log saving...
class LogSavingDialog : public SettingsDialogBase
{
    Q_OBJECT
public:
    explicit LogSavingDialog(MainWindow *app);

private slots:
    void revalidate();

private:
    void setControlsEnabled(bool on);

    QCheckBox   *m_enable;
    QLineEdit   *m_folder;
    QLineEdit   *m_template;
    QCheckBox   *m_append;
    QPushButton *m_browse;
    QWidget     *m_detail;
    QLabel      *m_status;
    QTimer      *m_checkTimer;
};

/// Settings > Terminal...
class TerminalOptionsDialog : public SettingsDialogBase
{
    Q_OBJECT
public:
    explicit TerminalOptionsDialog(MainWindow *app);

private:
    QCheckBox *m_echo;
    QCheckBox *m_reconnect;
    QComboBox *m_eol;
    QCheckBox *m_colorize;
    QCheckBox *m_timestamps;
    QCheckBox *m_direction;
    QComboBox *m_displayMode;
};

/// Settings > Configurations...  (named snapshots of settings, rules, filters)
class ConfigurationsDialog : public SettingsDialogBase
{
    Q_OBJECT
public:
    explicit ConfigurationsDialog(MainWindow *app);

private slots:
    void refreshList();

private:
    QComboBox *m_configs;
    QLineEdit *m_name;
    QLabel    *m_active;
};

/// Help > About UartX -- the user documentation.
class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent);
};

#endif // DIALOGS_H
