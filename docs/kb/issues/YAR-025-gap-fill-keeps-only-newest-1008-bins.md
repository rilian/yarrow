---
id: YAR-025
title: A gap fill longer than 1008 bins keeps only the newest 1008, although it reads back up to 13 days
type: bug
status: confirmed
severity: low
platforms: [all]
area: trickle
next: In fill_gap(), start no earlier than until - YT_MAX_PENDING * interval (less the bins already pending) and log the bins kept
pattern:
aliases: []
reported: 2026-10-27
source: review 2026-10-27
where: monitor/yarrow_monitor.c:30, 76-96; monitor/yarrow_trickle.c:7-20 @ 78b1446
fixed_in:
---

## Symptom

After a Yarrow detector was away from the monitor for more than 7 days with 10-minute bins, stderr says "the
detector's log filled N bins that were missed ..." with N up to 1872, but only the newest 1008 bins (7 days) are sent;
the older part of the gap stays empty on the map.

## Cause

`fill_gap()` reads the log back for up to `BACKFILL_MAX_AGE_S` = 13 days (monitor/yarrow_monitor.c:30, 84-85) and
emits the bins oldest first (`ylog_scan()` starts after the newest page, src/ylog.c:338-364) through `on_bin()` and
`ypending_add()`. The pending queue holds `YT_MAX_PENDING` = 1008 bins (monitor/yarrow_trickle.h:8); when it is full,
`ypending_add()` drops the oldest (monitor/yarrow_trickle.c:10-15), and its return value is ignored
(yarrow_monitor.c:37). 13 days are 1872 bins of 10 minutes, 624 of 30 minutes and 312 of one hour, so only 10-minute
bins are affected. The count in the log line includes the dropped bins (yarrow_monitor.c:88-92). The comment at :30
says the server takes bins up to 14 days old. Verified by reading at 78b1446; not reproduced (`test_backfill.c` does
not overflow the queue).

## Fix

Limit the gap fill to what the queue can hold (start no earlier than `until - YT_MAX_PENDING * interval`, less when
bins are already pending), or raise `YT_MAX_PENDING` to 13 days of 10-minute bins (1872 lines, about 40 KB of state;
the buffers in `save_state()` and `main()` are sized from it). Log the number of bins actually kept.

## Test

`test_backfill.c` (or a new monitor test) with a log of 10 days of 10-minute bins: the oldest kept bin is the one the
limit gives, and the reported count equals the kept count.

## Log

- 2026-10-27: found while reading the gap fill (a4a7d47) at 78b1446.
