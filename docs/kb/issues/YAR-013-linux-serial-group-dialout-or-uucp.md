---
id: YAR-013
title: Linux: the serial-port group is dialout on Debian and Fedora but uucp on Arch, and the docs name only dialout
type: gotcha
status: documented
severity: low
platforms: [linux]
area: hardware-io
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: docs/boinc-monitor.md:93; docs/linux.md:21; monitor/yarrow_serial.c:255-257 @ 78b1446
fixed_in:
---

## Symptom

`usermod -aG dialout boinc` fails with "group 'dialout' does not exist" on Arch Linux and its derivatives, and a GQ GMC
counter (not covered by the udev rule, YAR-012) stays out of reach of the task.

## Cause

Serial devices belong to group `uucp` on Arch; Debian, Ubuntu, Raspberry Pi OS and Fedora use `dialout`. The docs
(docs/boinc-monitor.md:93, docs/linux.md:21) and the stderr hint (monitor/yarrow_serial.c:255-257, "usually dialout")
name only `dialout`. The group names are distribution facts and were not checked on each distribution here.

## Fix

Workaround: `ls -l /dev/ttyUSB0` shows the group; add the BOINC client's user to that group and restart the client.
Proposed: the docs and the hint say "the group shown by `ls -l` (dialout, or uucp on Arch)".

## Test

Documentation only.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified at 78b1446 (git grep dialout, uucp).
