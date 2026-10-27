---
id: YAR-016
title: The monitor's monotonic clock stops during system suspend on Linux but keeps running on macOS
type: gotcha
status: documented
severity: info
platforms: [all]
area: determinism
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: monitor/yarrow_serial.c:66-72, 130-135 @ 78b1446
fixed_in:
---

## Symptom

After the computer sleeps for a short time inside a bin, a Yarrow detector's bin is dropped on Linux, and on macOS it
can be kept (if the serial port survives the sleep). The 10-minute send interval and the 5-minute no-counter limit
count sleep time on macOS but not on Linux.

## Cause

`yser_now()` is `CLOCK_MONOTONIC` on POSIX and `QueryPerformanceCounter` on Windows (monitor/yarrow_serial.c:130-135,
66-72). Linux `CLOCK_MONOTONIC` does not advance while the system is suspended (`CLOCK_BOOTTIME` does); macOS
`CLOCK_MONOTONIC` keeps counting during sleep (clock_gettime(3) on macOS). Windows was not checked. The Yarrow source
stamps each sample with `yser_now()` (monitor/yarrow_src_yarrow.c:72), and `discontinuity()` closes the open bin when
the device clock and that host time disagree by more than 3 s (monitor/yarrow_bin.c:21-27): after a Linux suspend the
device clock moved on and the host clock did not. The timers in the main loop (`last_send`, `next_find`,
`searching_since`, `next_cal`) also use `yser_now()`. A BOINC suspend is different: the worker sits in the signal
handler and all clocks keep running. Code verified by reading at 78b1446; the clock behaviour is from the systems'
documentation.

## Fix

None needed: the bin rule never accepts a wrong bin on either system, only the number of bins kept around a sleep
differs. `CLOCK_BOOTTIME` on Linux would make both behave the same.

## Test

Linux laptop: suspend for 2 minutes in the middle of a 10-minute bin; that bin is not sent.

## Log

- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446.
