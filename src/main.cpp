#include "mainwindow.h"
#include "appconstants.h"
#include "theme.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
#ifdef Q_OS_UNIX
    // A Wayland session advertises itself through WAYLAND_DISPLAY, and Qt then
    // insists on the wayland platform plugin -- aborting with "Could not find
    // the Qt platform plugin" if it is not installed, rather than falling back.
    // That is a common state: the plugin ships in a separate package
    // (qt6-wayland) that the Qt base install does not pull in, and WSLg sets
    // WAYLAND_DISPLAY on every session. Handing Qt an ordered list lets a
    // missing plugin degrade to X11 instead of refusing to start. An explicit
    // QT_QPA_PLATFORM from the user is left untouched.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "wayland;xcb");
    }
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(App::NAME);
    QApplication::setApplicationVersion(App::VERSION);
    QApplication::setOrganizationName(App::DEVELOPER);
    QApplication::setWindowIcon(Theme::appIcon());

    // Application-wide, not per-window: the Filter windows are parentless
    // top-level widgets and would inherit nothing from the main window.
    qApp->setStyleSheet(Theme::styleSheet());

    MainWindow window;
    window.show();
    return QApplication::exec();
}
