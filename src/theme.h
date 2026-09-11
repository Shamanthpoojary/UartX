#ifndef THEME_H
#define THEME_H

#include <QString>
#include <QFont>
#include <QIcon>
#include <QColor>
#include <QPalette>
#include <QPixmap>

/// The application-wide look: a neutral black-and-grey palette, so the only
/// colour on screen comes from the user's own colour rules.
///
/// This is applied to the QApplication rather than to the main window,
/// because the Filter windows are parentless top-level widgets and would
/// otherwise inherit nothing and fall back to the native theme.
class QApplication;

namespace Theme {

/// Applies the whole look to the application: the palette first, then the
/// stylesheet.
///
/// Both are needed. A stylesheet only reaches the widget types it names, so
/// anything it misses keeps the platform's own colours -- which on a Linux
/// desktop means a light dialog behind the light-grey text the stylesheet
/// does apply. The palette closes that gap for every widget at once.
void apply(::QApplication &app);

QString styleSheet();

/// The dark palette behind the stylesheet, exposed for the same reason the
/// stylesheet is: the Filter windows are parentless top-level widgets.
QPalette palette();

/// The terminal typeface: the first monospace family actually installed,
/// chosen at runtime because no single family name exists on every platform.
QFont monospaceFont();

/// The application icon, assembled from the PNG sizes in the resources.
/// PNG rather than .ico so it works without Qt's ICO image plugin, which is
/// not always deployed on Linux.
QIcon appIcon();

/// Black or white, whichever stays readable on `background`.
QColor contrastingInk(const QColor &background);

/// The UartX wordmark, recoloured to `ink` and scaled to `width` pixels.
/// The asset itself is a transparent white mask, so it carries no background
/// of its own and can sit on any surface.
QPixmap wordmark(const QColor &ink, int width);

} // namespace Theme

#endif // THEME_H
