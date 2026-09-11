# Using UartX

UartX exists to make a busy firmware log readable without editing the firmware.
The terminal is how the data gets in; **colour rules (section 4) and filter
windows (section 5) are the point**. If you are in a hurry, connect a port and
skip straight to those two.

One principle runs through everything below: **you filter the view, never the
data.** Colour rules, filter windows, timestamps, RX/TX markers and hex display
all change what you *see*. None of them change what your rules match, and none
of them change what the session log records — that stays the raw bytes the
device sent. So you never have to choose between a readable screen and a
complete capture.

## 1. Configure the serial port

Set the **Port** and **Baud** rate in the Connection group of the ribbon. Use
**Refresh** after plugging in a device to re-scan. For data bits, parity, stop
bits and flow control open *Settings > Serial port*. Settings take effect the
next time the port is opened.

The port list is populated from the machine: `COM1`, `COM3`, … on Windows;
`/dev/ttyUSB0`, `/dev/ttyACM0`, `/dev/ttyAMA0`, … on Linux. You can also type a
port the scan did not find.

> **Linux permissions — do this once.** Serial devices belong to a group your
> user is not in, so the first connection attempt fails with "permission
> denied":
>
> ```bash
> sudo usermod -a -G dialout $USER   # Debian, Ubuntu, Raspberry Pi OS
> sudo usermod -a -G uucp    $USER   # Arch, Fedora, openSUSE
> ```
>
> Log out and back in for it to take effect. UartX shows this hint in the error
> dialog when it hits the problem.

## 2. Connect

Press **Connect** (or **F2**). Incoming lines appear live and the status bar
shows the connection state and byte count. Typing sends characters to the
device; **Enter** transmits the line ending chosen under
*Settings > Terminal*.

## 3. Send data to the device

Use the **Send** box under the terminal: type a command and press **Enter** or
click **Send**. Sent data is echoed into the terminal in its own shade so it is
easy to tell apart from what the device produced.

- **Up** / **Down** in the Send box recall earlier commands, which stay
  editable before sending. History persists between runs.
- The **Line Ending** appended to everything you send is set under
  *Settings > Terminal*: **None**, **LF**, **CR** or **CRLF**.
- Typing directly into the terminal still sends individual keystrokes.

## 4. Colour rules — highlight important lines

*Tools > Color rules* (**Ctrl+R**). A rule is a **name**, a **keyword** and a
**colour**:

| Field | Example |
| --- | --- |
| Name | `Error` |
| Keyword | `\|E\|` |
| Colour | red |

Any line containing the keyword is displayed entirely in that colour.

- Rules are evaluated top to bottom and the **first match wins**, so put
  specific rules above general ones (**Move up** / **Move down**).
- The **On** checkbox disables a rule without deleting it.
- **Match case** stops a keyword such as `|E|` matching `|e|`.

Colour rules only change how lines *look*. They never hide anything — that is
what filters are for.

A fresh installation ships four ordinary starter rules (Error, Warning, Info,
Debug). They are only a suggestion; edit or delete them freely.

## 5. Filters — show only the lines you want

*Tools > New filter window* (**Ctrl+F**). A Filter window shows a subset of the
traffic, chosen two ways:

- **Colour rules** — tick a rule to show every line it highlights. The
  categories you already defined for colouring double as filters, and the
  lines keep their colour.
- **Text filters** — tick a filter to show every line containing its text. A
  filter of `/6/` shows only the lines containing `/6/`. **Add** creates one;
  it joins your filter library and is available in every Filter window.

Ticking several sources combines them: a line is shown if it matches **any**
ticked rule or filter. With nothing ticked every line is shown. **Quick find**
narrows whatever gets through, without saving a filter.

Open several Filter windows to watch different subsets side by side; each
keeps its own selection. Filters apply to incoming lines only: a window starts
empty and follows the live stream, which keeps it responsive on long captures.

