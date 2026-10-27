---
id: YAR-011
title: The search for a counter opens, reconfigures and writes to the serial ports of other devices
type: bug
status: confirmed
severity: medium
platforms: [all]
area: device-selection
next: Make yser_find() open only ports with USB id 1209:0001, skip ports another process holds (TIOCEXCL); correct docs/boinc-monitor.md:61
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan sections 3.3 and 6.3, "detection writes LIVE 0/INFO to foreign serial ports")
where: monitor/yarrow_serial.c:93-104, 212-275, 376-389; monitor/yarrow_src.c:18-58 @ 78b1446
fixed_in:
---

## Symptom

Expected from the code, not reported yet. While no counter is attached the monitor opens, every 15 s, every serial
port it may open, and writes to it. Devices on those ports can be disturbed: a board of the Arduino kind resets when
its port is opened (DTR); a 3D printer, modem, UPS, GPS receiver or Zigbee stick receives `LIVE 0`, `INFO` and
`<GETVER>>`; and on Linux and macOS a port that another program is using is switched to raw mode at 115200 or 57600
baud under that program.

## Cause

Verified by reading at 78b1446.

- Yarrow search, `yser_find()`: on Windows COM1 to COM64 (monitor/yarrow_serial.c:216-228), opened with DTR enabled
  (30); on POSIX `/dev/ttyACM*` and `/dev/cu.usbmodem*` (234). Each port that opens gets `LIVE 0` and `INFO`
  (222-223, 263-264). No USB id filter, although the GC-03's id is known (1209:0001).
- GQ GMC search, `ysrc_gmc_find()` (monitor/yarrow_src.c:18-58): `/dev/ttyUSB*`, `/dev/ttyACM*`,
  `/dev/cu.usbserial*`, `/dev/cu.usbmodem*` (yarrow_serial.c:376-389) or COM1 to COM64, at 115200 and then 57600 baud,
  each sent `<GETVER>>`.
- POSIX `yser_open()` opens with `O_RDWR | O_NOCTTY | O_NONBLOCK`, takes no exclusive lock (`TIOCEXCL`, `flock`), and
  calls `cfmakeraw` and `tcsetattr` at once (93-104). Terminal settings belong to the device, so they change for every
  program that has the port open; they are not restored on close. On Windows `CreateFileA` with share mode 0 (19)
  fails for a port in use, so only idle ports are touched there.
- Which ports are reachable depends on permissions. On Linux the BOINC user reaches the GC-03 through the udev rule,
  and every USB serial port once it is in `dialout`, as docs/linux.md:21 asks GQ GMC owners to do.
- docs/boinc-monitor.md:61 says "Looking for a counter sends nothing but `<GETVER>>` to the serial ports"; that is not
  true for the Yarrow search.

## Fix

Proposed: filter by USB id before opening (the GC-03's 1209:0001; on Linux from sysfs as `sysfs_ids()` already does
for hidraw, on macOS through IOKit, on Windows through SetupAPI; for GQ GMC the USB serial chips those counters use);
skip ports another process holds (`TIOCEXCL` after the open, or `flock` as many serial programs use); restore the
terminal settings on close; correct docs/boinc-monitor.md:61. No change for the supported counters.

## Test

Manual, Linux: a second program holds `/dev/ttyUSB0` at 9600 baud; `stty -F /dev/ttyUSB0` before and after one search
cycle must show the same settings. An Arduino Uno on `/dev/ttyACM0` must not reset during the search.

## Log

- 2026-10-21: listed by the audit as a design choice to document.
- 2026-10-27: recorded and verified by reading at 78b1446; the change of terminal settings on ports in use and the docs sentence were not in the audit.
