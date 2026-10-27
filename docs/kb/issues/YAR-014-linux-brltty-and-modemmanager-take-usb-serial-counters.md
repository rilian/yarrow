---
id: YAR-014
title: Linux: brltty claims CH340 USB serial adapters and ModemManager probes new ttyACM ports
type: gotcha
status: documented
severity: low
platforms: [linux]
area: hardware-io
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: linux/99-yarrow.rules:12-13 @ 78b1446
fixed_in:
---

## Symptom

A counter on a CH340 USB serial chip (GQ GMC counters are commonly built with one; not checked per model here) appears
as `/dev/ttyUSB0` and disappears a moment later, so the monitor never finds it; the kernel log shows the interface
claimed by brltty. A device on `/dev/ttyACM*` receives AT commands from ModemManager for a few seconds after it is
plugged in, which can delay or confuse the first answers.

## Cause

brltty's udev rules on several distributions (Ubuntu 22.04 and later among them) match the CH340/CH341 id 1a86:7523,
which some Braille displays use. ModemManager probes new ttyACM devices unless they carry `ID_MM_DEVICE_IGNORE`. These
are distribution facts, not verified in this repository; at 78b1446 nothing in the repository mentions brltty
(`git grep -i brltty`).

## Fix

ModemManager: the udev rule sets `ID_MM_DEVICE_IGNORE` for the GC-03 (1209:0001) since 420a177
(linux/99-yarrow.rules:12-13); other ttyACM counters are not covered. brltty: remove the brltty package when no
Braille display is used, or disable its rule for 1a86:7523. Proposed: a line in docs/linux.md for GQ GMC owners.

## Test

Ubuntu 22.04 with brltty installed: plug in a CH340 counter, `dmesg` shows the claim; after removing brltty the
`ttyUSB` node stays.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit; the ModemManager rule for the GC-03 verified at 78b1446, the brltty behaviour not verified here.
