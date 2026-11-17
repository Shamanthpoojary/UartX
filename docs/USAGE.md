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
`/dev/ttyUSB0`, `/dev/ttyACM0`, `/dev/ttyAMA0`, … on Linux;
`/dev/cu.usbserial-…`, `/dev/cu.usbmodem…`, `/dev/cu.SLAB_USBtoUART`, … on
macOS. You can also type a port the scan did not find.

> **macOS uses the `cu.*` nodes.** macOS creates two device files per adapter.
> The `tty.*` one waits for a carrier-detect signal that USB-serial adapters
> never raise, so UartX lists only the `cu.*` one. The built-in
> `Bluetooth-Incoming-Port` is hidden. Paired Bluetooth serial devices do
> appear, as on Linux. No group membership or `sudo` is needed on a Mac.

When nothing is connected the box is left empty and reads *No ports found*,
rather than keeping the port from the last session — plug the device in and
press **Refresh**.

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

*Tools > New filter window* (**Ctrl+F**). A filter view opens **beside the
terminal**, splitting the window so you read the filtered lines without losing
sight of the stream they came from. Drag the divider to change the balance.

**Pop out** moves that view into a window of its own — useful on a second
monitor — and **Dock** puts it back. **Close** drops it.

**Only the first view docks.** The split pane holds one filter view at a time;
open a second and it arrives as a window of its own, which keeps the docked
view and the terminal at a readable width however many views you have. Pressing
**Dock** on a floating view swaps it into the pane and pops the current
occupant out, so the split always shows the one view you picked for it. Every
view follows the live stream independently, docked or not.

A filter view shows a subset of the traffic, chosen two ways:

- **Colour rules** — tick a rule to show every line it highlights. The
  categories you already defined for colouring double as filters, and the
  lines keep their colour.
- **Text filters** — tick a filter to show every line containing its text. A
  filter of `/6/` shows only the lines containing `/6/`. **Add** creates one;
  it joins your filter library and is available in every Filter window.

A new window opens with **every colour rule already ticked**, so it starts by
showing every categorised line and you narrow from there by unticking.

Ticking several sources combines them: a line is shown if it matches **any**
ticked rule or filter. Untick everything and the window shows nothing — what
is ticked is exactly what you see. (The one exception: if you have no colour
rules and no text filters at all, there is nothing to choose from, so the
window mirrors the terminal instead of sitting empty.)

**Quick find** narrows whatever gets through, without saving a filter.

Open several Filter windows to watch different subsets side by side; each
keeps its own selection, and all but the docked one are ordinary windows you
can move, resize and put on another screen. Filters apply to incoming lines only: a window starts
empty and follows the live stream, which keeps it responsive on long captures.

**Search captured lines** looks through what a window has already collected,
rather than changing what it collects. Typing jumps to the first match and
pauses follow-live so results stay put; **Previous** / **Next** step through
them, wrapping at the ends. Clearing the box resumes live following.

**Save output** writes the visible lines to a file of your choosing,
independent of the session log. The file opens with a header recording exactly
which settings produced it:

