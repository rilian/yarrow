---
id: YAR-030
title: Without a counter the monitor takes a temporary exit about every 35 minutes, and the client aborts the task after more than 100 of them
type: bug
status: confirmed
severity: medium
platforms: [all]
area: boinc-lifecycle
next: Before each boinc_temporary_exit(), call boinc_checkpoint_completed() and wait >= 3 s so the client sees a checkpoint (same wait as YAR-006)
pattern: PAT-030
aliases: []
reported: 2026-10-27
source: review 2026-10-27, after the client limit was added to the shared boinc-api.md
where: monitor/yarrow_monitor.c:108-117, 162-166; monitor/yarrow_boinc.cpp:125-130 @ 78b1446
fixed_in:
---

## Symptom

Expected from the code, not reported yet. A volunteer who selected the monitor but has no counter attached (unplugged
for a few days, or never plugged in) gets the task aborted by the client after about 2.5 days with "too many
boinc_temporary_exit()s" (exit status `ERR_TOO_MANY_EXITS`); it is reported as an error and a new task is sent. With
missing country or city the same happens after about 4 days, for a second monitor held off by the lock after about
8 days.

## Cause

The BOINC client counts the premature exits of a task and aborts it at more than 100 (`client/app_control.cpp:383-392`
at BOINC 4d1529d). The count goes back to 0 only when a status message from the app carries a new checkpoint CPU time
(`client/app_control.cpp:1551-1566`), or when the client restarts (the count is not saved, `client/app.cpp:136`,
`144`). The app sends status messages on one-second ticks ([boinc-api.md](../shared/boinc-api.md#timer-and-signals-unix)).

- No counter: the loop searches for 300 s, then calls `save_state()` (whose `boinc_checkpoint_completed()` sets a new
  checkpoint time, monitor/yarrow_boinc.cpp:88) and `boinc_temporary_exit(1800)` at once (monitor/yarrow_monitor.c:162-165,
  yarrow_boinc.cpp:129). The new checkpoint time is almost never reported before the exit, and nothing else in such a
  run reports one. One cycle is about 300 + 1800 s, so the 101st exit comes after about 59 hours.
- Missing settings: `boinc_temporary_exit(3600)` before any checkpoint (yarrow_monitor.c:108-111): about 4 days.
- Lock held by another monitor: `boinc_temporary_exit(7200)` (112-117): about 8 days.

With a counter attached the state is saved every 60 s (204-206) and the count is reset. Before 1c30c7a (6 h instead of
30 min) the no-counter limit was about 26 days away. A client restart resets the count, so computers that are switched
off every night do not reach it. Verified by reading the monitor at 78b1446 and the BOINC client and library at
4d1529d; not reproduced.

## Fix

Proposed: before each temporary exit, let the client see a checkpoint: call `boinc_checkpoint_completed()` and wait
more than one second (past a timer tick). The wait of at least 3 s that PAT-009 asks for before an exit (YAR-006) does
both. Alternatives: keep the task alive and search on with a long pause instead of exiting (an NCI task that sleeps
costs nothing and notices a plugged-in counter sooner), or count the exits in the state file and lengthen the delay.
Whether an abort after days without a counter is acceptable is for the maintainers (YAR-027); each abort is an error
result for the host.

## Test

Hard to automate (it is the client that counts). Manual: a client with `<task_debug>` and no counter attached; after
the change the client log must show "checkpointed" in each cycle. A client built with a lower limit shortens the test.

## Log

- 2026-10-27: found while re-checking YAR-020 against the corrected boinc-api.md; client code read at 4d1529d.

- 2026-10-27: linked to PAT-030, the shared pattern written from this issue.
