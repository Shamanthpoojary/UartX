# Changelog

All notable changes to UartX are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.1.2] - 2026-09-28

### Changed

- **New logo, applied everywhere.** The app icon, the About box and the README
  header now come from one pair of masters in `resources/logo/`, and
  `scripts/make_icon.py` derives every size and ink from them — change the
  masters, re-run the script, and nothing else needs editing.
- The **app icon** is white artwork on a dark rounded plate. It is the one
  asset that cannot adapt to its surroundings: it lands on a taskbar or
  launcher that may be light or dark, and a single-ink transparent logo
  disappears against one of them. Everywhere the background *is* known — the
  About box, the README — the logo stays transparent and is re-inked to suit.

### Fixed

- **Firmware that colours its own output was displayed as garbage.** A device
  emitting ANSI escape sequences (`ESC[31m` and friends) had them rendered as
  literal `[31m` noise around every line, because the ASCII view passed the
  line to the widget untouched — UartX is a log viewer, not a terminal
  emulator, so it never interpreted them. Escape sequences and stray control
  bytes are now removed from the ASCII view. The HEX views still show every
  byte, and colour rules still match the raw text, so nothing about filtering
  changes.
- **The session log was not the raw bytes, despite promising to be.** Every
  byte below 0x20 or at/above 0x7F was replaced with `.` before writing, so
  escape sequences, UTF-8 and binary payloads were destroyed in the capture —
  while the display, which is where cleaning belonged, got the raw line. The
  two were exactly inverted. The log is now written byte for byte, using a
  latin-1 round trip so bytes above 0x7F are not re-encoded.
- **The session log no longer starts with a header line.** A banner naming the
  date, port and baud was the one thing in the file the device had not sent,
  which broke the "exactly what came off the wire" guarantee and anything that
  parses or diffs a capture. The file name already carries the date and port,
  and a Filter window's **Save output** remains the place for a header that
  describes how a view was produced.

## [1.1.1] - 2026-09-19

### Fixed

- **The Windows packages would not start on a machine without MinGW
  installed**, failing with a "DLL not found" box naming
  `libgcc_s_seh-1.dll`. The executable imports the MinGW runtime directly, and
  `windeployqt` only copies it when it can find the toolchain on `PATH` —
  which is true of `scripts/build_release.bat` and was not true on CI. So every
  local build worked and every published build was broken. CMake now installs
  the runtime explicitly, resolved from the compiler itself rather than from
  `PATH`, and the release workflow fails if anything the staged binaries import
  is neither shipped nor a Windows system DLL.

- **No AppImage was ever published.** `make-appimage.sh` cd's into the build
  directory partway through, so a build directory passed as a relative path --
  which is how the release workflow called it -- stopped resolving, and
  linuxdeploy inspected a directory that did not exist. It reported "Could not
  find Qt modules to deploy" and the script went on to print an empty "written
  to:" line and exit successfully. Calling it through `build_release.sh`, which
  passes an absolute path, always worked. The path is now resolved before
  anything else, and the script fails if no image was produced.

- **A new Filter window showed every line while all of its sources sat
  unticked.** Nothing ticked was treated as "no filtering" rather than "nothing
  chosen", so the checkboxes and the contents disagreed from the moment the
  window opened. A window now starts with every colour rule already ticked, and
  unticking everything shows nothing — what is ticked is what you see. A user
  with no colour rules and no text filters at all still gets the full stream,
  since there is nothing to choose from.

### Added

- Screenshots in the README, and the wordmark at the top of it in both inks so
  it follows the reader's light or dark theme.
- A "Which file do I need?" table in the README, mapping each release asset
  to the platform it is for, since the Releases page keeps its Assets list
  collapsed and offers the source archives alongside the real downloads.

## [1.1.0] - 2026-09-11

Linux support, and the three bugs found while testing it.

### Added

- **Linux support.** The POSIX serial backend (`termios` / `select`, with
  `/sys/class/tty` port enumeration) sits behind the same Qt-free interface as
  the Win32 one, so everything above it is shared unmodified.
- Linux packages: `.deb`, `.rpm`, `.tar.gz` and a self-contained AppImage that
  bundles Qt and needs no root. The `.deb` and `.rpm` install a desktop entry
  and the icon theme files, so UartX appears in the applications menu.