```
===== UartX 1.3.0 - filtered output =====
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
| Display data as | shows traffic as ASCII, Escaped (control bytes spelled out as `\r`, `\x1b`…), HEX, or HEX + ASCII side by side |
| Use the colors the device sends | obeys ANSI colour codes in the incoming data |

These affect the display only. The session log always records the raw UART
data, and colour rules and filters always match the decoded text with escape
sequences removed, so switching to hex never changes which lines match.

### Line framing and text encoding

Under *Settings > Serial port*, applied the next time the port is opened:

| Setting | Choices |
| --- | --- |
| Line framing | **Auto** (CR, LF, CRLF and LFCR each end a line) · Custom terminator (any bytes, e.g. `;` or `\x03`) · Idle gap (a pause ends a record — for binary protocols) · Fixed length · None |
| Text encoding | **Auto** · UTF-8 · Latin-1 · Windows-1252, plus GBK, Shift_JIS and other legacy codecs where your Qt build provides them |

**Auto** encoding shows valid UTF-8 (which includes plain ASCII) as text and
any other byte as `‹xNN›` in place, so binary data and a wrong baud rate look
like what they are instead of silently turning into question marks. A line
that never ends is cut at 4 KB, and a line that goes quiet without ending is
shown after 250 ms and completed in place when the rest arrives; filter
windows receive it once, complete.

### When the device is faster than the screen

Reading, logging and matching run on their own thread; the window only draws.
If the device sends faster than the screen can be redrawn, the terminal skips
ahead and says so — `… 12,408 lines not displayed (all saved to log) …` —
while the log keeps every line.

**Device colours.** Firmware that colours its own output with ANSI codes is
shown in those colours rather than having them stripped. Where one of your
colour rules matches, the rule wins — a rule is you saying what matters, and
that outranks the firmware's own opinion. Turn the option off to see every line
in the default ink and let your rules do all the colouring.

## 6. Save UART logs

Log saving is **off by default**. Turn it on in *Settings > Log saving* and
choose a destination folder — leave it blank to use the default location. The
dialog confirms whether the folder is writable, and UartX reports a clear
error rather than connecting if the location cannot be used.

Each time the port is opened UartX starts a new **session folder** in the log
folder. Nothing is created until the first byte arrives, so a session that
receives nothing leaves nothing behind.

```
uartx_2026-11-17_COM5_143205_s1/
  session.json      port, the settings it was opened with, and its runs
  run-001/
    raw.log         the exact bytes received — nothing added, nothing changed
    lines.log       one line per record:  seq <TAB> time <TAB> direction/origin <TAB> text
    lines.idx       an index into lines.log, so any line can be found instantly
```

- **raw.log** is byte-for-byte what the device sent, line endings included.
  (Until 1.3 every line ending was rewritten as LF; it no longer is.)
- **lines.log** numbers every record — received lines, what you sent, and
  UartX's own notes such as connects and disconnects — and stamps it with the
  time the computer received it, e.g.
  `5543  2026-11-17T14:32:05.123+05:30  RX/device  E/I2C: timeout addr=0x3c`.
  Control bytes and anything that is not valid UTF-8 are spelled out (`\x1b`,
  `\xff`), so the file is plain text that any editor or `grep` can read. Past
  256 MB it continues in `lines.2.log`, `lines.3.log`, …
- Times are when the computer received the data. USB serial adapters deliver
  in packets, so every line in one packet shares a time; order is always by
  the sequence number.

The session folder name accepts these placeholders:

| Placeholder | Meaning |
| --- | --- |
| `&Y` | year |
| `&M` | month |
| `&D` | day |
| `&T` | time (HHMMSS) |
| `&H` | the port name |

Default: `uartx_&Y-&M-&D_&H_&T` (a trailing `.log` from older settings is
dropped), followed by the session number.

**How long logs are kept.** *Keep logs for* defaults to **12 hours and at most
1 GB**; choose anything from 1 hour to forever, and a size cap up to no limit.
Cleanup runs at startup and every hour, oldest sessions first. The session that
is open is never removed — if it alone grows past the size cap, UartX warns
you and deletes nothing — and nothing in the log folder that is not a UartX
session folder (logs from 1.3, your own files) is ever touched. The policy is
shown in the status bar.

**If the disk fills up** or the folder becomes unwritable, UartX keeps showing
the data, marks the status bar `log: STOPPED`, and prints the reason in red in
the terminal.

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
| macOS | `~/Library/Preferences/UartX/config.json` |
| Linux | `~/.config/UartX/config.json` |

Logs default to `Documents/UartX_Logs` under your home directory on every
platform, and can be pointed anywhere. The folder is created the first time a
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

On a Mac, every **Ctrl** shortcut below is **⌘ Command** instead (⌘R, ⌘F, ⌘L,
⌘C, ⇧⌘R), and the About box shows them that way. The **Control** key itself
is not a shortcut there: Control+C in the terminal sends 0x03 to the device,
as in Terminal.app.

| Input | Action |
| --- | --- |
| **F2** | Connect / disconnect |
| **Ctrl+R** | Colour rules |
| **Ctrl+F** | New filter window |
| **Ctrl+L** | Clear the screen |
| **Ctrl+C** | Copy the selection (shows a brief "N characters copied" notice) |
| **Right-click** | Copy the selection |
| Any other key | Sent to the device; Enter sends the selected line ending |
