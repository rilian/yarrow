---
id: YAR-020
title: Temporary exits: settings missing 1 h, no counter for 5 minutes 30 min, another monitor 2 h
type: design-note
status: documented
severity: info
platforms: [all]
area: boinc-lifecycle
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 6.3, "a 6-hour temporary exit after five minutes without a detector")
where: monitor/yarrow_boinc.cpp:125-130; monitor/yarrow_monitor.c:108-117, 162-166 @ 78b1446
fixed_in:
---

## Symptom

The BOINC Manager shows the task as postponed, with the reason as a notice: "Set your country ... and your city ..."
(1 h), "No detector found ..." (30 min), "Another Yarrow monitor is already running ..." (2 h).

## Cause

All three are `boinc_temporary_exit(delay, why, true)` (monitor/yarrow_boinc.cpp:125-130): missing or invalid country
or city, 3600 s (yarrow_monitor.c:108-111); no counter found for 300 s of searching, 1800 s (162-166); the lock held by
another monitor, 7200 s (112-117). History: the no-counter delay was 6 h until 1c30c7a (2026-10-25: a counter plugged
in again waited most of a day for its task); 1c30c7a gave the lock case its own exit, and c8acefc (2026-10-26) set that
one to 2 h. The values match docs/boinc-monitor.md:36 and :90 and docs/linux.md:14 at 78b1446. `boinc_temporary_exit()`
keeps the task and its slot, but the client aborts a task after more than 100 premature exits without a reported
checkpoint ([boinc-api.md](../shared/boinc-api.md#exit-paths)): at these delays that is about 2.5 days without a
counter (YAR-030). Verified by reading at 78b1446.

## Fix

None for the durations themselves. The no-counter exit sends a trickle right before it exits, which is lost (YAR-006), and the exits count towards the client's limit (YAR-030).

## Test

None automatic. Under a client: start without a counter; after about 5 minutes the task is postponed for 1800 s.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit; durations changed upstream in 1c30c7a and c8acefc, verified by reading at 78b1446.
- 2026-10-27: corrected after boinc-api.md was corrected: there is a client limit of 100 temporary exits; see YAR-030.
