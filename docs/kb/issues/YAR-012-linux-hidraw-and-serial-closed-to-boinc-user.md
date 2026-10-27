---
id: YAR-012
title: Linux: the BOINC client's user cannot open hidraw and most USB serial nodes, so counters need a udev rule
type: gotcha
status: documented
severity: medium
platforms: [linux]
area: hardware-io
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: linux/99-yarrow.rules:6-13; monitor/yarrow_hid.c:227-246 @ 78b1446
fixed_in: 420a177
---

## Symptom

On Linux the task says "No detector found" while a test program started by the volunteer finds the counter. Before
420a177 (2026-10-24) the task gave no reason and then slept for six hours (30 minutes since 1c30c7a). Since 420a177
stderr names the cause, for example `yhid: /dev/hidrawN: this is the counter the monitor looks for, but the BOINC
client's user may not open it. Install the udev rule ...` (monitor/yarrow_hid.c:239-241) or `yser: ...: permission
denied ...` (monitor/yarrow_serial.c:254-257).

## Cause

The BOINC client runs as its own user without a desktop session. hidraw nodes are root-only by default and tty nodes
belong to a group (`dialout` or `uucp`, YAR-013); a desktop session gets access through its seat, the client's user
does not.

## Fix

Workaround shipped in 420a177 and described in docs/linux.md: `linux/99-yarrow.rules`, and `linux/yarrow-linux-setup.sh`,
which writes the same rules and reloads udev. Mode 0666 for the Radioactive@Home ids (with the product name for the
shared V-USB and Microchip ids) and for the GC-03 (1209:0001, also `ID_MM_DEVICE_IGNORE`). GQ GMC ports are not covered
(their USB serial chips are shared with many devices): the BOINC user has to join the device's group. A client
installed as a Snap or Flatpak cannot see the devices at all (docs/linux.md:33-34). The volunteer has to run the script
as root once; the task cannot do it. The rules exist twice (rule file and setup script) and must stay identical.

## Test

On Linux without the rule: the stderr line appears once; with the rule and the counter plugged in again, the counter
is found within 15 s.

## Log

- 2026-10-21: listed by the audit (no udev rule shipped; R@H under the boinc user ended in 6 h pauses).
- 2026-10-27: recorded; rule, setup script and stderr hints verified by reading at 78b1446 (added in 420a177).
