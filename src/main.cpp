#include "mainwindow.h"
#include "appconstants.h"
#include "theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QPixmap>
#include <QSplashScreen>
#include <QTimer>

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
    // top-level widgets, and every dialog is short-lived, so neither would
    // inherit anything from the main window.
    Theme::apply(app);

    // The logo while the window is built. Building it is not slow enough to
    // need a progress indicator, which is why this is a fixed short interval
    // rather than something tied to the work: it exists so the app announces
    // itself instead of a blank maximised window appearing out of nowhere.
    QElapsedTimer startup;
    startup.start();
    QSplashScreen *splash = nullptr;
    if (const QPixmap art = Theme::splashArt(App::SPLASH_PX); !art.isNull()) {
        splash = new QSplashScreen(art);
        splash->setAttribute(Qt::WA_DeleteOnClose);
        splash->show();
        // Without this the splash is only painted once the event loop starts,
        // which is after the window is built -- so it would never be seen.
        QApplication::processEvents();
    }

    MainWindow window;

    // Whatever building the window already consumed counts towards the
    // interval, so a slow machine waits less rather than more. A fast one
    // still gets the full SPLASH_MS; zero remaining simply fires on the next
    // pass of the event loop.
    const int remaining = splash
                              ? qMax(0, App::SPLASH_MS - int(startup.elapsed()))
                              : 0;
    QTimer::singleShot(remaining, &window, [&window, splash] {
        // finish() holds the splash up until the window is actually on screen,
        // so there is no flash of empty desktop between the two.
        if (splash)
            splash->finish(&window);
        // Maximised rather than true full screen: a debugging session wants
        // every pixel of the log, but full screen would take the title bar and
        // the taskbar with it, which is wrong for a window people keep beside
        // an IDE.
        window.showMaximized();
    });

    return QApplication::exec();
}