- `scripts/build_release.sh` builds and packages in one step, and warns when
  the Qt it found is not the distribution's — which would produce a `.deb`
  that installs nowhere else.
- Both CI and the release workflow now build Linux alongside Windows.

### Fixed

- **Session logs were never written on Linux.** The `&H` placeholder in the log
  file name was substituted with the raw port, and a POSIX port is a path
  (`/dev/ttyUSB0`), which turned the file name into a path through directories
  that do not exist. The port is now reduced to a filename-safe token, and the
  parent directory is created as a backstop. Windows was unaffected, because
  `COM5` contains no separators.
- **Crash when disconnecting.** Every teardown ignored what `QThread::wait()`
  returned and destroyed the thread anyway; a timeout meant destroying a thread
  still inside `run()`, which aborts the process. Teardown now goes through one
  place that only deletes the worker once it has actually finished, and
  otherwise lets it delete itself on `finished`.
- **A write to an unresponsive device could hang the reader thread.** The POSIX
  backend wrote to a blocking descriptor, so a device asserting flow control —
  or unplugged mid-write — parked the thread indefinitely and made the teardown
  above time out. Writes now wait for writability with a deadline.
- **Dialogs were unreadable: light-grey text on a light background.** The
  stylesheet gave `QMainWindow` a dark background but never named `QDialog`, so
  every dialog kept the platform's own light window colour while the `QLabel`
  rule painted light-grey text onto it — measured at a 1.24:1 contrast ratio.
  Dialogs and the widgets they are built from are now styled explicitly, and a
  dark `QPalette` is applied so anything the stylesheet does not name still
  gets the right colours. The same dialog now measures 12.93:1. This affected
  both platforms; it was simply more obvious against a Linux desktop theme.
- **UartX would not start on a Wayland session** without `qt6-wayland`
  installed, which is the default state on WSLg and on a fresh Wayland desktop:
  Qt aborted with "Could not find the Qt platform plugin wayland" instead of
  falling back. It now asks Qt for `wayland;xcb` so a missing plugin degrades
  to X11, and the `.deb` recommends `qt6-wayland`.

## [1.0.0] - 2026-09-09

First public release. Windows only.

A UART debugging and log analysis tool for embedded and firmware developers:
you filter the view, never the data, so a busy log becomes readable without
removing prints from the firmware.

### Added

- **Colour rules** — each a name, a keyword and a colour. Any line containing
  the keyword is shown entirely in that colour. Rules are evaluated in order,
  first match wins, and can be reordered, disabled or made case-sensitive.
  Nothing about the device's log format is assumed; the four starter rules are
  defaults a user can delete.
- **Filter windows** showing only selected lines, chosen by colour rule or by
  text filter, with search over captured lines and savable output carrying a
  header of the settings that produced it. Several windows can watch different
  subsets side by side, each following the live stream independently, while the
  full stream keeps being recorded.
- **Serial terminal** with live port enumeration and configurable baud rate,
  data bits, parity, stop bits and flow control. Auto-reconnects when the
  device reboots or the USB-serial adapter blips.
- **Collapsible ribbon** for the controls needed during normal debugging, with
  optional aids kept out of the way under *Settings*.
- **Send box with persistent history**, a selectable line ending, and optional
  local echo.
- **Optional display aids** — millisecond timestamps, RX/TX indicators and
  ASCII / HEX / HEX + ASCII modes. All off by default and display-only: the
  session log and the rule matching always see the raw text.
- **Optional session logging** that records the bytes the device sent and
  nothing else — no timestamps, direction markers, hex formatting or colours.
- **Persistent configuration** in a single `config.json`, plus named
  configuration snapshots.
- **CR/LF normalisation** — LF, CRLF, bare CR and LFCR all render as exactly
  one line break, including when a terminator is split across read chunks.
- **Windows installer and portable zip**, both carrying their own Qt runtime so
  the target machine needs no Qt installation. The installer runs without
  administrator rights and leaves `config.json` alone on uninstall.

[Unreleased]: https://github.com/Shamanthpoojary/UartX/compare/v1.1.2...HEAD
[1.1.2]: https://github.com/Shamanthpoojary/UartX/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/Shamanthpoojary/UartX/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/Shamanthpoojary/UartX/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/Shamanthpoojary/UartX/releases/tag/v1.0.0
