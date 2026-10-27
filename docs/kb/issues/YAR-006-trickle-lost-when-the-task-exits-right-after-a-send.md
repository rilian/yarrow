---
id: YAR-006
title: A trickle sent right before a temporary exit or boinc_finish is lost with its bins, and two quick sends can overwrite each other
type: bug
status: confirmed
severity: medium
platforms: [all]
area: trickle
next: Send through one function that holds bins while trickle_up.xml exists and waits >= 3 s after the last send before any exit
pattern: PAT-009
aliases: [Y6]
reported: 2026-10-21
source: audit 2026-10-21
where: monitor/yarrow_monitor.c:57-72, 162-165 @ 78b1446
fixed_in:
---

## Symptom

Bins missing on the map for the last minutes before a counter was unplugged or lost for more than five minutes: the
bins finished since the last periodic send (normally none or one; up to 144 after a gap fill) never reach the server,
and they are no longer in the state file.

## Cause

Verified by reading the monitor at 78b1446 and BOINC at 4d1529d.

1. **Send, then exit at once.** After 300 s without a counter the loop calls `flush(1)`, `save_state()` and
   `glue_no_detector()` (monitor/yarrow_monitor.c:162-165), which is `boinc_temporary_exit(1800)`
   (monitor/yarrow_boinc.cpp:129). `flush()` drops the sent bins and saves the state before that (yarrow_monitor.c:67-71).
   `boinc_send_trickle_up()` only writes `trickle_up.xml` and sets a flag (`api/boinc_api.cpp:1632-1644`); the timer
   thread tells the client at the next one-second boundary (`1409`, `1449-1451`), and the client then moves the file
   (`client/app_control.cpp:1493-1507`). `boinc_temporary_exit()` exits without waiting (`884-899`), so the client is
   almost never told. The file stays in the slot directory, nothing announces it after the restart, and the next send
   overwrites it. The quit branch (YAR-004) does the same with `flush(1)` and `boinc_finish(0)`; the 2 s sleep in
   `boinc_finish()` does not help, because it sets `finishing` first (`858`) and the timer handler then returns before
   the trickle step (`1379-1386`).
2. **Two forced sends close together.** `flush()` never checks whether the client has taken the previous
   `trickle_up.xml`. When more than one chunk (144 slots) is pending, two sends within about two seconds lose the
   first: the start-up `flush(1)` (139) followed by the identity-change `flush(1)` (175), or a periodic `flush(0)`
   (220) followed by a calibration-change `flush(1)` (213) in the next iteration. More than one chunk is pending only
   after a gap fill (up to 1008 bins) or failed sends.

The send's return value is checked (67), so a failed `fopen` keeps the bins pending.

## Fix

Proposed, as in PAT-009: one send function that (a) keeps the bins pending while `trickle_up.xml` still exists in the
slot directory or less than a few seconds have passed since the previous send; (b) sends anyway once such a file is
much older than a normal pick-up (a file left by the previous run of the task must not block sending for good; its
content can also be sent again at start-up); (c) waits at least 3 s after the last send, and until the file is gone,
before `boinc_temporary_exit()`, `boinc_finish()` or `exit()`. The same wait lets the client see the checkpoint the
monitor reports before a temporary exit, which YAR-030 needs. A smaller fix for the no-counter exit: do not send there
at all; the bins stay in the state and go out after the restart. For volunteers: no lost bins when a
counter is unplugged; the task waits a few seconds longer before it sleeps.

## Test

A BOINC stub whose `boinc_send_trickle_up()` records each message and fails while the previous one was not taken
(PAT-009). Manual: under a real client with `<trickle_debug>`, unplug the counter for 6 minutes after a bin finished and
before the next periodic send; the client log must show the trickle file being moved.

## Log

- 2026-10-21: suspected by the audit at 1ecf955 (plan item Y6, "only if confirmed").
- 2026-10-27: recorded from the 2026-10-21 audit; confirmed by reading at 78b1446 and BOINC 4d1529d. The send before the no-counter exit was not in the audit.
- 2026-10-27: re-checked after PAT-009 and boinc-api.md were corrected; verdict unchanged; Fix aligned with the pattern (3 s wait, stale file).
