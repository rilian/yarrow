---
id: YAR-029
title: docs/data.md says the device's stored log is never sent, but the monitor fills gaps from it since a4a7d47
type: bug
status: confirmed
severity: low
platforms: [all]
area: docs
next: Reword docs/data.md:10 to match docs/boinc-monitor.md Principles: live counts plus one gap filled from the detector's own log
pattern:
aliases: []
reported: 2026-10-27
source: review 2026-10-27
where: docs/data.md:10 @ 78b1446
fixed_in:
---

## Symptom

docs/data.md:10: "Only live counts are sent, never the device's stored log."

## Cause

Since a4a7d47 (2026-10-25) the monitor reads a Yarrow detector's flash log and sends bins for the time it was not
running (monitor/yarrow_monitor.c:76-96, monitor/yarrow_src_yarrow.c:106-148), as docs/boinc-monitor.md:8-14
describes. data.md was last changed in 9873455, before that. A second docs sentence that the code contradicts is
docs/boinc-monitor.md:61 (YAR-011). Verified by reading at 78b1446.

## Fix

Reword docs/data.md:10 after docs/boinc-monitor.md "Principles": live counts, plus one gap filled from the detector's
own log under the conditions listed there.

## Test

Documentation only.

## Log

- 2026-10-27: found while reading the docs at 78b1446.
