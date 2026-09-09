# Contributing to UartX

Thanks for your interest in UartX. This is a small project, so the process is
light — but a little consistency keeps it maintainable.

## Getting set up

You need Qt 6.2 or newer, a C++17 compiler and CMake 3.19 or newer. Full
instructions, including the one-shot release script, are in
[docs/BUILDING.md](docs/BUILDING.md).

```
git clone https://github.com/Shamanthpoojary/UartX.git
cd UartX
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64
cmake --build build/windows
```

## Repository layout

| Directory | Contents |
| --- | --- |
| `src/` | all application sources and headers |
| `resources/` | `uartx.ico`, the Windows resource template, and `icons/` |
| `scripts/` | release build script and the icon generator |
| `packaging/windows/` | the Inno Setup template |
| `docs/` | usage and build documentation |
| `.github/workflows/` | CI and release automation |

## Reporting a bug

Open an issue using the bug report template. The two things that make a serial
bug actionable are **the device and adapter you were talking to** and **the
exact port settings** — baud rate, data bits, parity, stop bits, flow control.
A short excerpt of the raw log usually settles the rest.

## Making a change

1. Branch off `main`.
2. Keep the change focused. One behaviour per pull request is much easier to
   review than a mixed batch.
3. Build it and exercise the affected area against real hardware. There is no
   automated test suite yet, so a note in the pull request describing what you
   tried and what you saw carries real weight.
4. Open a pull request against `main` and fill in the template.

## Code conventions

The existing code has a consistent style; matching it matters more than any
particular rule.

- **4 spaces**, no tabs. Braces on their own line for functions and types,
  same line for control flow — follow the file you are editing.
- **Comments explain why, not what.** The codebase deliberately documents the
  non-obvious constraint behind a piece of code (why `QT_DEPLOY_BIN_DIR` has
  to be set before the install rule, why the caption needs an explicit
  stretch). Please keep that up; skip comments that restate the line below.
- **No Qt in the platform layer.** `src/serialport.h` and its backend are
  plain C++ so the operating-system code stays independently testable. Adding
  a platform means adding one `.cpp` behind that interface, nothing more.
- **Saved logs stay raw.** The session log records the bytes the device sent
  and nothing else — no timestamps, direction markers, hex formatting or
  colours. Display options must never leak into the file.
- **The palette is black and grey.** The only colour on screen comes from the
  user's own colour rules, so UI work should not introduce accent colours.
- **Nothing device-specific is hardcoded.** There are no built-in keywords,
  severity levels or log conventions; the starter colour rules are defaults a
  user can delete, not assumptions.

## Commits

Write a short imperative subject line — `Fix ribbon caption alignment`, not
`fixed stuff`. Explain the reasoning in the body when the change is not
self-evident.

## Releases

Maintainer notes: bump `project(UartX VERSION …)` in `CMakeLists.txt`, add a
`CHANGELOG.md` entry, then push a `v*` tag. The release workflow builds the
installer and the portable zip and attaches them to a GitHub Release. Details
in [docs/BUILDING.md](docs/BUILDING.md).
