---
id: YAR-015
title: macOS: the monitor has no HID backend, so Radioactive@Home counters are never found there, while the hub has one
type: design-note
status: documented
severity: low
platforms: [macos]
area: device-selection
pattern: PAT-028
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: monitor/yarrow_hid.c:329-335 @ 78b1446
fixed_in:
---

## Symptom

A macOS build of the monitor would never find a Radioactive@Home counter; Yarrow Hub on the same Mac reads it.

## Cause

`yhid_open()` is implemented for Linux (hidraw) and Windows (hid.dll); on any other system it returns -1 ("no HID
access on this system yet", monitor/yarrow_hid.c:329-335). The hub uses hidapi with the `macos-shared-device` feature
(hub/crates/yarrow-core/Cargo.toml:23). The project site lists Windows and Linux (site/build.py:295); whether a macOS
monitor is built is decided outside this repository. Verified by reading at 78b1446.

## Fix

Only if a macOS monitor ships: an IOKit backend (`IOHIDDeviceGetReport`) or hidapi, as in the hub. Until then the docs
could say that Radioactive@Home counters need Windows or Linux for the monitor.

## Test

None.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446.
- 2026-10-27: linked to PAT-028 (a feature silently left out of a build): the stub says nothing at run time. The ledger row is not-affected because no macOS monitor build is known to ship; it becomes affected if one does.
