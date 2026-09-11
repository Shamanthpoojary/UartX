# Building UartX

## Requirements

- Qt 6.2 or newer
- A C++17 compiler and CMake 3.19 or newer
- [Inno Setup 6](https://jrsoftware.org/isinfo.php), only to build the
  installer

No optional Qt modules are needed: only `Qt::Core`, `Qt::Gui` and
`Qt::Widgets` are linked.

## Platform support

UartX runs on **Windows** and **Linux** from a single codebase. The serial port
is reached through a small platform layer (`src/serialport.h`) that is
deliberately free of Qt, so the operating-system differences are confined to
two files:

| File | Platform | Uses |
| --- | --- | --- |
| `src/serialport_win.cpp` | Windows | `CreateFile` / `DCB` / `COMMTIMEOUTS`, registry enumeration |
| `src/serialport_posix.cpp` | Linux, other Unix | `open` / `termios` / `select`, `/sys/class/tty` enumeration |

CMake picks the right one automatically. Everything above that layer — the
reader thread, the UI, the configuration — is shared by both, unmodified.
Neither backend uses Qt, which keeps the platform code testable on its own.

> **Keep the two build directories separate.** A CMake build tree records the
> source path and toolchain that created it, so pointing WSL at a tree a
> Windows build made fails with *"does not match the source … used to generate
> cache"*. The two scripts already use `build/windows` and `build/linux`.

## Qt Creator

Open `CMakeLists.txt`, pick a Desktop Qt 6 kit, then Build & Run.

## Linux

Install the toolchain and Qt 6 development packages:

```bash
# Debian / Ubuntu / Raspberry Pi OS / Linux Mint
sudo apt install build-essential cmake ninja-build qt6-base-dev libgl1-mesa-dev

# Fedora / RHEL
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel mesa-libGL-devel

# Arch / Manjaro
sudo pacman -S base-devel cmake ninja qt6-base
```

Then build and run:

```bash
scripts/build_release.sh
./build/linux/UartX
```

The script also produces packages:

```bash
scripts/build_release.sh -p deb        # build/linux/uartx_<ver>_amd64.deb
scripts/build_release.sh -p rpm        # an .rpm, where rpmbuild is available
scripts/build_release.sh -p tgz        # a plain tarball
scripts/build_release.sh -p appimage   # a single self-contained AppImage
```

It can be run from any directory, and it prints which Qt it picked.

> **Building a `.deb` needs Qt from the distribution.** CPack runs
> `dpkg-shlibdeps` to work out the dependencies, and that only resolves
> libraries the package manager knows about. If you built against a Qt
> installed somewhere else (the official Qt installer, `aqtinstall`, a
> self-built Qt) it fails with `cannot find library libQt6Core.so.6`. Either
> build against `qt6-base-dev`, or use `-p appimage` / `-p tgz`, which bundle
> or ignore the Qt location instead. The script warns when the Qt it found is
> not the distribution's.

> **The AppImage needs `patchelf`.** `linuxdeploy` uses it to rewrite library
> paths: `sudo apt install patchelf`. The first run also downloads
> `linuxdeploy` and its Qt plugin into `build/tools`.

Or drive CMake yourself:

```bash
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel
sudo cmake --install build/linux        # installs to /usr/local
```

A system install adds the binary, a desktop entry and the icon theme files, so
UartX appears in the application menu.

### The Wayland platform plugin

Qt's wayland platform plugin ships in a separate package (`qt6-wayland`) that
the Qt base install does not pull in. When `WAYLAND_DISPLAY` is set and the
plugin is missing, Qt aborts with *"Could not find the Qt platform plugin
wayland"* rather than falling back — which is the default state on WSLg and on
a fresh Wayland desktop.

`src/main.cpp` handles this by asking Qt for `wayland;xcb`, so a missing plugin
degrades to X11 instead of refusing to start. The `.deb` also lists
`qt6-wayland` under `Recommends`, since the native plugin is the better
experience where it is available. Set `QT_QPA_PLATFORM` yourself to override
the choice.

## Windows

### One-shot release build

```
scripts\build_release.bat
```

Produces the executable, the installer and the portable zip in
`build\windows\`. Edit `QT_DIR` at the top of the script if your Qt lives
somewhere else. The script can be run from any directory.

### Command line

```
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64
cmake --build build/windows
```

## Packaging targets

### Windows

Three targets produce distributable artifacts. All of them run `windeployqt`,
so the result carries its own Qt runtime and needs no Qt installation on the
target machine.

| Command | Result |
| --- | --- |
| `cmake --build build/windows --target installer` | `build/windows/installer/UartX-<ver>-setup.exe` |
| `cmake --build build/windows --target portable` | `build/windows/installer/UartX-<ver>-portable-win64.zip` |
| `cmake --install build/windows --prefix <dir>` | a self-contained folder at `<dir>` |

### Linux

| Command | Result |
| --- | --- |
| `cd build/linux && cpack -G DEB` | `uartx_<ver>_amd64.deb` |
| `cd build/linux && cpack -G RPM` | `uartx-<ver>.x86_64.rpm` |
| `cd build/linux && cpack -G TGZ` | `uartx-<ver>-Linux.tar.gz` |
| `packaging/linux/make-appimage.sh build/linux` | `UartX-<ver>-x86_64.AppImage` |

The `.deb` and `.rpm` depend on the distribution's Qt; the AppImage bundles it.

### The installer

Built with Inno Setup 6:

```
winget install -e --id JRSoftware.InnoSetup
```

Re-run CMake afterwards so `ISCC_EXECUTABLE` is picked up.

The generated setup program:

- installs to `%ProgramFiles%\UartX`, or to the user's own
  `%LocalAppData%\Programs` when run without admin rights;
- shows the MIT licence as an acceptance page;
- creates a Start Menu entry and an optional desktop shortcut;
- registers in **Apps & features** with publisher, version and icon;
- ships an uninstaller that removes everything it installed;
- **leaves `config.json` alone on uninstall**, so filters and settings survive
  an upgrade or a reinstall.

Because `AppId` is a fixed GUID, installing a newer build upgrades the
existing installation in place rather than leaving two entries behind.

Silent install, for scripted rollout:

```
UartX-1.1.0-setup.exe /VERYSILENT /SUPPRESSMSGBOXES
```

## Regenerating the icons

`scripts/make_icon.py` rebuilds `resources/uartx.ico` and everything in
`resources/icons/`. It needs Pillow:

```
pip install pillow
python scripts\make_icon.py
```

The app icon and the in-app wordmark are white on a transparent background.
The wordmark is tinted at run time to whichever of black or white stays
readable against the surface it is drawn on, so it must have no background of
its own.

## Cutting a release

1. Bump the version in the `project()` call in `CMakeLists.txt`. It flows into
   the executable metadata, the About box and the installer file name at once.
2. Move the `[Unreleased]` notes in `CHANGELOG.md` under the new version and
   date them.
3. Commit, then push a tag:

   ```
   git tag v1.1.1
   git push origin v1.1.1
   ```

The **Release** workflow then builds both platforms in parallel and attaches
everything to a GitHub Release:

| Job | Artifacts |
| --- | --- |
| Windows | `UartX-<ver>-setup.exe`, `UartX-<ver>-portable-win64.zip`, `SHA256SUMS-windows.txt` |
| Linux (Ubuntu 22.04) | `uartx_<ver>_amd64.deb`, `uartx-<ver>-Linux.tar.gz`, `UartX-<ver>-x86_64.AppImage`, `SHA256SUMS-linux.txt` |

Each job is set to publish nothing rather than an empty release if its build
produced no artifacts. The AppImage step is marked `continue-on-error` on
purpose: it downloads tools at build time, so it is the step most likely to
break on something outside this repository, and it must never take a good
`.deb` down with it.

The Linux job builds against Ubuntu 22.04's Qt so the `.deb` installs on the
current LTS. Distributions that renamed the Qt packages (Ubuntu 24.04 and
newer) are served by the AppImage.

Release binaries are **not** committed to this repository — they live only as
Release assets, so cloning stays cheap.

> **Bumping the version matters for upgrades.** Shipping a changed build under
> a version number that is already installed means package managers and
> installers treat it as the same release and may skip it.

## Code signing

The installer is **unsigned**, so Windows SmartScreen shows a
"Windows protected your PC" prompt on first run on a machine that has not seen
it before. Users get past it with **More info → Run anyway**. To remove the
warning:

- **Internal / team use** — create a self-signed certificate and push it to
  the team's Trusted Publishers store via Group Policy.
- **External distribution** — buy an OV or EV code-signing certificate
  (Sectigo, DigiCert, SSL.com). EV certificates clear SmartScreen immediately;
  OV certificates clear it once the binary builds reputation.

Sign both the executable and the setup program, in that order:

```
signtool sign /f cert.pfx /p <password> /tr http://timestamp.digicert.com /td sha256 /fd sha256 UartX.exe
signtool sign /f cert.pfx /p <password> /tr http://timestamp.digicert.com /td sha256 /fd sha256 UartX-1.1.0-setup.exe
```

`signtool` ships with the Windows SDK.

## Source layout

| Path | Purpose |
| --- | --- |
| `src/main.cpp` | entry point |
| `src/mainwindow.{h,cpp}` | menus, ribbon host, terminal, status line, session and log handling |
| `src/terminalview.{h,cpp}` | dark terminal widget: colour-per-line, capped scrollback, copy, key forwarding |
| `src/colorrules.{h,cpp}` | the colour-rule model: matching, ordering, JSON, defaults |
| `src/colorrulesdialog.{h,cpp}` | the Color Rules window and the single-rule editor |
| `src/textfilters.{h,cpp}` | the text-filter model used by the Filter windows |
| `src/filterwindow.{h,cpp}` | Filter windows: live filtered views with savable output |
| `src/ribbon.{h,cpp}` | the collapsible ribbon and its titled groups |
| `src/theme.{h,cpp}` | palette, stylesheet, monospace font pick, contrast-tinted wordmark |
| `src/dialogs.{h,cpp}` | Serial port / Log saving / Terminal / Configurations / About |
| `src/serialport.h` | the Qt-free platform interface |
| `src/serialport_win.cpp` | Win32 backend and port enumeration |
| `src/serialport_posix.cpp` | POSIX backend: termios, `select`, `/sys/class/tty` enumeration |
| `src/serialworker.{h,cpp}` | the serial reader-writer thread |
| `src/lineassembler.{h,cpp}` | CR/LF normalisation and log sanitising |
| `src/lineformat.{h,cpp}` | display formatting: timestamps, direction, hex modes |
| `src/configstore.{h,cpp}` | `config.json`: active state and named configurations |
| `src/appconstants.h` | app identity, defaults, paths |
| `resources/uartx.ico` | executable and installer icon |
| `resources/app.rc.in` | executable icon and version metadata (configured by CMake) |
| `resources/icons/` | icon sizes, wordmark and UI glyphs, compiled into the binary |
| `packaging/windows/installer.iss.in` | Inno Setup script (configured by CMake) |
| `packaging/linux/uartx.desktop.in` | desktop entry (configured by CMake) |
| `packaging/linux/metainfo.xml.in` | AppStream metadata (configured by CMake) |
| `packaging/linux/make-appimage.sh` | builds a self-contained AppImage |
| `scripts/build_release.bat` | one-shot Windows release build |
| `scripts/build_release.sh` | one-shot Linux release build and packaging |
| `scripts/make_icon.py` | regenerates the brand assets (needs Pillow) |
