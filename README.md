# UartX — UART Terminal &amp; Log Filter

[![Release](https://img.shields.io/github/v/release/Shamanthpoojary/UartX?sort=semver)](https://github.com/Shamanthpoojary/UartX/releases/latest)
[![CI](https://github.com/Shamanthpoojary/UartX/actions/workflows/ci.yml/badge.svg)](https://github.com/Shamanthpoojary/UartX/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Platform: Windows](https://img.shields.io/badge/platform-Windows-lightgrey)

A general-purpose serial terminal and log viewer. UartX talks to anything that
speaks UART — a microcontroller, a dev board, a module, a USB-to-serial
adapter — and makes a busy log readable by colouring the lines you care about.

UartX assumes **nothing** about your device's message format. There are no
built-in keywords, severity levels or log conventions: every highlight rule is
one you define, so it works the same whether your firmware prints `|E| fault`,
`[ERROR]` or something else entirely.

## Install

Download the latest build from the
**[Releases page](https://github.com/Shamanthpoojary/UartX/releases/latest)**:

| Download | Use it when |
| --- | --- |
| `UartX-<version>-setup.exe` | Normal install. Start Menu entry, optional desktop shortcut, proper uninstaller. No administrator rights required. |
| `UartX-<version>-portable-win64.zip` | You would rather not install anything. Unzip and run `UartX.exe`. |

Both carry their own Qt runtime, so nothing else needs installing.

Every release also ships `SHA256SUMS.txt` if you want to verify the download:

```powershell
Get-FileHash UartX-1.0.0-setup.exe -Algorithm SHA256
```

> **SmartScreen warning.** The installer is not code-signed, so Windows shows
> "Windows protected your PC" the first time it runs on a machine.
> Choose **More info → Run anyway**. See
> [docs/BUILDING.md](docs/BUILDING.md#code-signing) for why, and what signing
> would take.

## Quick start

1. Pick the **Port** and **Baud** rate in the ribbon — **Refresh** re-scans
   after you plug a device in.
2. Press **Connect** (**F2**). Traffic appears live.
3. Open *Tools > Color rules* (**Ctrl+R**) and add a rule: a name, a keyword
   your firmware actually prints, and a colour. Every line containing that
   keyword is now shown in it.
4. Open *Tools > New filter window* (**Ctrl+F**) and tick that rule to watch
   only those lines, in their own window, while the full log keeps running.

Full walkthrough: **[docs/USAGE.md](docs/USAGE.md)**.

## Features

- **Serial terminal** — live port enumeration; configurable baud rate, data
  bits, parity, stop bits and flow control. Type to send data back to the
  device, with a selectable line ending and optional local echo.
- **Colour rules** — each rule is a *name*, a *keyword* and a *colour*. Any
  line containing the keyword is shown entirely in that colour. Add, edit,
  delete, reorder, enable and disable as many as you like.
- **Filters** — separate windows showing only the lines you select, by colour
  rule or by text filter, with their output savable to its own file complete
  with a header recording the filter settings.
- **Collapsible ribbon** — the controls needed during normal debugging, in
  grouped sections that fold away via the arrow at the right-hand edge.
  Optional aids stay out of the way under *Settings*.
- **Send box with history** — type a command, press Enter, and recall earlier
  commands with Up/Down; recalled text stays editable.
- **Optional debugging aids** — millisecond timestamps, RX/TX indicators and
  ASCII / HEX / HEX + ASCII display modes. All off by default, all
  display-only.
- **Optional log saving** — off by default. Turn it on, choose a folder, and
  UartX writes a plain-text log for each session, holding the raw data the
  device sent and nothing else.
- **Persistent configuration** — serial settings, terminal options,
  log-saving setup, colour rules and filters are saved automatically and
  reloaded at startup. Named configurations let you keep several setups.
- **CR/LF normalisation** — LF, CRLF, bare CR and LFCR all render as exactly
  one line break, including when a terminator is split across read chunks.
- **Auto-reconnect** — recovers when the device reboots or the USB-serial
  adapter blips.

## Documentation

| Document | Contents |
| --- | --- |
| [docs/USAGE.md](docs/USAGE.md) | Every feature in detail, the configuration file, menus and shortcuts |
| [docs/BUILDING.md](docs/BUILDING.md) | Requirements, build and packaging targets, releases, code signing, source layout |
| [CHANGELOG.md](CHANGELOG.md) | What changed in each version |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to report a bug or propose a change |

## Building from source

Needs Qt 6.2 or newer, a C++17 compiler and CMake 3.19 or newer.

```
git clone https://github.com/Shamanthpoojary/UartX.git
cd UartX
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64
cmake --build build/windows
```

Or `scripts\build_release.bat` for the executable, installer and portable zip
in one step. See [docs/BUILDING.md](docs/BUILDING.md) for the detail.

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
platform interface. Everything above it is platform-independent, so supporting
another operating system means adding one backend `.cpp` and nothing else.
UartX ships the Windows backend today.

## Licence

[MIT](LICENSE) · Copyright (c) 2026 Shamanth

Support: **shamanth25402@gmail.com**
