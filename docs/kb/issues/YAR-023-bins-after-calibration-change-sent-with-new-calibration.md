---
id: YAR-023
title: Bins still pending after a calibration change are sent with the new calibration
type: design-note
status: documented
severity: low
platforms: [all]
area: trickle
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 6.3)
where: monitor/yarrow_monitor.c:204-217 @ 78b1446
fixed_in:
---

## Symptom

None reported. Some bins counted under the old calibration of a Yarrow detector reach the server with the new
sensitivity and dead time, so the server computes a wrong dose rate for them (it converts with the stated calibration,
docs/data.md:9).

## Cause

The housekeeping step (every 60 s, Yarrow detector only) reports a new calibration; the loop calls `flush(1)` with the
old values and then sets `dev.sens_x100` and `dev.dead_us` (monitor/yarrow_monitor.c:212-216). `flush()` sends only
the first chunk (at most 144 slots, `ypending_xml()`), and only when the send works. The bins beyond the first chunk,
or all of them when the send failed, stay pending and go out later with the new values; `save_state()` also writes the
new values into the `D` line. More than one chunk is pending only after a gap fill or failed sends, so this is rare.
The comment "bins sent so far keep the calibration they were counted with" (213) holds for the first chunk. Verified by
reading at 78b1446.

## Fix

If it matters: send every pending chunk with the old calibration before switching, spaced as YAR-006 needs, or store
the calibration with each pending bin.

## Test

`test_monitor.c` could cover it with more than 144 pending bins and a calibration change.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446.
