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

/// The startup logo, `px` points square, for the splash shown while the main
/// window is built.
///
/// This is the app icon rather than the wordmark: it already carries its own
/// dark rounded plate, so it needs nothing painted behind it and reads the
/// same whatever desktop it lands on. Scaled for the display's device pixel
/// ratio, because a splash is the first thing the user sees and a soft one
/// says the wrong thing about the rest.
QPixmap splashArt(int px);

/// Black or white, whichever stays readable on `background`.
QColor contrastingInk(const QColor &background);

/// The colour for an ANSI index 0..15, as sent by the device.
///
/// This is the one place colour enters the UI without the user asking for it,
/// and it is deliberate: firmware that colour-codes its own output is saying
/// something, and a viewer that flattens it is throwing information away. The
/// shades are muted to sit on the terminal's near-black background rather than
/// the harsh primaries a bare VT palette would give. An index outside the
/// range returns `fallback`, which is what an uncoloured line gets.
QColor ansiColor(int index, const QColor &fallback);

/// The UartX wordmark, recoloured to `ink` and scaled to `width` pixels.
/// The asset itself is a transparent white mask, so it carries no background
/// of its own and can sit on any surface.
QPixmap wordmark(const QColor &ink, int width);

} // namespace Theme

#endif // THEME_H
