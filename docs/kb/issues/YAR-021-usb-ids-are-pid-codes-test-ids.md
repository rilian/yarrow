---
id: YAR-021
title: The firmware, the hub and the udev rule use the pid.codes test USB id 1209:0001
type: design-note
status: documented
severity: low
platforms: [all]
area: device-selection
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: hal/gc03/usb_gc03.c:107; hub/crates/yarrow-core/src/transport.rs:8-10; linux/99-yarrow.rules:13 @ 78b1446
fixed_in:
---

## Symptom

None reported. Another device that uses the same test id is taken for a Yarrow detector by the hub, and the udev rule
opens it to every user.

## Cause

The GC-03 with the Yarrow firmware enumerates as 1209:0001 (device descriptor bytes 8 to 11,
hal/gc03/usb_gc03.c:107). The hub recognises a Yarrow port by that id ("These are test ids until the project gets its
own", hub/crates/yarrow-core/src/transport.rs:8-10), and the udev rule gives mode 0666 and `ID_MM_DEVICE_IGNORE` to
every tty with that id (linux/99-yarrow.rules:13, and the copy in linux/yarrow-linux-setup.sh:17). 1209:0001 is the
pid.codes test id, free for anyone to use while experimenting. The monitor does not filter by id at all (YAR-011).
Verified by reading at 78b1446.

## Fix

An own product id from pid.codes, changed in the firmware descriptor, the hub, the udev rule and the setup script
together, while still accepting 1209:0001 from detectors with older firmware.

## Test

None.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446.
