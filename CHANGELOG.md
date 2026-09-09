# Changelog

All notable changes to UartX are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/Shamanthpoojary/UartX/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Shamanthpoojary/UartX/releases/tag/v1.0.0
