---
id: YAR-002
title: Windows serial reads had no total timeout, so the search for a detector hung on a silent COM port
type: bug
status: fixed
severity: high
platforms: [windows]
area: hardware-io
pattern:
aliases: [Y2]
reported: 2026-10-21
source: audit 2026-10-21
where: monitor/yarrow_serial.c:32 @ 1ecf955
fixed_in: 3fe24a9
---

## Symptom

On Windows the monitor stopped looking for a detector: the first COM port that opened but never answered (an onboard
COM1, the USB serial chip of a GQ GMC counter) blocked the search for good. No "found" line, no temporary exit.

## Cause

`yser_open()` set `COMMTIMEOUTS {20, 0, 0, 0, 0}` (monitor/yarrow_serial.c:32 at 1ecf955): only `ReadIntervalTimeout`.
Without a total timeout `ReadFile` waits for the first byte without limit; the interval timer only starts after a
byte. `rd()` reads one byte per `ReadFile` and is used by `yser_command()`, which `yser_find()` calls on COM1 to COM64
(`LIVE 0`, `INFO`). The per-call timeouts of `yser_read()` (raw reads for GQ GMC) were already right at 1ecf955
(line 273). Verified by reading; not run on Windows.

## Fix

3fe24a9 (2026-10-25) sets `{MAXDWORD, MAXDWORD, 20, 0, 0}` (monitor/yarrow_serial.c:35 at 78b1446): `ReadFile` returns
at once with the bytes that are there, and otherwise waits at most 20 ms for the first one (COMMTIMEOUTS semantics as
documented by Microsoft). Verified by reading at 78b1446; not run on Windows.

Not covered by the fix, and not verified: the write timeouts are still 0, and the DCB keeps the port's own
flow-control settings (only baud rate, byte size, parity, stop bits and DTR are set, yarrow_serial.c:22-31). A port
with CTS flow control enabled and CTS held low could block `WriteFile` the same way.

## Test

None automatic. On Windows: a COM port that opens and stays silent (a USB serial adapter with nothing attached, or a
com0com pair); the search must move on after about a second per port, and stderr must show it repeating every 15 s.

## Log

- 2026-10-21: found by the audit at 1ecf955 (plan item Y2), by reading.
- 2026-10-27: recorded from the 2026-10-21 audit; fixed upstream in 3fe24a9, verified by reading at 78b1446.
