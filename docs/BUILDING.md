# Building UartX

## Requirements

- Qt 6.2 or newer
- A C++17 compiler and CMake 3.19 or newer
- [Inno Setup 6](https://jrsoftware.org/isinfo.php), only to build the
  installer

No optional Qt modules are needed: only `Qt::Core`, `Qt::Gui` and
`Qt::Widgets` are linked.

## Platform support

UartX targets **Windows**. The serial port is reached through a small platform
layer (`src/serialport.h`) that is deliberately free of Qt, so the
operating-system differences are confined to a single file:

| File | Platform | Uses |
| --- | --- | --- |
| `src/serialport_win.cpp` | Windows | `CreateFile` / `DCB` / `COMMTIMEOUTS`, registry enumeration |

Everything above that layer — the reader thread, the UI, the configuration —
is platform-independent. Supporting another operating system means adding one
more `.cpp` behind the same interface; nothing above it changes. CMake stops
with an explanatory message if configured on a non-Windows platform.

## Qt Creator

Open `CMakeLists.txt`, pick a Desktop Qt 6 kit, then Build & Run.

## One-shot release build

```
scripts\build_release.bat
```

Produces the executable, the installer and the portable zip in
`build\windows\`. Edit `QT_DIR` at the top of the script if your Qt lives
somewhere else. The script can be run from any directory.

## Command line

```
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64
cmake --build build/windows
```

## Packaging targets

Three targets produce distributable artifacts. All of them run `windeployqt`,
so the result carries its own Qt runtime and needs no Qt installation on the
target machine.

| Command | Result |
| --- | --- |
| `cmake --build build/windows --target installer` | `build/windows/installer/UartX-<ver>-setup.exe` |
| `cmake --build build/windows --target portable` | `build/windows/installer/UartX-<ver>-portable-win64.zip` |
| `cmake --install build/windows --prefix <dir>` | a self-contained folder at `<dir>` |

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
UartX-1.0.0-setup.exe /VERYSILENT /SUPPRESSMSGBOXES
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
   git tag v1.0.1
   git push origin v1.0.1
   ```

The **Release** workflow builds the installer and the portable zip, writes a
`SHA256SUMS.txt`, and attaches all three to a GitHub Release. It is set to
publish nothing rather than an empty release if a build step produces no
artifacts.

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
signtool sign /f cert.pfx /p <password> /tr http://timestamp.digicert.com /td sha256 /fd sha256 UartX-1.0.0-setup.exe
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
| `src/serialworker.{h,cpp}` | the serial reader-writer thread |
| `src/lineassembler.{h,cpp}` | CR/LF normalisation and log sanitising |
| `src/lineformat.{h,cpp}` | display formatting: timestamps, direction, hex modes |
| `src/configstore.{h,cpp}` | `config.json`: active state and named configurations |
| `src/appconstants.h` | app identity, defaults, paths |
| `resources/uartx.ico` | executable and installer icon |
| `resources/app.rc.in` | executable icon and version metadata (configured by CMake) |
| `resources/icons/` | icon sizes, wordmark and UI glyphs, compiled into the binary |
| `packaging/windows/installer.iss.in` | Inno Setup script (configured by CMake) |
| `scripts/build_release.bat` | one-shot release build |
| `scripts/make_icon.py` | regenerates the brand assets (needs Pillow) |