**Search captured lines** looks through what a window has already collected,
rather than changing what it collects. Typing jumps to the first match and
pauses follow-live so results stay put; **Previous** / **Next** step through
them, wrapping at the ends. Clearing the box resumes live following.

**Save output** writes the visible lines to a file of your choosing,
independent of the session log. The file opens with a header recording exactly
which settings produced it:

```
===== UartX 1.1.0 - filtered output =====
Saved         : 2026-08-21 12:05:43
Source        : COM5 @ 115200 baud
Color rules   : Fatal
Text filters  : Task 6 ("/6/")
Quick find    : (none)
Lines         : 3
=========================================
```

## Optional debugging aids

Under *Settings > Terminal*, off until you turn them on:

| Option | Effect |
| --- | --- |
| Timestamps | prefixes each line with its arrival time, to the millisecond |
| RX / TX indicators | labels each line with its direction |
| Display data as | shows traffic as ASCII, HEX, or HEX + ASCII side by side |

These affect the display only. The session log always records the raw UART
data, and colour rules and filters always match the raw text, so switching to
hex never changes which lines match.

## 6. Save UART logs

Log saving is **off by default**. Turn it on in *Settings > Log saving* and
choose a destination folder — leave it blank to use the default location. The
dialog confirms whether the folder is writable, and UartX reports a clear
error rather than connecting if the location cannot be used.

A new file is created each time the port is opened. The file name accepts
these placeholders:

| Placeholder | Meaning |
| --- | --- |
| `&Y` | year |
| `&M` | month |
| `&D` | day |
| `&T` | time (HHMMSS) |
| `&H` | the port name |

Default: `uartx_&Y-&M-&D_&H_&T.log`. The log holds the data received from the
device and nothing else: no timestamps, direction markers, hex formatting or
colours, and no data you sent.

## 7. Manage configurations

A configuration is a named snapshot of the serial settings, terminal options,
log-saving setup, colour rules and filters.

- Pick one in the ribbon and press **Load**. The name box is filled in with
  the loaded name.
- Change whatever you like, then press **Save** to write the changes back to
  that same configuration.
- To branch off instead, type a different name before pressing **Save**.
- **Delete** removes the selected configuration.

Your current settings are also saved automatically and restored next time
UartX starts, whether or not they belong to a named configuration.

## Configuration file

Everything the user can change is stored in one file and reloaded at startup:

| Platform | Location |
| --- | --- |
| Windows | `%LOCALAPPDATA%\UartX\config.json` |
| Linux | `~/.config/UartX/config.json` |

Logs default to `Documents/UartX_Logs` under your home directory on both
platforms, and can be pointed anywhere. The folder is created the first time a
session needs it.

It holds the serial settings, terminal options, log-saving state and path,
every colour rule (name, keyword, colour, enabled state, match-case flag),
every text filter, the name of the loaded configuration, and any named
configurations. Nothing project-specific is hardcoded anywhere; the built-in
defaults are only the starting point for a fresh installation.

Writes are debounced, and the file is also flushed when you connect and when
you exit.

## Menu structure

- **Session** — Connect/Disconnect (F2), Clear screen (Ctrl+L), Open log
  folder, Exit
- **Settings** — Serial port…, Log saving…, Terminal…, Configurations…
- **Tools** — Color rules… (Ctrl+R), New filter window… (Ctrl+F)
- **View** — Show ribbon (Ctrl+Shift+R)
- **Help** — About UartX

## Keyboard and mouse

| Input | Action |
| --- | --- |
| **F2** | Connect / disconnect |
| **Ctrl+R** | Colour rules |
| **Ctrl+F** | New filter window |
| **Ctrl+L** | Clear the screen |
| **Ctrl+C** | Copy the selection (shows a brief "N characters copied" notice) |
| **Right-click** | Copy the selection |
| Any other key | Sent to the device; Enter sends the selected line ending |
