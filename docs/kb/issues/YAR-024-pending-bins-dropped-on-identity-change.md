---
id: YAR-024
title: Pending bins beyond the first trickle are dropped when the counter's identity, calibration or bin length changes
type: design-note
status: documented
severity: low
platforms: [all]
area: trickle
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 6.3)
where: monitor/yarrow_monitor.c:173-177 @ 78b1446
fixed_in:
---

## Symptom

None reported. After the volunteer changed the bin length in the preferences, or plugged in another counter, some
finished bins of the previous set-up are never sent.

## Cause

When the counter found differs from `dev` in uid, sensitivity, dead time or interval, the loop calls `flush(1)` and
then `ypending_drop(&pending, pending.n)` (monitor/yarrow_monitor.c:173-177). `flush()` sends one chunk (at most 144
slots); the rest, or everything when the send failed, is dropped. At start-up this follows the start-up `flush(1)`
(139) within seconds, so it is also a second send that can overwrite the first (YAR-006). Typical triggers: a new bin
length in the preferences (the `D` line keeps the old interval), or another counter. Deliberate: those bins were
counted under another set-up and `dev` can describe only one. Verified by reading at 78b1446.

## Fix

If the loss matters: send all chunks with the old identity first, spaced as YAR-006 needs.

## Test

`test_monitor.c` could cover it with more than 144 pending bins and an identity change.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446.
