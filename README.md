# UartX

**Turn thousands of lines of firmware output into the information you actually need.**

[![Release](https://img.shields.io/github/v/release/Shamanthpoojary/UartX?sort=semver)](https://github.com/Shamanthpoojary/UartX/releases/latest)
[![CI](https://github.com/Shamanthpoojary/UartX/actions/workflows/ci.yml/badge.svg)](https://github.com/Shamanthpoojary/UartX/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Platform: Windows and Linux](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)

UartX is a UART-based debugging and analysis tool built specifically for
embedded and firmware developers.

## Sound familiar?

Your board is printing at 115200 baud. Six tasks, all logging, all interleaved.
Somewhere in that wall of text a sensor read is failing — once every few
minutes, never in the same place twice. You need to see the failure *and* what
the other tasks were doing when it happened.

So you scroll. Then you stop scrolling and start deleting: comment out the
chatty task's prints, drop the log level, rebuild, reflash. Now the output is
readable — and the bug has moved, because you changed the timing. Or it stopped
reproducing. Or it is still there, but you removed the one line that would have
explained it.

**That is the trade UartX removes.**

Leave every print in the firmware. Define a rule for what you are hunting, open
a Filter window, and watch only those lines — live, in their own window — while
the complete unfiltered stream keeps being written to disk exactly as the device
sent it.

You filter the view, never the data. When you finally catch the failure, the
full log is still sitting there with all the context you did not think you would
need.

## How it works

**1. Say what matters.** A colour rule is a name, a keyword your firmware
actually prints, and a colour. `|E|` in red. `TASK6` in amber. Nothing is built
in — UartX assumes nothing about your log format, so the rules are whatever your
firmware happens to emit.

**2. Open a window on it.** Tick that rule in a Filter window and you see only
the lines it matches. Open several windows to watch different subsets side by
side — one for faults, one for a single task, one for a state machine — each
following the live stream independently.

**3. The full log keeps recording.** The session log holds the raw bytes the
device sent and nothing else: no timestamps, no direction markers, no hex
formatting, no colours. Filters, highlighting and display options never touch
it. What lands on disk is what came off the wire.

## Install

Everything comes from the
**[Releases page](https://github.com/Shamanthpoojary/UartX/releases/latest)**.
Pick the file for your system — nothing else needs installing first.

### Windows

| Download | Use it when |
| --- | --- |
| `UartX-<version>-setup.exe` | Normal install. Start Menu entry, optional desktop shortcut, proper uninstaller. No administrator rights required. |
| `UartX-<version>-portable-win64.zip` | You would rather not install anything. Unzip and run `UartX.exe`. |

Both carry their own Qt runtime.

> **SmartScreen warning.** The installer is not code-signed, so Windows shows
> "Windows protected your PC" the first time it runs on a machine.
> Choose **More info → Run anyway**. See
> [docs/BUILDING.md](docs/BUILDING.md#code-signing) for why, and what signing
> would take.

> **No COM ports listed?** That is almost always the USB-serial adapter's driver
> rather than UartX. Check **Device Manager → Ports (COM & LPT)**. CP210x and
> FTDI adapters usually install themselves over Windows Update; CH340/CH341
> clones normally need their driver installed by hand.

### Linux

| Download | Use it when |
| --- | --- |
| `UartX-<version>-x86_64.AppImage` | **Any distribution.** Self-contained — bundles Qt, needs no root and installs nothing. |
| `uartx_<version>_amd64.deb` | Debian, Ubuntu, Mint, Raspberry Pi OS. Proper package with a menu entry and icon; uses your distribution's Qt. |
| `uartx-<version>-Linux.tar.gz` | You want to unpack it yourself. |

**AppImage — works everywhere:**

```bash
cd ~/Downloads
chmod +x UartX-1.1.0-x86_64.AppImage
./UartX-1.1.0-x86_64.AppImage
```

**`.deb` — Debian and Ubuntu:**

```bash
cp uartx_1.1.0_amd64.deb /tmp/
sudo apt install /tmp/uartx_1.1.0_amd64.deb
```

Then launch **UartX** from the applications menu, or run `UartX` (capital U
and X).

> **Copy the `.deb` to `/tmp` first.** apt drops privileges to the `_apt` user
> to fetch packages and cannot read inside your home directory, so installing
> from `~` prints `Download is performed unsandboxed as root...`. It is only a
> notice, but `/tmp` avoids it.

> **Serial permissions — do this once.** Serial devices belong to a group your
> user is not in, so the first connection fails with "permission denied":
>
> ```bash
> sudo usermod -a -G dialout $USER   # Debian, Ubuntu, Raspberry Pi OS
> sudo usermod -a -G uucp    $USER   # Arch, Fedora, openSUSE
> ```
>
> **Log out and back in** for it to take effect. UartX shows this hint in the
> error dialog when it hits the problem.

> **The `.deb` needs Qt 6.2 or newer from your distribution.** Ubuntu 24.04 and
> newer renamed those packages (`libqt6core6` became `libqt6core6t64`), so a
> `.deb` built on 22.04 cannot satisfy its dependencies there. Use the
> **AppImage** on those releases, or build from source.

> **Older distributions and the AppImage.** Ubuntu 22.04 and earlier need FUSE 2
> (`sudo apt install libfuse2`). To skip it entirely, run the AppImage with
> `--appimage-extract-and-run`.

### Verifying a download

Each platform's assets ship with a checksum file — `SHA256SUMS-windows.txt` or
`SHA256SUMS-linux.txt`:

```bash
sha256sum -c SHA256SUMS-linux.txt        # Linux
```

```powershell
Get-FileHash UartX-1.1.0-setup.exe -Algorithm SHA256    # Windows
```

## Quick start

1. Pick the **Port** and **Baud** rate in the ribbon — **Refresh** re-scans
   after you plug a device in.
2. Press **Connect** (**F2**). Traffic appears live.
3. Open *Tools > Color rules* (**Ctrl+R**) and add a rule: a name, a keyword
   your firmware prints, and a colour.
4. Open *Tools > New filter window* (**Ctrl+F**) and tick that rule. You are now
   watching only those lines, while the full log keeps running behind it.

Full walkthrough: **[docs/USAGE.md](docs/USAGE.md)**.

## What it does

- **Colour rules** — each rule is a *name*, a *keyword* and a *colour*. Any line
  containing the keyword is shown entirely in that colour. Rules are evaluated in
  order and the first match wins, so specific rules sit above general ones.
  Reorder, disable or make them case-sensitive without deleting anything.
- **Filter windows** — separate windows showing only the lines you select, by
  colour rule or by text filter. Tick several and a line shows if it matches any
  of them. Open as many windows as you need; each keeps its own selection and
  follows the live stream on its own.
- **Search what you have captured** — look back through the lines a window has
  already collected. Following pauses so results stay put, **Previous** /
  **Next** step through matches, and clearing the box resumes live following.
- **Savable filtered output** — write just the visible lines to their own file,
  with a header recording exactly which rules, filters and find text produced it.
  Independent of the session log.
- **Raw session logging** — off by default. Turn it on and UartX records the
  bytes the device sent and nothing else. A new file per port open, with
  placeholders for date, time and port name.
- **Serial terminal** — live port enumeration; configurable baud rate, data bits,
  parity, stop bits and flow control. Auto-reconnects when the device reboots or
  the adapter blips.
- **Send box with history** — type a command, press Enter, recall earlier
  commands with Up/Down. Recalled text stays editable, and history persists
  between runs.
- **Optional display aids** — millisecond timestamps, RX/TX indicators, and
  ASCII / HEX / HEX + ASCII modes. All off by default and strictly display-only:
  switching to hex never changes which lines your rules match, and never changes
  what lands in the log.
- **Collapsible ribbon** — the controls you need while debugging, grouped and
  foldable. Optional aids stay out of the way under *Settings*.
- **Persistent configuration** — serial settings, terminal options, log setup,
  colour rules and filters are saved automatically and restored at startup. Named
  configurations let you keep a setup per board or per project.
- **CR/LF normalisation** — LF, CRLF, bare CR and LFCR all render as exactly one
  line break, including when a terminator is split across read chunks.

## Documentation

| Document | Contents |
| --- | --- |
| [docs/USAGE.md](docs/USAGE.md) | Every feature in detail, the configuration file, menus and shortcuts |
| [docs/BUILDING.md](docs/BUILDING.md) | Requirements, build and packaging targets, releases, code signing, source layout |
| [CHANGELOG.md](CHANGELOG.md) | What changed in each version |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to report a bug or propose a change |

## Building from source

Needs Qt 6.2 or newer, a C++17 compiler and CMake 3.19 or newer.

**Linux:**

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev libgl1-mesa-dev
git clone https://github.com/Shamanthpoojary/UartX.git
cd UartX
scripts/build_release.sh            # or -p deb / -p tgz / -p appimage
./build/linux/UartX
```

**Windows:**

```
git clone https://github.com/Shamanthpoojary/UartX.git
cd UartX
scripts\build_release.bat
```

Either script produces the packages for its platform in one step. See
[docs/BUILDING.md](docs/BUILDING.md) for the detail, including the other
distributions' package names.

## Project layout

```
src/                  application sources and headers
resources/            uartx.ico, the Windows resource template, and icons/
scripts/              release build script and icon generator
packaging/windows/    Inno Setup template
docs/                 usage and build documentation
.github/workflows/    CI and release automation
```

The serial port is reached through `src/serialport.h`, a deliberately Qt-free
platform interface, with one backend per platform —
`src/serialport_win.cpp` (Win32) and `src/serialport_posix.cpp` (termios).
Everything above that line is shared between both, unmodified, so supporting
another operating system means adding one more backend `.cpp` and nothing else.

## Licence

[MIT](LICENSE) · Copyright (c) 2026 Shamanth

Support: **shamanth25402@gmail.com**
