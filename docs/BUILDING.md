# Building UartX

## Requirements

- Qt 6.2 or newer
- A C++17 compiler and CMake 3.19 or newer
- [Inno Setup 6](https://jrsoftware.org/isinfo.php), only to build the
  installer

No optional Qt modules are needed: only `Qt::Core`, `Qt::Gui` and
`Qt::Widgets` are linked.

## Platform support

UartX runs on **Windows**, **macOS** and **Linux** from a single codebase. The serial port
is reached through a small platform layer (`src/serialport.h`) that is
deliberately free of Qt, so the operating-system differences are confined to
two files:

| File | Platform | Uses |
| --- | --- | --- |
| `src/serialport_win.cpp` | Windows | `CreateFile` / `DCB` / `COMMTIMEOUTS`, registry enumeration |
| `src/serialport_posix.cpp` | Linux, macOS, other Unix | `open` / `termios` / `select`; `/sys/class/tty` enumeration on Linux, `/dev/cu.*` on macOS |

CMake picks the right one automatically. Everything above that layer — the
reader thread, the UI, the configuration — is shared by every platform,
unmodified.
Neither backend uses Qt, which keeps the platform code testable on its own.

> **Keep the two build directories separate.** A CMake build tree records the
> source path and toolchain that created it, so pointing WSL at a tree a
> Windows build made fails with *"does not match the source … used to generate
> cache"*. The scripts already use `build/windows`, `build/macos` and
> `build/linux`.

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

## macOS

The `.dmg` is one **universal** app (x86_64 + arm64) that runs on any Mac with
**macOS 12 Monterey or newer**, and carries its own Qt.

Install the tools once:

```bash
xcode-select --install                      # Apple's compiler
brew install cmake ninja                    # or MacPorts / the cmake.org installer
pip3 install aqtinstall                     # fetches the official Qt build
aqt install-qt mac desktop 6.8.3 clang_64 -O ~/Qt
```

Use the official Qt build (the Qt online installer or `aqtinstall`), not
Homebrew's `qt`. Only the official build is universal, so an app linked
against Homebrew's Qt runs only on the kind of Mac that built it. Qt 6.8 sets
the macOS 12 floor; `CMAKE_OSX_DEPLOYMENT_TARGET` follows it.

Then build:

```bash
scripts/build_macos.sh              # build/macos/UartX.app
scripts/build_macos.sh -p dmg       # + build/macos/UartX-<ver>-macos.dmg
open build/macos/UartX.app
```

The script finds Qt in `$QT_DIR`, then the newest `~/Qt/6.*/macos`, then
Homebrew. Or drive CMake yourself:

```bash
cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/macos
cmake --build build/macos --parallel
cmake --build build/macos --target dmg
```

`CMAKE_OSX_ARCHITECTURES` defaults to `x86_64;arm64`. Pass
`-DCMAKE_OSX_ARCHITECTURES=arm64` for a faster single-architecture build
during development.

### What the `dmg` target does

`packaging/macos/make-dmg.sh` takes these steps:

1. Runs `cmake --install`. Its install step runs **macdeployqt**, which copies
   the Qt frameworks and the cocoa, style and image-format plugins into
   `UartX.app/Contents`.
2. Fails the build if any binary in the bundle still links a library outside
   the bundle and the OS. A `.dmg` like that works on the build machine and
   nowhere else.
3. Signs the bundle, ad hoc by default. Apple Silicon refuses to run unsigned
   code, and macdeployqt's path rewriting breaks the linker's signature, so
   this step is not optional even for an unsigned release.
4. Wraps the app and an `/Applications` shortcut in a compressed `.dmg`.

### Serial differences on macOS

- Ports are enumerated from `/dev/cu.*`, never `/dev/tty.*`. The `tty.*` node
  blocks on open until carrier detect, which a USB-serial adapter never asserts.
- Rates outside the standard `B*` table (460800, 921600, 1500000, …) are set
  with the `IOSSIOSPEED` ioctl after `tcsetattr`.
- Mark/space parity (`CMSPAR`) and 1.5 stop bits do not exist in the macOS
  termios API. They are refused at open with a message that names the setting.
- Exclusive access uses `TIOCEXCL` plus `flock()`, because not every macOS
  driver honours `TIOCEXCL` on its own.

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

### macOS

| Command | Result |
| --- | --- |
| `cmake --build build/macos --target dmg` | `build/macos/UartX-<ver>-macos.dmg` |
| `cmake --install build/macos --prefix <dir>` | a self-contained `UartX.app` at `<dir>` |

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
UartX-1.3.0-setup.exe /VERYSILENT /SUPPRESSMSGBOXES
```

## Regenerating the icons

Every brand asset is derived from two masters, so the logo is changed in one
place and regenerated everywhere:

```
resources/logo/uartx-512-white.png    the artwork in white, on transparency
resources/logo/uartx-512-black.png    the same artwork in black
```

```
pip install pillow
python scripts/make_icon.py
```

That writes:

| Output | Treatment | Why |
| --- | --- | --- |
| `resources/uartx.icns` | the same plate, padded to Apple's 824/1024 icon grid | The macOS app-bundle icon. A Mac icon that fills its whole square looks oversized next to every other icon in the Dock. |
| `resources/uartx.ico` | white on a dark rounded plate | The app icon is the one asset that cannot adapt — it lands on a taskbar or launcher that may be light or dark, and a single-ink transparent logo disappears against one of them. |
| `resources/icons/uartx_<N>.png` | same plate, 16–256 | Qt loads these without the ICO image plugin, which is not always deployed, and the Linux icon theme wants PNGs. |
| `resources/icons/uartx_logo.png` | white, transparent, no plate | A **mask**: `Theme::wordmark()` re-inks it at run time to whichever of black or white contrasts with the dialog it is drawn on. It must have no background of its own. |
| `docs/Images/wordmark-{light,dark}.png` | black / white, transparent | A web page cannot tint an image, so both inks ship and a `<picture>` element picks one from the reader's theme. |
| `resources/icons/chevron_down*.png`, `check.png` | drawn, not derived | Qt stops drawing its own arrow once `QComboBox::drop-down` is styled, and the same goes for a styled checkbox tick. |

Each icon frame is composed at its own size rather than downscaled from one
master, so the plate's corner radius and hairline edge stay crisp at 16 px.

To change the logo, replace the two masters and re-run the script — nothing
else needs editing.

## Cutting a release

1. Bump the version in the `project()` call in `CMakeLists.txt`. It flows into
   the executable metadata, the About box and the installer file name at once.
2. Move the `[Unreleased]` notes in `CHANGELOG.md` under the new version and
   date them.
3. Commit, then push a tag:

   ```
   git tag v1.2.1
   git push origin v1.2.1
   ```

The **Release** workflow then builds all three platforms in parallel and
attaches everything to a GitHub Release:

| Job | Artifacts |
| --- | --- |
| Windows | `UartX-<ver>-setup.exe`, `UartX-<ver>-portable-win64.zip`, `SHA256SUMS-windows.txt` |
| macOS (universal) | `UartX-<ver>-macos.dmg`, `SHA256SUMS-macos.txt` |
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

### Windows

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
signtool sign /f cert.pfx /p <password> /tr http://timestamp.digicert.com /td sha256 /fd sha256 UartX-1.3.0-setup.exe
```

`signtool` ships with the Windows SDK.

### macOS

The `.dmg` is **ad-hoc signed and not notarized**. On first launch Gatekeeper
says Apple could not verify UartX. The user clicks **Open Anyway** under
*System Settings → Privacy & Security*, or runs
`xattr -dr com.apple.quarantine /Applications/UartX.app`. To remove that step:

1. Join the Apple Developer Program and create a
   *Developer ID Application* certificate.
2. Build with it: `UARTX_CODESIGN_IDENTITY="Developer ID Application: <name> (<team>)" scripts/build_macos.sh -p dmg`.
   `make-dmg.sh` then signs with the hardened runtime and a secure timestamp.
3. Notarize and staple the image:

   ```bash
   xcrun notarytool submit build/macos/UartX-<ver>-macos.dmg \
         --apple-id <id> --team-id <team> --password <app-specific-password> --wait
   xcrun stapler staple build/macos/UartX-<ver>-macos.dmg
   ```

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
| `src/serialport_posix.cpp` | POSIX backend (Linux and macOS): termios, `select`, `/sys/class/tty` or `/dev/cu.*` enumeration |
| `src/serialworker.{h,cpp}` | the serial reader-writer thread |
| `src/lineassembler.{h,cpp}` | CR/LF normalisation and log sanitising |
| `src/lineformat.{h,cpp}` | display formatting: timestamps, direction, hex modes |
| `src/configstore.{h,cpp}` | `config.json`: active state and named configurations |
| `src/appconstants.h` | app identity, defaults, paths |
| `resources/uartx.ico` | executable and installer icon |
| `resources/uartx.icns` | macOS app-bundle icon |
| `resources/app.rc.in` | executable icon and version metadata (configured by CMake) |
| `resources/logo/` | the logo masters, in both inks — the source every other asset is derived from |
| `resources/icons/` | icon sizes, the About-box mask and UI glyphs, compiled into the binary |
| `packaging/windows/installer.iss.in` | Inno Setup script (configured by CMake) |
| `packaging/macos/Info.plist.in` | app-bundle metadata (configured by CMake) |
| `packaging/macos/make-dmg.sh` | deploys Qt into the bundle, signs it, builds the `.dmg` |
| `packaging/linux/uartx.desktop.in` | desktop entry (configured by CMake) |
| `packaging/linux/metainfo.xml.in` | AppStream metadata (configured by CMake) |
| `packaging/linux/make-appimage.sh` | builds a self-contained AppImage |
| `scripts/build_release.bat` | one-shot Windows release build |
| `scripts/build_release.sh` | one-shot Linux release build and packaging |
| `scripts/build_macos.sh` | one-shot macOS build and `.dmg` |
| `scripts/make_icon.py` | regenerates the brand assets (needs Pillow) |
