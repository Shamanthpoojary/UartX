---
name: uartx
description: Observe and explain a live UART / serial console (firmware boot logs, device debug output, COM ports, /dev/ttyUSB*) through the UartX app. Use whenever the user wants to watch, check, debug or understand what a board prints over UART, or asks why a device misbehaves and its serial log would show it. UartX does the filtering and sends back only the lines asked for, so never read the whole log.
---

# Reading UART logs through UartX

UartX holds the serial port and the full log. You never read the raw stream:
you tell UartX what you are looking for, and it sends back a small, capped
excerpt. Everything you do is shown to the user in UartX — in a **Claude**
tab listing every request, the exact lines you were sent, and your findings —
so the user can follow your reasoning even if they cannot read a UART log
themselves.

Run `uartx-ctl` (on PATH after installing UartX; otherwise
`%LOCALAPPDATA%\Programs\UartX\uartx-ctl.exe` on Windows). Each call prints
compact text; add `--json` for structure. Exit code 5 means UartX is open
but not under your control: run `uartx-ctl launch`.

## Workflow

1. **Take control.** `uartx-ctl launch` — starts UartX under your control,
   or takes over the copy the user already has open (they see that you did).
2. **Open the port.** `uartx-ctl open COM7 --baud 921600` (ask the user for
   port and baud if unknown; `uartx-ctl status` shows the last ones used).
   If another port is open and the user wants this one, add `--force`.
   "port not present yet" means the board is unplugged — tell the user.
3. **Get the shape before any text.** `uartx-ctl summary` returns counts per
   kind of line and session events, no log text. Use it to decide what to ask.
4. **Ask narrowly.**
   - `uartx-ctl search "status=[1-9]" --regex` — matching lines only
     (newest first; `--first` for the earliest, `--before/--after N` for
     context, `--max N` to cap). The footer says how many were left out.
   - `uartx-ctl tail -n 20 [--match P]` — what is happening right now.
   - `uartx-ctl wait "PATTERN" --timeout 5m --before 10 --after 20` — sleeps
     until a NEW line matches, then prints only that moment. For long waits
     run it as a background command and continue when it returns.
5. **Show the user what matters.** `uartx-ctl filter add "Name" "PATTERN"
   [--regex]` opens a live view (an "AI" tab) in UartX with only those lines,
   filled from what already arrived. Name views in plain words ("Bluetooth
   errors", not "bt_err"). `filter list` / `filter rm ID`.
6. **Explain.** `uartx-ctl note "..."` adds a finding to the Claude tab and
   to the log. Write it for someone who has never read a UART log:
   - what the device did, in plain words, and whether that is healthy;
   - what is wrong, why it matters, and what to do about it;
   - the line numbers (#152) that show it, so it can be checked;
   - say what you did *not* check.
   Prefer one clear note per conclusion over one long note.

## Rules

- **Always pass `--why "..."`** on summary, search, tail, wait and
  `filter add`, in plain words a non-engineer understands ("to find
  Bluetooth commands that failed", not "grep status!=0"). The user sees it
  first in the Claude tab; the pattern is shown after it.
- Number your notes when you write several, in the order you want them read.
- Never ask for more than you need. Start with `summary`, then targeted
  `search`. Do not page through the log with `tail -n 200` repeatedly.
- Make no assumptions about the log format; learn it from `summary` and a
  small `tail` first.
- Ask the user before closing a view they made, before `--force`-reopening a
  port they are using, and before recreating a view the user closed (they
  closed it for a reason; `filter list` shows what is still open).
- You cannot send commands to the device through UartX yet; ask the user to
  type them in UartX's Send box (e.g. a reset command) when needed.
- Lines are numbered (`#5543`); refer to them by number in notes and replies.
- The full log is also on disk (`uartx-ctl status` shows the folder):
  `raw.log` is the exact bytes, `lines.log` is numbered text. Prefer the
  commands above; read those files only for something the commands cannot do.
