---
id: YAR-017
title: Radioactive@Home and GQ GMC samples use the wall clock as host time, so the clock-jump check cannot fire for them
type: design-note
status: documented
severity: low
platforms: [all]
area: determinism
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: monitor/yarrow_src_rah.c:56-80; monitor/yarrow_src_gmc.c:83-93 @ 78b1446
fixed_in:
---

## Symptom

None visible. A step of the computer's clock inside a bin that is smaller than the tube-time slack goes unnoticed for
these counters; the bin is then reported with a counting time that differs from the interval by the step.

## Cause

`ysample_t.host_s` is documented as the "host monotonic clock" (monitor/yarrow_bin.h:13) and `discontinuity()`
compares its change with the change of the sample time `t` (monitor/yarrow_bin.c:21-27). For the Yarrow detector `t`
is the device clock and `host_s` is `yser_now()`, so the check works. Radioactive@Home and GQ GMC counters have no
clock: both `t` and `host_s` come from `yhost_wall()` (CLOCK_REALTIME or GetSystemTimePreciseAsFileTime,
monitor/yarrow_hid.c:97-114, 337-342; monitor/yarrow_src.c:50, monitor/yarrow_src_rah.c:170), so the difference is
always 0. What still protects them: a step back is caught by `s->t < p->t`; the tube-time check of `hv_s` against the
span (the device's millisecond timer for R@H, the heartbeat count for GMC; slack 1 s + 0.5 %,
monitor/yarrow_bin.c:45-46) catches steps above about 4 s in a 10-minute bin; GMC heartbeat gaps outside 0.5 to 1.5 s
restart the stream (monitor/yarrow_src_gmc.c:87-88). NTP normally slews the clock instead of stepping it. Verified by
reading at 78b1446.

## Fix

None required. If wanted: set `host_s` from `yser_now()` for these sources too, and keep the wall clock for `t` only.

## Test

`test_rah.c` / `test_gmc.c` with a fake wall clock that steps by 3 s inside a bin.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446.
