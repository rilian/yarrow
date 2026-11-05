---
id: YAR-035
title: Monitor: one poll and one read per byte of a serial reply, and a full state-file write per gap-filled bin
type: bug
status: fix-proposed
severity: low
platforms: [all]
area: performance
action: review
next: Review the block reads (POSIX) and the single save after a gap fill; do the Windows half together with YAR-002
blocked_by: []
pattern:
aliases: []
reported: 2026-11-04
source: M4 benchmark 2026-11-04 (docs/BENCHMARK.md, Results)
where: monitor/yarrow_serial.c:122-126, monitor/yarrow_monitor.c:34-39 @ ea3ab38
fixed_in:
---

## Symptom

Wasted work on a volunteer's idle machine, no wrong data: the monitor made about 200,000 system calls per hour with a Yarrow
detector attached, and a gap fill after 13 days away rewrote the whole state file about 1,870 times (10.5 MB written per
simulated hour of the `gap-fill` profile).

## Cause

`rd()` in the POSIX half of `monitor/yarrow_serial.c:122-126` (ea3ab38) did one `poll()` and one 1-byte `read()` per byte of
every reply. `on_bin()` (`monitor/yarrow_monitor.c:34-39`) saved the state file after every bin, also for each bin a gap
fill delivers from the detector's log.

## Fix

This branch: `rd()` reads what the port holds into a 256-byte buffer per port and hands it out byte by byte; `yser_read()`
takes buffered bytes first and `yser_drain()` drops them, so bytes after a reply stay for the next call as they stayed in
the driver before. During a gap fill `on_bin()` only counts, and `fill_gap()` saves once at the end; a crash in between
refills the same gap from the detector's log at the next start. Measured with `scripts/bench/ab.sh` on an Apple M4 Pro:
80-96 % fewer system calls on every Yarrow profile, state bytes of a gap fill -97 %, its CPU time 12x lower, the same trickles
and state. The Windows half is unchanged (YAR-002, see docs/BENCHMARK.md).

## Test

`scripts/bench/ab.sh` (same output on all eight profiles); `make monitor-check`; compiles with clang, GCC 12 and MinGW-w64.

## Log

- 2026-11-04: created; fix written and measured.
