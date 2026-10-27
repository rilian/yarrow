---
id: YAR-004
title: On quit or abort the main loop sends, saves and ends through boinc_finish(0)
type: bug
status: confirmed
severity: low
platforms: [all]
area: boinc-lifecycle
action: decide
next: Decide with YAR-027: on quit, save and exit without boinc_finish(0)? Then remove the loop's quit branch (yarrow_monitor.c:223-227)
blocked_by: [YAR-027]
pattern: PAT-008
aliases: [Y4]
reported: 2026-10-21
source: audit 2026-10-21
where: monitor/yarrow_monitor.c:151, 223-227 @ 78b1446
fixed_in:
---

## Symptom

Not seen in practice. The code path reports a quitting or aborted monitor task as finished successfully. The docs say
a task should run for as long as the client does (docs/boinc-monitor.md:32, 88).

## Cause

The loop runs `while (!glue_quit_requested())` (monitor/yarrow_monitor.c:151); `glue_quit_requested()` returns
`quit_request || abort_request` from `boinc_get_status()` (monitor/yarrow_boinc.cpp:103-108). After the loop:
`flush(1)`, `save_state()`, close the source, `glue_done(0)` (yarrow_monitor.c:223-227), which writes `out` and calls
`boinc_finish(0)` (yarrow_boinc.cpp:112-123).

When the loop can see the flag (BOINC read at 4d1529d): the monitor uses `boinc_options_defaults()`, so
`direct_process_action` is 1 (yarrow_boinc.cpp:35-37), and it never opens a critical section (no
`boinc_time_to_checkpoint()` or `boinc_begin_critical_section()`; `boinc_checkpoint_completed()` only decrements a
counter that is clamped at 0, `api/boinc_api.cpp:1677-1689`). So the library carries out quit and abort itself:

- Windows: the timer thread sets `quit_request` (or `abort_request`) and ends the process at once through
  `exit_from_timer_thread()` and `boinc_exit()` (`api/boinc_api.cpp:1325-1349`, `996-1014`). The loop practically
  never sees the flag.
- Linux and macOS: the timer thread sets the flag, sets `worker_thread_exit_flag` and leaves with `pthread_exit`
  (`1015-1028`); the worker's SIGALRM handler calls `boinc_exit()` at the next signal, at most 0.1 s later
  (`1528-1532`). If the loop's check falls into that window, `flush(1)`, `save_state()` and `glue_done(0)` start, and
  the handler ends the process during them or during the 2 s sleep at the start of `boinc_finish()` (`858-860`),
  before `boinc_finish_called` is written (`863`). The task is then not reported as finished. What remains is a
  forced send just before the exit, which is lost (YAR-006), and an `out` file written early.
- The branch becomes live, and every quit and abort would end as a success, if the monitor sets
  `direct_process_action = false`, or calls `glue_quit_requested()` inside a critical section.

Verified by reading the monitor at 78b1446 and BOINC at 4d1529d; not reproduced.

## Fix

Proposed, pending the answer to YAR-027: on quit, save the state and exit without `boinc_finish` and without a forced
send, so the client resumes the task; on abort, exit without reporting success. The smallest change: drop the BOINC
host's own quit handling (leave both to the library) and keep `glue_quit_requested()` for the command-line host only.
For volunteers nothing changes in normal operation; a latent path to a "finished" monitor task goes away.

## Test

A BOINC stub whose `boinc_get_status()` reports `quit_request` after a few iterations (PAT-008 "How to test"): the run
must end without `boinc_finish` and without a send in its last seconds, and a second run must load the saved state.
There is no BOINC stub in the repository yet.

## Log

- 2026-10-21: found by the audit at 1ecf955 (plan item Y4), "to confirm".
- 2026-10-27: recorded from the 2026-10-21 audit, verified by reading at 78b1446 and BOINC 4d1529d; the window in which the loop can see the flag described.
- 2026-10-27: re-checked after PAT-008 was corrected; verdict unchanged: affected, latent at 78b1446 (no `direct_process_action = false`, no critical section held from the check until `boinc_finish()`).
