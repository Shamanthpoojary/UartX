#include "mainwindow.h"
#include "appconstants.h"
#include "theme.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
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
